/* Gerris - The GNU Flow Solver
 * Copyright (C) 2001 National Institute of Water and Atmospheric Research
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.  
 */

#include <stdlib.h>
#include <math.h>

#include "adaptive.h"
#include "solid.h"

#include "graphic.h"

/*#define DEBUG*/

/**
 * gfs_cell_coarse_init:
 * @cell: a #FttCell.
 * @domain: a #GfsDomain containing @cell.
 *
 * Initialises the variables of @cell using the values of its children
 * cells.
 */
void gfs_cell_coarse_init (FttCell * cell, GfsDomain * domain)
{
  GSList * i;

  g_return_if_fail (cell != NULL);
  g_return_if_fail (!FTT_CELL_IS_LEAF (cell));
  g_return_if_fail (domain != NULL);

  i = domain->variables;
  while (i) {
    GfsVariable * v = i->data;

    (* v->fine_coarse) (cell, v);
    i = i->next;
  }
}

/* GfsAdapt: Object */

typedef struct {
  GfsSimulation * sim;
  guint nc;
  GtsEHeap * hcoarse, * hfine;
  gdouble clim;
  GfsVariable * hcoarsev, * hfinev, * costv, * c;
} AdaptParams;

static void gfs_adapt_destroy (GtsObject * o)
{
  gts_object_destroy (GTS_OBJECT (GFS_ADAPT (o)->minlevel));
  gts_object_destroy (GTS_OBJECT (GFS_ADAPT (o)->maxlevel));

  (* GTS_OBJECT_CLASS (gfs_adapt_class ())->parent_class->destroy) (o);
}

static void none (FttCell * cell, GfsVariable * v) {}

static void gfs_adapt_read (GtsObject ** o, GtsFile * fp)
{
  GfsAdapt * a = GFS_ADAPT (*o);

  if (GTS_OBJECT_CLASS (gfs_adapt_class ())->parent_class->read)
    (* GTS_OBJECT_CLASS (gfs_adapt_class ())->parent_class->read) 
      (o, fp);
  if (fp->type == GTS_ERROR)
    return;
  if (fp->type != '{') {
    gts_file_error (fp, "expecting an opening brace");
    return;
  }
  fp->scope_max++;
  gts_file_next_token (fp);

  while (fp->type != GTS_ERROR && fp->type != '}') {
    if (fp->type == '\n') {
      gts_file_next_token (fp);
      continue;
    }
    if (fp->type != GTS_STRING) {
      gts_file_error (fp, "expecting a keyword");
      return;
    }
    else if (!strcmp (fp->token->str, "minlevel")) {
      gts_file_next_token (fp);
      if (fp->type != '=') {
	gts_file_error (fp, "expecting '='");
	return;
      }
      gts_file_next_token (fp);
      gfs_function_read (a->minlevel, gfs_object_simulation (*o), fp);
    }
    else if (!strcmp (fp->token->str, "maxlevel")) {
      gts_file_next_token (fp);
      if (fp->type != '=') {
	gts_file_error (fp, "expecting '='");
	return;
      }
      gts_file_next_token (fp);
      gfs_function_read (a->maxlevel, gfs_object_simulation (*o), fp);
    }
    else if (!strcmp (fp->token->str, "mincells")) {
      gts_file_next_token (fp);
      if (fp->type != '=') {
	gts_file_error (fp, "expecting '='");
	return;
      }
      gts_file_next_token (fp);
      if (fp->type != GTS_INT) {
	gts_file_error (fp, "expecting an integer (mincells)");
	return;
      }
      a->mincells = atoi (fp->token->str);
      gts_file_next_token (fp);
    }
    else if (!strcmp (fp->token->str, "maxcells")) {
      gts_file_next_token (fp);
      if (fp->type != '=') {
	gts_file_error (fp, "expecting '='");
	return;
      }
      gts_file_next_token (fp);
      if (fp->type != GTS_INT) {
	gts_file_error (fp, "expecting an integer (maxcells)");
	return;
      }
      a->maxcells = atoi (fp->token->str);
      gts_file_next_token (fp);
    }
    else if (!strcmp (fp->token->str, "cmax")) {
      gts_file_next_token (fp);
      if (fp->type != '=') {
	gts_file_error (fp, "expecting '='");
	return;
      }
      gts_file_next_token (fp);
      a->cmax = gfs_read_constant (fp, gfs_object_simulation (*o));
      if (fp->type == GTS_ERROR)
	return;
    }
    else if (!strcmp (fp->token->str, "weight")) {
      gts_file_next_token (fp);
      if (fp->type != '=') {
	gts_file_error (fp, "expecting '='");
	return;
      }
      gts_file_next_token (fp);
      a->weight = gfs_read_constant (fp, gfs_object_simulation (*o));
      if (fp->type == GTS_ERROR)      
	return;
    }
    else if (!strcmp (fp->token->str, "cfactor")) {
      gts_file_next_token (fp);
      if (fp->type != '=') {
	gts_file_error (fp, "expecting '='");
	return;
      }
      gts_file_next_token (fp);
      a->cfactor = gfs_read_constant (fp, gfs_object_simulation (*o));
      if (fp->type == GTS_ERROR)
	return;
    }
    else if (!strcmp (fp->token->str, "c")) {
      GfsDomain * domain;

      gts_file_next_token (fp);
      if (fp->type != '=') {
	gts_file_error (fp, "expecting '='");
	return;
      }
      gts_file_next_token (fp);
      if (fp->type != GTS_STRING) {
	gts_file_error (fp, "expecting a variable name");
	return;
      }
      domain = GFS_DOMAIN (gfs_object_simulation (*o));
      a->c = gfs_domain_get_or_add_variable (domain, fp->token->str, "Adaptive refinement cost");
      if (!a->c) {
	gts_file_error (fp, "`%s' is a reserved keyword", fp->token->str);
	return;
      }
      a->c->fine_coarse = none;
      gts_file_next_token (fp);
    }
    else {
      gts_file_error (fp, "unknown keyword `%s'", fp->token->str);
      return;
    }
  }
  if (fp->type == GTS_ERROR)
    return;
  if (fp->type != '}') {
    gts_file_error (fp, "expecting a closing brace");
    return;
  }
  fp->scope_max--;
  gts_file_next_token (fp);
}

static void gfs_adapt_write (GtsObject * o, FILE * fp)
{
  GfsAdapt * a = GFS_ADAPT (o);

  if (GTS_OBJECT_CLASS (gfs_adapt_class ())->parent_class->write)
    (* GTS_OBJECT_CLASS (gfs_adapt_class ())->parent_class->write) 
      (o, fp);
  fputs (" { minlevel =", fp);
  gfs_function_write (a->minlevel, fp);
  fputs (" maxlevel =", fp);
  gfs_function_write (a->maxlevel, fp);
  fputc (' ', fp);
  if (a->mincells > 0)
    fprintf (fp, "mincells = %u ", a->mincells);
  if (a->maxcells < G_MAXINT)
    fprintf (fp, "maxcells = %u ", a->maxcells);
  if (a->cmax > 0.)
    fprintf (fp, "cmax = %g ", a->cmax);
  if (a->weight != 1.)
    fprintf (fp, "weight = %g ", a->weight);
  if (a->cfactor != 4.)
    fprintf (fp, "cfactor = %g ", a->cfactor);
  if (a->c != NULL)
    fprintf (fp, "c = %s ", a->c->name);
  fputc ('}', fp);
}

static gboolean gfs_adapt_event (GfsEvent * event, GfsSimulation * sim)
{
  if (GFS_ADAPT (event)->cost == NULL) {
    gts_object_destroy (GTS_OBJECT (event));
    return FALSE;
  }
  if ((* GFS_EVENT_CLASS (GTS_OBJECT_CLASS (gfs_adapt_class ())->parent_class)->event) 
      (event, sim)) {
    GFS_ADAPT (event)->active = TRUE;
    return TRUE;
  }
  GFS_ADAPT (event)->active = FALSE;
  return FALSE;
}

static void gfs_adapt_class_init (GfsEventClass * klass)
{
  klass->event = gfs_adapt_event;
  GTS_OBJECT_CLASS (klass)->destroy = gfs_adapt_destroy;
  GTS_OBJECT_CLASS (klass)->read = gfs_adapt_read;
  GTS_OBJECT_CLASS (klass)->write = gfs_adapt_write;
}

static void gfs_adapt_init (GfsAdapt * object)
{
  object->active = FALSE;
  object->minlevel = gfs_function_new (gfs_function_class (), 0);
  object->maxlevel = gfs_function_new (gfs_function_class (), 5);
  object->mincells = 0;
  object->maxcells = G_MAXINT;
  object->cmax = 0.;
  object->weight = 1.;
  object->cfactor = 4.;
  object->c = NULL;
}

GfsEventClass * gfs_adapt_class (void)
{
  static GfsEventClass * klass = NULL;

  if (klass == NULL) {
    GtsObjectClassInfo gfs_adapt_info = {
      "GfsAdapt",
      sizeof (GfsAdapt),
      sizeof (GfsEventClass),
      (GtsObjectClassInitFunc) gfs_adapt_class_init,
      (GtsObjectInitFunc) gfs_adapt_init,
      (GtsArgSetFunc) NULL,
      (GtsArgGetFunc) NULL
    };
    klass = gts_object_class_new (GTS_OBJECT_CLASS (gfs_event_class ()),
				  &gfs_adapt_info);
  }

  return klass;
}

/* GfsAdaptVorticity: Object */

static gboolean gfs_adapt_vorticity_event (GfsEvent * event, 
					   GfsSimulation * sim)
{
  if ((* GFS_EVENT_CLASS (GTS_OBJECT_CLASS (gfs_adapt_vorticity_class ())->parent_class)->event) 
      (event, sim)) {
    GfsAdaptVorticity * a = GFS_ADAPT_VORTICITY (event);

    a->u = gfs_domain_velocity (GFS_DOMAIN (sim));
    a->maxa = gfs_domain_norm_velocity (GFS_DOMAIN (sim), FTT_TRAVERSE_LEAFS, -1).infty;
    return TRUE;
  }
  return FALSE;
}

static void gfs_adapt_vorticity_class_init (GfsEventClass * klass)
{
  GFS_EVENT_CLASS (klass)->event = gfs_adapt_vorticity_event;
}

static gdouble cost_vorticity (FttCell * cell, GfsAdaptVorticity * a)
{
  if (a->maxa <= 0.)
    return 0.;
  return fabs (gfs_vorticity (cell, a->u))*ftt_cell_size (cell)/a->maxa;
}

static void gfs_adapt_vorticity_init (GfsAdaptVorticity * object)
{
  GFS_ADAPT (object)->cost = (GtsKeyFunc) cost_vorticity;
}

GfsEventClass * gfs_adapt_vorticity_class (void)
{
  static GfsEventClass * klass = NULL;

  if (klass == NULL) {
    GtsObjectClassInfo gfs_adapt_vorticity_info = {
      "GfsAdaptVorticity",
      sizeof (GfsAdaptVorticity),
      sizeof (GfsEventClass),
      (GtsObjectClassInitFunc) gfs_adapt_vorticity_class_init,
      (GtsObjectInitFunc) gfs_adapt_vorticity_init,
      (GtsArgSetFunc) NULL,
      (GtsArgGetFunc) NULL
    };
    klass = gts_object_class_new (GTS_OBJECT_CLASS (gfs_adapt_class ()),
				  &gfs_adapt_vorticity_info);
  }

  return klass;
}

/* GfsAdaptStreamlineCurvature: Object */

static void gfs_adapt_streamline_curvature_init (GfsAdapt * object)
{
  object->cost = (GtsKeyFunc) gfs_streamline_curvature;
}

GfsEventClass * gfs_adapt_streamline_curvature_class (void)
{
  static GfsEventClass * klass = NULL;

  if (klass == NULL) {
    GtsObjectClassInfo gfs_adapt_streamline_curvature_info = {
      "GfsAdaptStreamlineCurvature",
      sizeof (GfsAdapt),
      sizeof (GfsEventClass),
      (GtsObjectClassInitFunc) NULL,
      (GtsObjectInitFunc) gfs_adapt_streamline_curvature_init,
      (GtsArgSetFunc) NULL,
      (GtsArgGetFunc) NULL
    };
    klass = gts_object_class_new (GTS_OBJECT_CLASS (gfs_adapt_class ()),
				  &gfs_adapt_streamline_curvature_info);
  }

  return klass;
}

/* GfsAdaptFunction: Object */

static void gfs_adapt_function_destroy (GtsObject * o)
{
  gts_object_destroy (GTS_OBJECT (GFS_ADAPT_FUNCTION (o)->f));

  (* GTS_OBJECT_CLASS (gfs_adapt_function_class ())->parent_class->destroy) (o);
}

static void gfs_adapt_function_read (GtsObject ** o, GtsFile * fp)
{
  (* GTS_OBJECT_CLASS (gfs_adapt_function_class ())->parent_class->read) (o, fp);
  if (fp->type == GTS_ERROR)
    return;

  gfs_function_read (GFS_ADAPT_FUNCTION (*o)->f, gfs_object_simulation (*o), fp);
}

static void gfs_adapt_function_write (GtsObject * o, FILE * fp)
{
  (* GTS_OBJECT_CLASS (gfs_adapt_function_class ())->parent_class->write) (o, fp);
  gfs_function_write (GFS_ADAPT_FUNCTION (o)->f, fp);
}

static void gfs_adapt_function_class_init (GtsObjectClass * klass)
{
  klass->destroy = gfs_adapt_function_destroy;  
  klass->read = gfs_adapt_function_read;
  klass->write = gfs_adapt_function_write;
}

static gdouble function_cost (FttCell * cell, GfsAdaptFunction * a)
{
  return gfs_function_value (a->f, cell);
}

static void gfs_adapt_function_init (GfsAdaptFunction * object)
{
  object->f = gfs_function_new (gfs_function_class (), 0.);
  GFS_ADAPT (object)->cost = (GtsKeyFunc) function_cost;
}

GfsEventClass * gfs_adapt_function_class (void)
{
  static GfsEventClass * klass = NULL;

  if (klass == NULL) {
    GtsObjectClassInfo gfs_adapt_function_info = {
      "GfsAdaptFunction",
      sizeof (GfsAdaptFunction),
      sizeof (GfsEventClass),
      (GtsObjectClassInitFunc) gfs_adapt_function_class_init,
      (GtsObjectInitFunc) gfs_adapt_function_init,
      (GtsArgSetFunc) NULL,
      (GtsArgGetFunc) NULL
    };
    klass = gts_object_class_new (GTS_OBJECT_CLASS (gfs_adapt_class ()),
				  &gfs_adapt_function_info);
  }

  return klass;
}

/* GfsAdaptGradient: Object */

static void gfs_adapt_gradient_destroy (GtsObject * o)
{
  if (GFS_ADAPT_GRADIENT (o)->v && !gfs_function_get_variable (GFS_ADAPT_FUNCTION (o)->f))
    gts_object_destroy (GTS_OBJECT (GFS_ADAPT_GRADIENT (o)->v));

  (* GTS_OBJECT_CLASS (gfs_adapt_gradient_class ())->parent_class->destroy) (o);
}

static void gfs_adapt_gradient_read (GtsObject ** o, GtsFile * fp)
{
  (* GTS_OBJECT_CLASS (gfs_adapt_gradient_class ())->parent_class->read) (o, fp);
  if (fp->type == GTS_ERROR)
    return;

  GfsAdaptGradient * a = GFS_ADAPT_GRADIENT (*o);
  a->v = gfs_function_get_variable (GFS_ADAPT_FUNCTION (a)->f);
  if (a->v == NULL)
    a->v = gfs_temporary_variable (GFS_DOMAIN (gfs_object_simulation (a)));
  a->dimension = pow (gfs_object_simulation (a)->physical_params.L, a->v->units);
}

static void update_f (FttCell * cell, GfsAdaptFunction * a)
{
  GFS_VALUE (cell, GFS_ADAPT_GRADIENT (a)->v) = gfs_function_value (a->f, cell);
}

static gboolean gfs_adapt_gradient_event (GfsEvent * event, 
					  GfsSimulation * sim)
{
  if ((* GFS_EVENT_CLASS (GTS_OBJECT_CLASS (gfs_adapt_gradient_class ())->parent_class)->event) 
      (event, sim)) {
    if (!gfs_function_get_variable (GFS_ADAPT_FUNCTION (event)->f)) {
      gfs_domain_cell_traverse (GFS_DOMAIN (sim), FTT_PRE_ORDER, FTT_TRAVERSE_LEAFS, -1,
				(FttCellTraverseFunc) update_f, event);
      GfsVariable * v = GFS_ADAPT_GRADIENT (event)->v;
      gfs_domain_cell_traverse (GFS_DOMAIN (sim),
				FTT_POST_ORDER, FTT_TRAVERSE_NON_LEAFS, -1,
				(FttCellTraverseFunc) v->fine_coarse, v);
      gfs_domain_bc (GFS_DOMAIN (sim), FTT_TRAVERSE_ALL, -1, v);
    }
    return TRUE;
  }
  return FALSE;
}

static void gfs_adapt_gradient_class_init (GfsEventClass * klass)
{
  GTS_OBJECT_CLASS (klass)->destroy = gfs_adapt_gradient_destroy;
  GTS_OBJECT_CLASS (klass)->read = gfs_adapt_gradient_read;
  klass->event = gfs_adapt_gradient_event;
}

static gdouble gradient_cost (FttCell * cell, GfsAdaptGradient * a)
{
  FttComponent c;
  gdouble sum2 = 0;
  gdouble * lambda;

  lambda = (gdouble *) &GFS_DOMAIN (gfs_object_simulation (a))->lambda;
  for (c = 0; c < FTT_DIMENSION; c++) {
    gdouble g = lambda[c]*gfs_center_gradient (cell, c, a->v->i);

    sum2 += g*g;
  }
  return sqrt (sum2)*a->dimension;
}

static void gfs_adapt_gradient_init (GfsAdaptGradient * object)
{
  GFS_ADAPT (object)->cost = (GtsKeyFunc) gradient_cost;
  GFS_ADAPT_GRADIENT (object)->dimension = 1.;
}

GfsEventClass * gfs_adapt_gradient_class (void)
{
  static GfsEventClass * klass = NULL;

  if (klass == NULL) {
    GtsObjectClassInfo gfs_adapt_gradient_info = {
      "GfsAdaptGradient",
      sizeof (GfsAdaptGradient),
      sizeof (GfsEventClass),
      (GtsObjectClassInitFunc) gfs_adapt_gradient_class_init,
      (GtsObjectInitFunc) gfs_adapt_gradient_init,
      (GtsArgSetFunc) NULL,
      (GtsArgGetFunc) NULL
    };
    klass = gts_object_class_new (GTS_OBJECT_CLASS (gfs_adapt_function_class ()),
				  &gfs_adapt_gradient_info);
  }

  return klass;
}

/* GfsAdaptError: Object */

static void gfs_adapt_error_destroy (GtsObject * o)
{
  if (GFS_ADAPT_ERROR (o)->v)
    gts_object_destroy (GTS_OBJECT (GFS_ADAPT_ERROR (o)->v));

  (* GTS_OBJECT_CLASS (gfs_adapt_error_class ())->parent_class->destroy) (o);
}

static void gfs_adapt_error_read (GtsObject ** o, GtsFile * fp)
{
  (* GTS_OBJECT_CLASS (gfs_adapt_error_class ())->parent_class->read) (o, fp);
  if (fp->type == GTS_ERROR)
    return;

  GFS_ADAPT_ERROR (*o)->v = gfs_temporary_variable (GFS_DOMAIN (gfs_object_simulation (*o)));
  GFS_ADAPT_ERROR (*o)->v->coarse_fine = none;
  GFS_ADAPT_ERROR (*o)->v->fine_coarse = none;
}

static gdouble center_regular_gradient (FttCell * cell, FttComponent c, GfsVariable * v)
{
  FttCell * n1 = ftt_cell_neighbor (cell, 2*c);
  guint level = ftt_cell_level (cell);
  if (n1) {
    if (ftt_cell_level (n1) < level)
      return center_regular_gradient (ftt_cell_parent (cell), c, v)/2.;
    FttCell * n2 = ftt_cell_neighbor (cell, 2*c + 1);
    if (n2) {
      if (ftt_cell_level (n2) < level)
	return center_regular_gradient (ftt_cell_parent (cell), c, v)/2.;
      /* two neighbors: second-order differencing (parabola) */
      return (GFS_VALUE (n1, v) - GFS_VALUE (n2, v))/2.;
    }
    else
      /* one neighbor: first-order differencing */
      return GFS_VALUE (n1, v) - GFS_VALUE (cell, v);
  }
  else {
    FttCell * n2 = ftt_cell_neighbor (cell, 2*c + 1);
    if (n2) {
      if (ftt_cell_level (n2) < level)
	return center_regular_gradient (ftt_cell_parent (cell), c, v)/2.;
      /* one neighbor: first-order differencing */
      return GFS_VALUE (cell, v) - GFS_VALUE (n2, v);
    }
  }
  /* no neighbors */
  return 0.;
}

static gdouble center_regular_2nd_derivative (FttCell * cell, FttComponent c, GfsVariable * v)
{
  FttCell * n1 = ftt_cell_neighbor (cell, 2*c);
  FttCell * n2 = ftt_cell_neighbor (cell, 2*c + 1);
  if (n1 && n2) {
    guint level = ftt_cell_level (cell);
    if (ftt_cell_level (n1) < level || ftt_cell_level (n2) < level)
      return center_regular_2nd_derivative (ftt_cell_parent (cell), c, v)/4.;
    return GFS_VALUE (n1, v) - 2.*GFS_VALUE (cell, v) + GFS_VALUE (n2, v);
  }
  /* one or no neighbors */
  return 0.;
}

static void compute_gradient (FttCell * cell, GfsAdaptError * a)
{
  GFS_VALUE (cell, a->dv) = center_regular_gradient (cell, a->dv->component, 
						     GFS_ADAPT_GRADIENT (a)->v);
}

static void add_hessian_norm (FttCell * cell, GfsAdaptError * a)
{
  /* off-diagonal */
  FttComponent j;
  for (j = 0; j < FTT_DIMENSION; j++)
    if (j != a->dv->component) {
      gdouble g = center_regular_gradient (cell, j, a->dv);
      GFS_VALUE (cell, a->v) += g*g;
    }
  /* diagonal */
  gdouble g = center_regular_2nd_derivative (cell, a->dv->component, GFS_ADAPT_GRADIENT (a)->v);
  GFS_VALUE (cell, a->v) += g*g;
}

static gboolean gfs_adapt_error_event (GfsEvent * event, 
				       GfsSimulation * sim)
{
  if ((* GFS_EVENT_CLASS (GTS_OBJECT_CLASS (gfs_adapt_error_class ())->parent_class)->event) 
      (event, sim)) {
    GfsAdaptError * a = GFS_ADAPT_ERROR (event);
    GfsDomain * domain = GFS_DOMAIN (sim);

    gfs_domain_cell_traverse (domain,
			      FTT_PRE_ORDER, FTT_TRAVERSE_ALL, -1,
			      (FttCellTraverseFunc) gfs_cell_reset, a->v);
    a->dv = gfs_temporary_variable (domain);
    for (a->dv->component = 0; a->dv->component < FTT_DIMENSION; a->dv->component++) {
      gfs_domain_cell_traverse (domain,
				FTT_PRE_ORDER, FTT_TRAVERSE_ALL, -1,
				(FttCellTraverseFunc) compute_gradient, a);
      gfs_domain_bc (domain, FTT_TRAVERSE_ALL, -1, a->dv);
      gfs_domain_cell_traverse (domain,
				FTT_PRE_ORDER, FTT_TRAVERSE_ALL, -1,
				(FttCellTraverseFunc) add_hessian_norm, a);
    }
    gts_object_destroy (GTS_OBJECT (a->dv));
    return TRUE;
  }
  return FALSE;
}

static void gfs_adapt_error_class_init (GfsEventClass * klass)
{
  GTS_OBJECT_CLASS (klass)->destroy = gfs_adapt_error_destroy;
  GTS_OBJECT_CLASS (klass)->read = gfs_adapt_error_read;
  GFS_EVENT_CLASS (klass)->event = gfs_adapt_error_event;
}

static gdouble cost_error (FttCell * cell, GfsAdaptError * a)
{
  return sqrt (GFS_VALUE (cell, a->v))/8.*GFS_ADAPT_GRADIENT (a)->dimension;
}

static void gfs_adapt_error_init (GfsAdapt * object)
{
  object->cost = (GtsKeyFunc) cost_error;
  object->cfactor = 2.;
}

GfsEventClass * gfs_adapt_error_class (void)
{
  static GfsEventClass * klass = NULL;

  if (klass == NULL) {
    GtsObjectClassInfo gfs_adapt_error_info = {
      "GfsAdaptError",
      sizeof (GfsAdaptError),
      sizeof (GfsEventClass),
      (GtsObjectClassInitFunc) gfs_adapt_error_class_init,
      (GtsObjectInitFunc) gfs_adapt_error_init,
      (GtsArgSetFunc) NULL,
      (GtsArgGetFunc) NULL
    };
    klass = gts_object_class_new (GTS_OBJECT_CLASS (gfs_adapt_gradient_class ()),
				  &gfs_adapt_error_info);
  }

  return klass;
}

static void refine_cell_corner (FttCell * cell, GfsDomain * domain)
{
  if (ftt_refine_corner (cell))
    ftt_cell_refine_single (cell, domain->cell_init, domain->cell_init_data);
}

/**
 * @domain: a #GfsDomain.
 * @depth: the depth of @domain.
 *
 * Force the grading of the tree hierarchy of domain, matches the
 * boundaries, recomputes merged cells and applies the boundary
 * conditions for all variables.
 */
void gfs_domain_reshape (GfsDomain * domain, guint depth)
{
  gint l;

  g_return_if_fail (domain != NULL);

  for (l = depth - 2; l >= 0; l--)
    gfs_domain_cell_traverse (domain,
			      FTT_PRE_ORDER, FTT_TRAVERSE_LEVEL, l,
			      (FttCellTraverseFunc) refine_cell_corner,
			      domain);
  gfs_domain_match (domain);
  gfs_set_merged (domain);
  GSList * i = domain->variables;
  while (i) {
    gfs_domain_bc (domain, FTT_TRAVERSE_LEAFS, -1, i->data);
    i = i->next;
  }
}

#define CELL_COST(cell) (GFS_VARIABLE (cell, p->costv->i))
#define CELL_HCOARSE(c) (GFS_DOUBLE_TO_POINTER (GFS_VARIABLE (c, p->hcoarsev->i)))
#define CELL_HFINE(c) (GFS_DOUBLE_TO_POINTER (GFS_VARIABLE (c, p->hfinev->i)))

static FttCell * remove_top_coarse (GtsEHeap * h, gdouble * cost, GfsVariable * hcoarse)
{
  FttCell * cell = gts_eheap_remove_top (h, cost);

  if (cell)
    GFS_VARIABLE (cell, hcoarse->i) = 0.;
  while (cell && !FTT_CELL_IS_LEAF (cell)) {
    cell = gts_eheap_remove_top (h, cost);
    if (cell) 
      GFS_VARIABLE (cell, hcoarse->i) = 0.;
  }
  return cell;
}

static FttCell * remove_top_fine (GtsEHeap * h, gdouble * cost, GfsVariable * hfine)
{
  FttCell * cell = gts_eheap_remove_top (h, cost);

  if (cell)
    GFS_VARIABLE (cell, hfine->i) = 0.;
  while (cell && ftt_cell_depth (cell) - ftt_cell_level (cell) != 1) {
    cell = gts_eheap_remove_top (h, cost);
    if (cell) 
      GFS_VARIABLE (cell, hfine->i) = 0.;
  }
  return cell;
}

static gdouble refine_cost (FttCell * cell, GfsSimulation * sim)
{
  GSList * i = sim->adapts->items;
  gdouble cost = 0.;

  while (i) {
    GfsAdapt * a = i->data;

    if (a->active && a->cost)
      cost += a->weight*(* a->cost) (cell, a);
    i = i->next;
  }

  return cost;
}

static void compute_cost (FttCell * cell, AdaptParams * p)
{
  gdouble cost = refine_cost (cell, p->sim);

  GFS_VARIABLE (cell, p->hcoarsev->i) = GFS_VARIABLE (cell, p->hfinev->i) = 0.;
  if (FTT_CELL_IS_LEAF (cell))
    CELL_COST (cell) = cost;
  else {
    FttCellChildren child;
    FttCellNeighbors n;
    guint i, level = ftt_cell_level (cell);
    FttCell * parent;
    gdouble cmax = 0.;

    ftt_cell_children (cell, &child);
    for (i = 0; i < FTT_CELLS; i++)
      if (child.c[i] && CELL_COST (child.c[i]) > cmax)
	cmax = CELL_COST (child.c[i]);
    if (cmax > cost) cost = cmax;
    if (cost > CELL_COST (cell)) CELL_COST (cell) = cost;

    ftt_cell_neighbors (cell, &n);
    for (i = 0; i < FTT_NEIGHBORS; i++)
      if (n.c[i] && ftt_cell_level (n.c[i]) == level && 
	  (parent = ftt_cell_parent (n.c[i])) &&
	  cmax > CELL_COST (parent))
	CELL_COST (parent) = cmax;
  }
  p->nc++;
}

static void store_cost (FttCell * cell, AdaptParams * p)
{
  GFS_VARIABLE (cell, p->c->i) = CELL_COST (cell);
}

static guint minlevel (FttCell * cell, GfsSimulation * sim)
{
  guint minlevel = 0;
  GSList * i = sim->adapts->items;

  while (i) {
    GfsAdapt * a = i->data;
    guint l;
    
    if (a->active && (l = gfs_function_value (a->minlevel, cell)) > minlevel)
      minlevel = l;
    i = i->next;
  }
  return minlevel;
}

static guint maxlevel (FttCell * cell, GfsSimulation * sim)
{
  GSList * i = sim->adapts->items;
  guint maxlevel = G_MAXINT;

  while (i) {
    GfsAdapt * a = i->data;
    guint l;

    if (a->active && (l = gfs_function_value (a->maxlevel, cell)) < maxlevel)
      maxlevel = l;
    i = i->next;
  }
  return maxlevel;
}

static void fill_heaps (FttCell * cell, AdaptParams * p)
{
  guint level = ftt_cell_level (cell);
  FttCell * parent = ftt_cell_parent (cell);
  
  if (level < maxlevel (cell, p->sim))
    GFS_DOUBLE_TO_POINTER (GFS_VARIABLE (cell, p->hcoarsev->i)) = 
      gts_eheap_insert_with_key (p->hcoarse, cell, - CELL_COST (cell));
  if (parent && !GFS_CELL_IS_PERMANENT (parent) && GFS_VARIABLE (parent, p->hfinev->i) == 0. &&
      level > minlevel (parent, p->sim))
    GFS_DOUBLE_TO_POINTER (GFS_VARIABLE (parent, p->hfinev->i)) = 
      gts_eheap_insert_with_key (p->hfine, parent, CELL_COST (parent));
}

static gboolean fine_cell_coarsenable (FttCell * cell, AdaptParams * p)
{
  if (GFS_CELL_IS_BOUNDARY (cell))
    return TRUE;
  if (GFS_CELL_IS_PERMANENT (cell))
    return FALSE;
  if (CELL_COST (cell) >= -p->clim)
    return FALSE;
  if (ftt_cell_level (cell) < minlevel (cell, p->sim))
    return FALSE;
  return TRUE;      
}

static void fine_cell_cleanup (FttCell * cell, AdaptParams * p)
{
  if (!GFS_CELL_IS_BOUNDARY (cell)) {
    gpointer o;

    p->nc--;
    if ((o = CELL_HCOARSE (cell)))
      gts_eheap_remove (p->hcoarse, o);
    if ((o = CELL_HFINE (cell)))
      gts_eheap_remove (p->hfine, o);
  }
  gfs_cell_cleanup (cell, GFS_DOMAIN (p->sim));
}

static void cell_fine_init (FttCell * cell, AdaptParams * p)
{
  FttCellChildren child;
  GfsDomain * domain = GFS_DOMAIN (p->sim);
  guint n;

  (* domain->cell_init) (cell, domain->cell_init_data);
  ftt_cell_children (cell, &child);
  for (n = 0; n < FTT_CELLS; n++)
    if (child.c[n])
      CELL_COST (child.c[n]) = G_MAXDOUBLE;
  if (!GFS_CELL_IS_BOUNDARY (cell))
    p->nc += FTT_CELLS;
}

static gboolean adapt_global (GfsSimulation * simulation,
			      guint * depth,
			      GfsAdaptStats * s,
			      guint mincells, guint maxcells,
			      GfsVariable * c,
			      gdouble cmax)
{
  GfsDomain * domain = GFS_DOMAIN (simulation);
  gint l;
  gdouble ccoarse = 0., cfine = 0.;
  FttCell * coarse, * fine;
  gboolean changed = TRUE, global_changed = FALSE;
  AdaptParams apar;
  
  apar.sim = simulation;
  apar.nc = 0;
  apar.costv = gfs_temporary_variable (domain);
  apar.hcoarsev = gfs_temporary_variable (domain);
  apar.hfinev = gfs_temporary_variable (domain);
  apar.hcoarse = gts_eheap_new (NULL, NULL);
  apar.hfine = gts_eheap_new (NULL, NULL);
  apar.c = c;
  
  gfs_domain_cell_traverse (domain, 
			    FTT_POST_ORDER, FTT_TRAVERSE_NON_LEAFS, -1,
			    (FttCellTraverseFunc) gfs_cell_reset, apar.costv);
  for (l = *depth; l >= 0; l--)
    gfs_domain_cell_traverse (domain, 
			      FTT_PRE_ORDER, FTT_TRAVERSE_LEVEL, l,
			      (FttCellTraverseFunc) compute_cost, &apar);
  if (apar.c)
    gfs_domain_cell_traverse (domain, 
			      FTT_PRE_ORDER, FTT_TRAVERSE_ALL, -1,
			      (FttCellTraverseFunc) store_cost, &apar);
  gts_eheap_freeze (apar.hcoarse);
  gts_eheap_freeze (apar.hfine);
  gfs_domain_cell_traverse (domain, FTT_PRE_ORDER, FTT_TRAVERSE_LEAFS, -1,
			    (FttCellTraverseFunc) fill_heaps, &apar);
  gts_eheap_thaw (apar.hcoarse);
  gts_eheap_thaw (apar.hfine);
  coarse = remove_top_coarse (apar.hcoarse, &ccoarse, apar.hcoarsev);
  fine = remove_top_fine (apar.hfine, &cfine, apar.hfinev);
#ifdef DEBUG
  fprintf (stderr, "initial: %g %g %d\n", cfine, -ccoarse, apar.nc);
#endif /* DEBUG */
  while (changed) {
#ifdef DEBUG
    fprintf (stderr, "%g %g %d\n", cfine, -ccoarse, apar.nc);
#endif /* DEBUG */
    changed = FALSE;
    if (fine && ((cfine < -ccoarse && apar.nc > maxcells) || 
		 (cfine < cmax && apar.nc >= mincells))) {
      guint n = apar.nc;
	
      apar.clim = MIN (ccoarse, -cmax);
      ftt_cell_coarsen (fine,
			(FttCellCoarsenFunc) fine_cell_coarsenable, &apar,
			(FttCellCleanupFunc) fine_cell_cleanup, &apar);
#ifdef DEBUG
      fprintf (stderr, "coarsen: %d\n", apar.nc);
#endif /* DEBUG */
      fine = remove_top_fine (apar.hfine, &cfine, apar.hfinev);
      s->removed += n - apar.nc;
      changed = global_changed = TRUE;
    }
    if (coarse && ((-ccoarse > cfine && apar.nc < mincells) ||
		   (-ccoarse > cmax && apar.nc <= maxcells))) {
      guint level = ftt_cell_level (coarse), n = apar.nc;
	
      ftt_cell_refine_corners (coarse, (FttCellInitFunc) cell_fine_init, &apar);
      ftt_cell_refine_single (coarse, (FttCellInitFunc) cell_fine_init, &apar);
      if (level + 1 > *depth)
	*depth = level + 1;
#ifdef DEBUG
      fprintf (stderr, "refine: %d\n", apar.nc);
#endif /* DEBUG */
      coarse = remove_top_coarse (apar.hcoarse, &ccoarse, apar.hcoarsev);
      s->created += apar.nc - n;
      changed = global_changed = TRUE;
    }
  }
  gts_range_add_value (&s->cmax, -ccoarse);
  gts_range_add_value (&s->ncells, apar.nc);

  gts_eheap_destroy (apar.hcoarse);
  gts_eheap_destroy (apar.hfine);
  gts_object_destroy (GTS_OBJECT (apar.costv));
  gts_object_destroy (GTS_OBJECT (apar.hcoarsev));
  gts_object_destroy (GTS_OBJECT (apar.hfinev));  

  return global_changed;
}

typedef struct {
  GfsSimulation * sim;
  guint depth, nc;
  GfsVariable * r, * c;
  GfsAdaptStats * s;
  gboolean changed;
} AdaptLocalParams;

#define REFINABLE(cell, p) (GFS_VARIABLE (cell, (p)->r->i))
#define COARSENABLE(cell, p) (GFS_VARIABLE (cell, (p)->c->i))

static gboolean coarsen_cell (FttCell * cell, AdaptLocalParams * p)
{
  if (GFS_CELL_IS_BOUNDARY (cell))
    return TRUE;
  return COARSENABLE (cell, p);
}

static void cell_cleanup (FttCell * cell, AdaptLocalParams * p)
{
  gfs_cell_cleanup (cell, GFS_DOMAIN (p->sim));
  p->s->removed++;
  p->nc--;
  p->changed = TRUE;
}

static void coarsen_box (GfsBox * box, AdaptLocalParams * p)
{
  ftt_cell_coarsen (box->root,
		    (FttCellCoarsenFunc) coarsen_cell, p,
		    (FttCellCleanupFunc) cell_cleanup, p);
}

static void local_cell_fine_init (FttCell * parent,  AdaptLocalParams * p)
{
  GfsDomain * domain = GFS_DOMAIN (p->sim);
  (* domain->cell_init) (parent, domain->cell_init_data);
  if (!GFS_CELL_IS_BOUNDARY (parent)) {
    p->s->created += FTT_CELLS;
    p->nc += FTT_CELLS;
  }
}

static void refine_cell (FttCell * cell, AdaptLocalParams * p)
{
  if (REFINABLE (cell, p)) {
    guint level = ftt_cell_level (cell);

    ftt_cell_refine_corners (cell, (FttCellInitFunc) local_cell_fine_init, p);
    ftt_cell_refine_single (cell, (FttCellInitFunc) local_cell_fine_init, p);
    if (level + 1 > p->depth)
      p->depth = level + 1;
    p->changed = TRUE;
  }
}

static void refine_cell_mark (FttCell * cell, AdaptLocalParams * p)
{
  p->nc++;
  REFINABLE (cell, p) = FALSE;
  COARSENABLE (cell, p) = !GFS_CELL_IS_PERMANENT (cell);

  guint level = ftt_cell_level (cell);
  GSList * i = p->sim->adapts->items;
  while (i) {
    GfsAdapt * a = i->data;
    if (a->active) {
      guint maxlevel = gfs_function_value (a->maxlevel, cell);
      if (FTT_CELL_IS_LEAF (cell) && level < maxlevel && (* a->cost) (cell, a) > a->cmax) {
	REFINABLE (cell, p) = TRUE;
	COARSENABLE (cell, p) = FALSE;
	return;
      }
      if (level < gfs_function_value (a->minlevel, cell) ||
	  (level < maxlevel && (* a->cost) (cell, a) > a->cmax/a->cfactor))
	COARSENABLE (cell, p) = FALSE;
    }
    i = i->next;
  }
  if (!FTT_CELL_IS_LEAF (cell)) {
    FttCell * parent = ftt_cell_parent (cell);    
    if (parent)
      COARSENABLE (parent, p) = FALSE;
  }
}

static gboolean adapt_local (GfsSimulation * sim, guint * depth, GfsAdaptStats * s)
{
  GfsDomain * domain = GFS_DOMAIN (sim);
  AdaptLocalParams p;
  p.sim = sim;
  p.depth = *depth;
  p.r = gfs_temporary_variable (domain);
  p.c = gfs_temporary_variable (domain);
  p.s = s;
  p.nc = 0;
  p.changed = FALSE;
  gfs_domain_cell_traverse (domain,
			    FTT_PRE_ORDER, FTT_TRAVERSE_ALL, -1,
			    (FttCellTraverseFunc) refine_cell_mark, &p);
  gfs_domain_cell_traverse (domain,
			    FTT_PRE_ORDER, FTT_TRAVERSE_LEAFS, -1,
			    (FttCellTraverseFunc) refine_cell, &p);
  gts_container_foreach (GTS_CONTAINER (domain), (GtsFunc) coarsen_box, &p);
  gts_object_destroy (GTS_OBJECT (p.r));
  gts_object_destroy (GTS_OBJECT (p.c));
  *depth = p.depth;

  gts_range_add_value (&s->ncells, p.nc);
  return p.changed;
}

/**
 * gfs_simulation_adapt:
 * @simulation: a #GfsSimulation.
 * @s: where to put statistics (or %NULL).
 *
 * Checks if any mesh adaptation is necessary and adapts the mesh
 * using an OR combination of all the regular criteria defined in
 * @simulation->adapts.
 * 
 * If any one or several criteria are defined as "not" refinements,
 * the mesh will be refined only if all of this criteria AND any other
 * regular criterion is verified.  
 */
void gfs_simulation_adapt (GfsSimulation * simulation)
{
  gboolean active = FALSE;
  guint mincells = 0, maxcells = G_MAXINT;
  GfsDomain * domain;
  gdouble cmax = 0.;
  GfsVariable * c = NULL;

  g_return_if_fail (simulation != NULL);

  domain = GFS_DOMAIN (simulation);

  gfs_domain_timer_start (domain, "adapt");

  GSList * i = simulation->adapts->items;
  while (i) {
    GfsAdapt * a = i->data;

    if (a->active) {
      if (a->maxcells < maxcells) maxcells = a->maxcells;
      if (a->mincells > mincells) mincells = a->mincells;
      cmax += a->cmax;
      active = TRUE;
      if (a->c)
	c = a->c;
    }
    i = i->next;
  }
  if (active) {
    guint depth = gfs_domain_depth (domain);
    gboolean changed;

    if (maxcells < G_MAXINT)
      changed = adapt_global (simulation, &depth, &simulation->adapts_stats, 
			      mincells, maxcells, c, cmax);
    else
      changed = adapt_local (simulation, &depth, &simulation->adapts_stats);

    gfs_all_reduce (domain, changed, MPI_INT, MPI_MAX);
    if (changed)
      gfs_domain_reshape (domain, depth);
  }

  gfs_domain_timer_stop (domain, "adapt");
}

/**
 * gfs_adapt_stats_init:
 * @s: the #GfsAdaptStats.
 *
 * Initializes or reset @s.
 */
void gfs_adapt_stats_init (GfsAdaptStats * s)
{
  g_return_if_fail (s != NULL);

  s->removed = 0;
  s->created = 0;
  gts_range_init (&s->cmax);
  gts_range_init (&s->ncells);
}

/**
 * gfs_adapt_stats_update:
 * @s: the #GfsAdaptStats.
 *
 * Updates @s.
 */
void gfs_adapt_stats_update (GfsAdaptStats * s)
{
  g_return_if_fail (s != NULL);

  gts_range_update (&s->cmax);
  gts_range_update (&s->ncells);
}
