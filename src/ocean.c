/* Gerris - The GNU Flow Solver
 * Copyright (C) 2004 St�phane Popinet
 * National Institute of Water and Atmospheric Research
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

#include "ocean.h"
#include "timestep.h"
#include "adaptive.h"
#include "source.h"
#include "vof.h"
#include "graphic.h"

/* GfsOcean: Object */

#if 1
static fixme(){}

GfsSimulationClass * gfs_ocean_class (void)
{
  static GfsSimulationClass * klass = NULL;

  if (klass == NULL) {
    GtsObjectClassInfo gfs_ocean_info = {
      "GfsOcean",
      sizeof (GfsOcean),
      sizeof (GfsSimulationClass),
      (GtsObjectClassInitFunc) NULL,
      (GtsObjectInitFunc) NULL,
      (GtsArgSetFunc) NULL,
      (GtsArgGetFunc) NULL
    };
    klass = gts_object_class_new (GTS_OBJECT_CLASS (gfs_simulation_class ()), &gfs_ocean_info);
  }

  return klass;
}
#else

static void ocean_destroy (GtsObject * object)
{
  guint i;
  GPtrArray * layer = GFS_OCEAN (object)->layer;

  for (i = 0; i < layer->len; i++) {
    GfsDomain * d = g_ptr_array_index (layer, i);

    d->variables = d->variables_io = NULL;
    gts_object_destroy (GTS_OBJECT (d));
  }
  g_ptr_array_free (layer, TRUE);

  (* GTS_OBJECT_CLASS (gfs_ocean_class ())->parent_class->destroy) (object);  
}

static void ocean_read (GtsObject ** object, GtsFile * fp)
{
  (* GTS_OBJECT_CLASS (gfs_ocean_class ())->parent_class->read) (object, fp);
  if (fp->type == GTS_ERROR)
    return;

  gfs_domain_add_variable (GFS_DOMAIN (*object), "PS");
  gfs_domain_add_variable (GFS_DOMAIN (*object), "Div");
}

static void new_layer (GfsOcean * ocean)
{
  GfsDomain * domain = GFS_DOMAIN (ocean);
  GfsDomain * d = GFS_DOMAIN (gts_object_new (GTS_OBJECT_CLASS (gfs_domain_class ())));
  
  d->rootlevel = domain->rootlevel;
  d->refpos = domain->refpos;
  d->lambda = domain->lambda;
  d->variables = domain->variables;
  d->variables_size = domain->variables_size;
  d->variables_io = domain->variables_io;
  g_ptr_array_add (ocean->layer, d);
}

static void add_layer (GfsBox * box, GfsDomain * domain)
{
#if FTT_2D
  gts_container_add (GTS_CONTAINER (g_ptr_array_index (GFS_OCEAN (domain)->layer, 0)),
		     GTS_CONTAINEE (box));
#else    /* 2D3 or 3D */
  if (box->neighbor[FTT_FRONT] == NULL || GFS_IS_BOUNDARY (box->neighbor[FTT_FRONT])) {
    GPtrArray * layer = GFS_OCEAN (domain)->layer;
    GtsObject * n;
    guint l = 0;

    gts_container_add (GTS_CONTAINER (g_ptr_array_index (layer, l++)), GTS_CONTAINEE (box));
    n = box->neighbor[FTT_BACK];
    while (GFS_IS_BOX (n)) {
      if (l == layer->len)
	new_layer (GFS_OCEAN (domain));
      gts_container_add (GTS_CONTAINER (g_ptr_array_index (layer, l++)), GTS_CONTAINEE (n));
      n = GFS_BOX (n)->neighbor[FTT_BACK];
    }
  }
#endif /* 2D3 or 3D */
}

static void ocean_post_read (GfsDomain * domain, GtsFile * fp)
{
  (* GFS_DOMAIN_CLASS (GTS_OBJECT_CLASS (gfs_ocean_class ())->parent_class)->post_read) 
    (domain, fp);

  gts_container_foreach (GTS_CONTAINER (domain), (GtsFunc) add_layer, domain);
  g_assert (GFS_OCEAN (domain)->layer->len > 0);
  GFS_OCEAN (domain)->toplayer = g_ptr_array_index (GFS_OCEAN (domain)->layer, 0);
}

#if (!FTT_2D) /* 2D3 or 3D */
static void sum_coeff (FttCell * cell)
{
  FttCell * c = ftt_cell_neighbor (cell, FTT_BACK);
  guint level = ftt_cell_level (cell);
  FttDirection d;

  while (c) {
    g_assert (FTT_CELL_IS_LEAF (c) && ftt_cell_level (c) == level);
    for (d = 0; d < FTT_NEIGHBORS_2D; d++) {
      g_assert (ftt_cell_neighbor (c, d) || GFS_STATE (c)->f[d].v == 0.);
      g_assert (!GFS_IS_MIXED (c) || GFS_STATE (c)->solid->s[d] != 0. ||
		GFS_STATE (c)->f[d].v == 0.);
      GFS_STATE (cell)->f[d].v += GFS_STATE (c)->f[d].v;
    }
    c = ftt_cell_neighbor (c, FTT_BACK);
  }
}

static void face_coeff_from_below (FttCell * cell)
{
  FttDirection d;
  GfsFaceStateVector * f = GFS_STATE (cell)->f;

  for (d = 0; d < FTT_NEIGHBORS_2D; d++) {
    FttCellChildren child;
    guint i, n;

    f[d].v = 0.;
    n = ftt_cell_children_direction (cell, d, &child);
    for (i = 0; i < n; i++)
      if (child.c[i])
	f[d].v += GFS_STATE (child.c[i])->f[d].v;
    f[d].v /= n;
  }
}

static void sum_divergence (FttCell * cell)
{
  FttCell * c = ftt_cell_neighbor (cell, FTT_BACK);
  guint level = ftt_cell_level (cell);

  while (c) {
    g_assert (FTT_CELL_IS_LEAF (c) && ftt_cell_level (c) == level);
    GFS_STATE (cell)->div += GFS_STATE (c)->div;
    c = ftt_cell_neighbor (c, FTT_BACK);
  }
}

static void column_pressure (FttCell * cell)
{
  FttCell * c = ftt_cell_neighbor (cell, FTT_BACK);
  guint level = ftt_cell_level (cell);

  while (c) {
    g_assert (FTT_CELL_IS_LEAF (c) && ftt_cell_level (c) == level);
    GFS_STATE (c)->p = GFS_STATE (cell)->p;
    c = ftt_cell_neighbor (c, FTT_BACK);
  }
}

static void compute_w (FttCell * c)
{
  guint level = ftt_cell_level (c);
  gdouble wf = 0., w = 0.;

  while (c) {
    GfsStateVector * s = GFS_STATE (c);

    g_assert (FTT_CELL_IS_LEAF (c) && ftt_cell_level (c) == level);
    s->f[FTT_BACK].un = w;
    wf += (s->f[FTT_LEFT].v*s->f[FTT_LEFT].un - s->f[FTT_RIGHT].v*s->f[FTT_RIGHT].un +
    	   s->f[FTT_BOTTOM].v*s->f[FTT_BOTTOM].un - s->f[FTT_TOP].v*s->f[FTT_TOP].un);
    if (GFS_IS_MIXED (c))
      s->f[FTT_FRONT].un = w = GFS_STATE (c)->solid->s[FTT_FRONT] > 0. ? 
	wf/GFS_STATE (c)->solid->s[FTT_FRONT] : 0.;
    else
      s->f[FTT_FRONT].un = w = wf;
    s->w = (s->f[FTT_BACK].un + s->f[FTT_FRONT].un)/2.;
    c = ftt_cell_neighbor (c, FTT_FRONT);
  }
}
#endif /* 2D3 or 3D */

static void reset_gradient (FttCell * cell)
{
  FttComponent c;

  for (c = 0; c < FTT_DIMENSION; c++)
    GFS_STATE (cell)->g[c] = 0.;
}

#define THETA 0.5

static void normal_divergence (FttCell * cell, GfsVariable * div)
{
  gfs_normal_divergence_2D (cell);
  GFS_STATE (cell)->div += (1. - THETA)*GFS_VARIABLE (cell, div->i)/THETA;
}

static void scale_divergence_helmoltz (FttCell * cell, gpointer * data)
{
  gdouble * dt = data[0];
  GfsVariable * p = data[1];
  gdouble * g = data[2];
  gdouble h = ftt_cell_size (cell);
  gdouble c = 2.*h*h/(THETA*(*g)*(*dt)*(*dt));

  if (GFS_IS_MIXED (cell))
    c *= GFS_STATE (cell)->solid->a;
  GFS_STATE (cell)->g[0] = c;
  GFS_STATE (cell)->div = 2.*GFS_STATE (cell)->div/(*dt) - c*GFS_VARIABLE (cell, p->i);
}

/**
 * gfs_free_surface_pressure:
 * @domain: a #GfsDomain.
 * @par: the multigrid paramaters.
 * @apar: the advection parameters.
 *
 */
static void gfs_free_surface_pressure (GfsDomain * domain,
				       GfsMultilevelParams * par,
				       GfsAdvectionParams * apar,
				       GfsVariable * ps,
				       GfsVariable * div,
				       gdouble g)
{
  guint minlevel, maxlevel;
  GfsDomain * toplayer;
  gpointer data[3];

  g_return_if_fail (domain != NULL);
  g_return_if_fail (par != NULL);
  g_return_if_fail (apar != NULL);
  g_return_if_fail (ps != NULL);
  g_return_if_fail (div != NULL);
  g_return_if_fail (g > 0.);

  toplayer = GFS_OCEAN (domain)->toplayer;
  apar->v = gfs_variable_from_name (domain->variables, "U");

  /* Initialize face coefficients */
#if (!FTT_2D) /* 2D3 or 3D */
  gfs_domain_cell_traverse (toplayer,
			    FTT_PRE_ORDER, FTT_TRAVERSE_LEAFS, -1,
			    (FttCellTraverseFunc) sum_coeff, NULL);
  gfs_domain_cell_traverse (toplayer,
			    FTT_POST_ORDER, FTT_TRAVERSE_NON_LEAFS, -1,
			    (FttCellTraverseFunc) face_coeff_from_below, NULL);
#endif /* 2D3 or 3D */

  /* compute MAC divergence */
  gfs_domain_cell_traverse (domain, FTT_PRE_ORDER, FTT_TRAVERSE_LEAFS, -1,
			    (FttCellTraverseFunc) normal_divergence, div);
#if (!FTT_2D)
  gfs_domain_cell_traverse (toplayer, FTT_PRE_ORDER, FTT_TRAVERSE_LEAFS, -1,
			    (FttCellTraverseFunc) sum_divergence, NULL);
#endif /* 2D3 or 3D */
  g /= GFS_OCEAN (domain)->layer->len;
  data[0] = &apar->dt;
  data[1] = ps;
  data[2] = &g;
  gfs_domain_cell_traverse (toplayer, FTT_PRE_ORDER, FTT_TRAVERSE_ALL, -1,
  			    (FttCellTraverseFunc) scale_divergence_helmoltz, data);
  
  /* solve for pressure */
  minlevel = toplayer->rootlevel;
  if (par->minlevel > minlevel)
    minlevel = par->minlevel;
  maxlevel = gfs_domain_depth (toplayer);
  gfs_residual (toplayer, 2, FTT_TRAVERSE_LEAFS, -1, gfs_p, gfs_div, gfs_res);
  par->residual_before = par->residual = 
    gfs_domain_norm_residual (toplayer, FTT_TRAVERSE_LEAFS, -1, apar->dt);
  par->niter = 0;
  while (par->residual.infty > par->tolerance && par->niter < par->nitermax) {
#if 0
    fprintf (stderr, "%d bias: %g first: %g second: %g infty: %g\n",
	     par->niter, 
	     par->residual.bias, 
	     par->residual.first, 
	     par->residual.second, 
	     par->residual.infty);
#endif
    gfs_poisson_cycle (toplayer, 2, minlevel, maxlevel, par->nrelax, gfs_p, gfs_div);
    par->residual = gfs_domain_norm_residual (toplayer, FTT_TRAVERSE_LEAFS, -1, apar->dt);
    par->niter++;
  }

  gfs_domain_cell_traverse (domain, FTT_PRE_ORDER, FTT_TRAVERSE_LEAFS, -1,
			    (FttCellTraverseFunc) reset_gradient, NULL);
#if (!FTT_2D)
  gfs_domain_cell_traverse (toplayer, FTT_PRE_ORDER, FTT_TRAVERSE_LEAFS, -1,
			    (FttCellTraverseFunc) column_pressure, NULL);
  gfs_poisson_coefficients (toplayer, NULL, 0.);
#endif /* 2D3 or 3D */
  gfs_correct_normal_velocities (domain, 2, gfs_p, apar->dt/2.);
#if (!FTT_2D)
  gfs_domain_cell_traverse_boundary (domain, FTT_BACK,
				     FTT_PRE_ORDER, FTT_TRAVERSE_LEAFS, -1,
  				     (FttCellTraverseFunc) compute_w, NULL);
#endif /* 2D3 or 3D */
  gfs_correct_centered_velocities (domain, 2, apar->dt/2.);
}

static void store_div (FttCell * cell, GfsVariable * div)
{
  GFS_VARIABLE (cell, div->i) = GFS_STATE (cell)->div;
}

static void gfs_free_surface_divergence (GfsDomain * domain, GfsVariable * div)
{
  GfsDomain * toplayer;

  g_return_if_fail (domain != NULL);
  g_return_if_fail (div != NULL);

  toplayer = GFS_OCEAN (domain)->toplayer;
  gfs_domain_face_traverse (domain, FTT_XY,
			    FTT_PRE_ORDER, FTT_TRAVERSE_LEAFS, -1,
			    (FttFaceTraverseFunc) gfs_face_reset_normal_velocity, NULL);
  gfs_domain_face_traverse (domain, FTT_XY,
			    FTT_PRE_ORDER, FTT_TRAVERSE_LEAFS, -1,
			    (FttFaceTraverseFunc) gfs_face_interpolated_normal_velocity, 
			    gfs_domain_velocity (domain));
  gfs_domain_cell_traverse (domain, FTT_PRE_ORDER, FTT_TRAVERSE_LEAFS, -1,
			    (FttCellTraverseFunc) gfs_normal_divergence_2D, NULL);
  gfs_domain_cell_traverse (domain, FTT_PRE_ORDER, FTT_TRAVERSE_LEAFS, -1,
			    (FttCellTraverseFunc) store_div, div);
}

static void save_p (FttCell * cell, GfsVariable * p1)
{
  GFS_VARIABLE (cell, p1->i) = GFS_STATE (cell)->p;
}

static void ocean_run (GfsSimulation * sim)
{
  GfsVariable * v, * ps, * div;
  GfsDomain * domain, * toplayer;

  domain = GFS_DOMAIN (sim);
  toplayer = GFS_OCEAN (sim)->toplayer;

  gfs_simulation_refine (sim);

  gts_container_foreach (GTS_CONTAINER (sim->events), (GtsFunc) gfs_event_init, sim);
  gts_container_foreach (GTS_CONTAINER (sim->adapts), (GtsFunc) gfs_event_init, sim);

  gfs_set_merged (domain);
  v = domain->variables;
  while (v) {
    gfs_event_init (GFS_EVENT (v), sim);
    gfs_domain_bc (domain, FTT_TRAVERSE_LEAFS, -1, v);
    v = v->next;
  }
  ps = gfs_variable_from_name (domain->variables, "PS");
  g_assert (ps);
  div = gfs_variable_from_name (domain->variables, "Div");
  g_assert (div);

  while (sim->time.t < sim->time.end &&
	 sim->time.i < sim->time.iend) {
    gdouble tstart;
    gboolean implicit;

    v = domain->variables;
    while (v) {
      gfs_event_do (GFS_EVENT (v), sim);
      v = v->next;
    }
    gfs_domain_cell_traverse (domain,
			      FTT_POST_ORDER, FTT_TRAVERSE_NON_LEAFS, -1,
			      (FttCellTraverseFunc) gfs_cell_coarse_init, domain);
    gts_container_foreach (GTS_CONTAINER (sim->events), (GtsFunc) gfs_event_do, sim);

    tstart = g_timer_elapsed (domain->timer, NULL);

    gfs_simulation_set_timestep (sim);

    gfs_domain_cell_traverse (domain, FTT_PRE_ORDER, FTT_TRAVERSE_LEAFS, -1,
			      (FttCellTraverseFunc) save_p, ps);
    gfs_domain_copy_bc (domain, FTT_TRAVERSE_LEAFS, -1, gfs_p, ps);
    gfs_free_surface_divergence (domain, div);

    gfs_predicted_face_velocities (domain, 2, &sim->advection_params);

    gfs_domain_timer_start (domain, "correct_normal_velocities");
    gfs_poisson_coefficients (domain, NULL, 0.);
    gfs_correct_normal_velocities (domain, 2, ps, sim->advection_params.dt/2.);
#if (!FTT_2D)
    gfs_domain_cell_traverse_boundary (domain, FTT_BACK,
				       FTT_PRE_ORDER, FTT_TRAVERSE_LEAFS, -1,
				       (FttCellTraverseFunc) compute_w, NULL);
#endif /* 2D3 or 3D */
    gfs_domain_timer_stop (domain, "correct_normal_velocities");

    v = domain->variables;
    while (v) {
      if (GFS_IS_VARIABLE_TRACER (v)) {
	GfsVariableTracer * t = GFS_VARIABLE_TRACER (v);

	t->advection.dt = sim->advection_params.dt;
	switch (t->advection.scheme) {
	case GFS_GODUNOV:
	  gfs_tracer_advection_diffusion (domain, &t->advection, &t->diffusion, NULL);
	  break;
	case GFS_VOF:
	  gfs_tracer_vof_advection (domain, &t->advection, NULL);
	  gfs_domain_variable_centered_sources (domain, v, v, t->advection.dt);
	  break;
	case GFS_NONE:
	  break;
	}
      }
      v = v->next;
    }

    gts_container_foreach (GTS_CONTAINER (sim->events), (GtsFunc) gfs_event_half_do, sim);

    gfs_centered_velocity_advection_diffusion (domain, 2,
					       &sim->advection_params,
					       &sim->diffusion_params);

    gfs_domain_timer_start (domain, "source_coriolis_implicit");
    implicit = gfs_source_coriolis_implicit (sim, &sim->advection_params, ps);
    gfs_domain_timer_stop (domain, "source_coriolis_implicit");

    gfs_domain_timer_start (domain, "free_surface_pressure");
    if (implicit)
      gfs_correct_centered_velocities (domain, 2, -sim->advection_params.dt/2.);
    else {
      gfs_poisson_coefficients (domain, NULL, 0.);
      gfs_correct_normal_velocities (domain, 2, ps, sim->advection_params.dt/2.);
      gfs_correct_centered_velocities (domain, 2, sim->advection_params.dt/2.);
    }

    gfs_domain_face_traverse (domain, FTT_XY,
			      FTT_PRE_ORDER, FTT_TRAVERSE_LEAFS, -1,
			      (FttFaceTraverseFunc) gfs_face_reset_normal_velocity, NULL);
    gfs_domain_face_traverse (domain, FTT_XY,
			      FTT_PRE_ORDER, FTT_TRAVERSE_LEAFS, -1,
			      (FttFaceTraverseFunc) gfs_face_interpolated_normal_velocity, 
			      gfs_domain_velocity (domain));
    gfs_free_surface_pressure (domain, &sim->approx_projection_params, &sim->advection_params,
			       ps, div, sim->physical_params.g);
    gfs_domain_timer_stop (domain, "free_surface_pressure");

    gfs_simulation_adapt (sim, NULL);

    sim->time.t = sim->tnext;
    sim->time.i++;

    gts_range_add_value (&domain->timestep, g_timer_elapsed (domain->timer, NULL) - tstart);
    gts_range_update (&domain->timestep);
    gts_range_add_value (&domain->size, gfs_domain_size (domain, FTT_TRAVERSE_LEAFS, -1));
    gts_range_update (&domain->size);
  }
  gts_container_foreach (GTS_CONTAINER (sim->events), (GtsFunc) gfs_event_do, sim);  
  gts_container_foreach (GTS_CONTAINER (sim->events),
			 (GtsFunc) gts_object_destroy, NULL);
}

static void gfs_ocean_class_init (GfsSimulationClass * klass)
{
  GTS_OBJECT_CLASS (klass)->destroy = ocean_destroy;
  GTS_OBJECT_CLASS (klass)->read = ocean_read;
  GFS_DOMAIN_CLASS (klass)->post_read = ocean_post_read;
  klass->run = ocean_run;
}

static void gfs_ocean_init (GfsOcean * object)
{
  object->layer = g_ptr_array_new ();
  new_layer (object);
}

GfsSimulationClass * gfs_ocean_class (void)
{
  static GfsSimulationClass * klass = NULL;

  if (klass == NULL) {
    GtsObjectClassInfo gfs_ocean_info = {
      "GfsOcean",
      sizeof (GfsOcean),
      sizeof (GfsSimulationClass),
      (GtsObjectClassInitFunc) gfs_ocean_class_init,
      (GtsObjectInitFunc) gfs_ocean_init,
      (GtsArgSetFunc) NULL,
      (GtsArgGetFunc) NULL
    };
    klass = gts_object_class_new (GTS_OBJECT_CLASS (gfs_simulation_class ()), &gfs_ocean_info);
  }

  return klass;
}

#if (!FTT_2D) /* 2D3 or 3D */
static void hydrostatic_pressure (FttCell * cell, gpointer * data)
{
  GfsVariable * vp = data[0];
  GfsVariable * rho = data[1];
  gdouble * g = data[2];
  gdouble r = GFS_VARIABLE (cell, rho->i), p = (*g)*r/2., r1;
  FttCellFace f;
  
  GFS_VARIABLE (cell, vp->i) = p;
  f.cell = cell;
  f.d = FTT_BACK;
  f.neighbor = ftt_cell_neighbor (f.cell, f.d);
  while (f.neighbor) {
    g_assert (ftt_face_type (&f) == FTT_FINE_FINE);
    r1 = gfs_face_interpolated_value (&f, rho->i);
    //    g_assert (r1 >= r);
    r = r1;
    GFS_VARIABLE (f.neighbor, vp->i) = p = p + (*g)*r;
    f.cell = f.neighbor;
    f.neighbor = ftt_cell_neighbor (f.cell, f.d);
  }
}
#endif /* 2D3 or 3D */

/**
 * gfs_hydrostatic_pressure:
 * @domain: a #GfsDomain.
 * @p: the hydrostatic pressure.
 * @rho: the density.
 * @g: the acceleration.
 *
 * Computes the hydrostatic pressure @p in @domain using the density
 * @rho.
 */
void gfs_hydrostatic_pressure (GfsDomain * domain,
			       GfsVariable * p,
			       GfsVariable * rho,
			       gdouble g)
{
#if FTT_2D
  g_return_if_fail (domain != NULL);
  g_return_if_fail (p != NULL);

  gfs_domain_cell_traverse (domain,
			    FTT_PRE_ORDER, FTT_TRAVERSE_LEAFS, -1,
			    (FttCellTraverseFunc) gfs_cell_reset, p);
#else /* 2D3 or 3D */
  gpointer data[3];

  g_return_if_fail (domain != NULL);
  g_return_if_fail (p != NULL);
  g_return_if_fail (rho != NULL);
  g_return_if_fail (g >= 0.);

  g /= GFS_OCEAN (domain)->layer->len;
  data[0] = p;
  data[1] = rho;
  data[2] = &g;
  gfs_domain_cell_traverse_boundary (domain, FTT_FRONT,
				     FTT_PRE_ORDER, FTT_TRAVERSE_LEAFS, -1,
				     (FttCellTraverseFunc) hydrostatic_pressure, data);
#endif /* 2D3 or 3D */
}

/* GfsSourceHydrostatic: Object */

static void gfs_source_hydrostatic_read (GtsObject ** o, GtsFile * fp)
{
  FttComponent c;
  GfsVariable * v;
  GfsDomain * domain = GFS_DOMAIN (gfs_object_simulation (*o));
  GfsSourceHydrostatic * sh;

  if (GTS_OBJECT_CLASS (gfs_source_hydrostatic_class ())->parent_class->read)
    (* GTS_OBJECT_CLASS (gfs_source_hydrostatic_class ())->parent_class->read) (o, fp);
  if (fp->type == GTS_ERROR)
    return;

  sh = GFS_SOURCE_HYDROSTATIC (*o);
  v = GFS_SOURCE_GENERIC (*o)->v->next;
  for (c = 1; c < 2; c++, v = v->next) {
    if (!v) {
      gts_file_error (fp, "not enough velocity components");
      return;
    }
    else {
      if (v->sources == NULL)
	v->sources = gts_container_new (GTS_CONTAINER_CLASS (gts_slist_container_class ()));
      gts_container_add (v->sources, GTS_CONTAINEE (*o));
    }
  }

  if (fp->type != GTS_STRING) {
    gts_file_error (fp, "expecting a string (rho)");
    return;
  }
  sh->rho = gfs_variable_from_name (domain->variables, fp->token->str);
  if (sh->rho == NULL) {
    gts_file_error (fp, "unknown variable `%s'", fp->token->str);
    return;
  }
  gts_file_next_token (fp);

  if (fp->type != GTS_STRING) {
    gts_file_error (fp, "expecting a string (ph)");
    return;
  }
  sh->ph = gfs_variable_from_name (domain->variables, fp->token->str);
  if (sh->ph == NULL)
    sh->ph = gfs_domain_add_variable (domain, fp->token->str);
  gts_file_next_token (fp);

  sh->ph1 = gfs_domain_add_variable (domain, NULL);
}

static void gfs_source_hydrostatic_write (GtsObject * o, FILE * fp)
{
  if (GTS_OBJECT_CLASS (gfs_source_hydrostatic_class ())->parent_class->write)
    (* GTS_OBJECT_CLASS (gfs_source_hydrostatic_class ())->parent_class->write) (o, fp);
  fprintf (fp, " %s %s",
	   GFS_SOURCE_HYDROSTATIC (o)->rho->name, 
	   GFS_SOURCE_HYDROSTATIC (o)->ph->name);
}

static gdouble gfs_source_hydrostatic_mac_value (GfsSourceGeneric * s,
						 FttCell * cell,
						 GfsVariable * v)
{
  return - gfs_center_gradient (cell, GFS_VELOCITY_COMPONENT (v->i),
				GFS_SOURCE_HYDROSTATIC (s)->ph1->i)/ftt_cell_size (cell);
}

static gdouble gfs_source_hydrostatic_centered_value (GfsSourceGeneric * s,
						      FttCell * cell,
						      GfsVariable * v)
{
  FttComponent c = GFS_VELOCITY_COMPONENT (v->i);
  GfsSourceHydrostatic * b = GFS_SOURCE_HYDROSTATIC (s);

  return - (gfs_center_gradient (cell, c, b->ph->i) + 
	    gfs_center_gradient (cell, c, b->ph1->i))/(2.*ftt_cell_size (cell));
}

static void copy_ph (FttCell * cell, GfsSourceHydrostatic * s)
{
  GFS_VARIABLE (cell, s->ph1->i) = GFS_VARIABLE (cell, s->ph->i);
}

static gboolean gfs_source_hydrostatic_event (GfsEvent * event, GfsSimulation * sim)
{
  if ((* GFS_EVENT_CLASS (GTS_OBJECT_CLASS (gfs_event_sum_class ())->parent_class)->event) 
      (event, sim)) {
    GfsSourceHydrostatic * s = GFS_SOURCE_HYDROSTATIC (event);

    if (s->not_first) {
      gfs_domain_cell_traverse (GFS_DOMAIN (sim), FTT_PRE_ORDER, FTT_TRAVERSE_LEAFS, -1,
				(FttCellTraverseFunc) copy_ph, s);
      gfs_domain_bc (GFS_DOMAIN (sim), FTT_TRAVERSE_LEAFS, -1, s->ph1);
    }
    else {
      gfs_hydrostatic_pressure (GFS_DOMAIN (sim), s->ph1, s->rho, sim->physical_params.g);
      gfs_domain_bc (GFS_DOMAIN (sim), FTT_TRAVERSE_LEAFS, -1, s->ph1);
      s->not_first = TRUE;
    }
    return TRUE;
  }
  return FALSE;
}

static void gfs_source_hydrostatic_event_half (GfsEvent * event, GfsSimulation * sim)
{
  GfsSourceHydrostatic * s = GFS_SOURCE_HYDROSTATIC (event);

  gfs_hydrostatic_pressure (GFS_DOMAIN (sim), s->ph, s->rho, sim->physical_params.g);
  gfs_domain_bc (GFS_DOMAIN (sim), FTT_TRAVERSE_LEAFS, -1, s->ph);
}

static void gfs_source_hydrostatic_class_init (GfsSourceGenericClass * klass)
{
  GTS_OBJECT_CLASS (klass)->read = gfs_source_hydrostatic_read;
  GTS_OBJECT_CLASS (klass)->write = gfs_source_hydrostatic_write;

  GFS_EVENT_CLASS (klass)->event = gfs_source_hydrostatic_event;
  GFS_EVENT_CLASS (klass)->event_half = gfs_source_hydrostatic_event_half;

  klass->mac_value = gfs_source_hydrostatic_mac_value;
  klass->centered_value = gfs_source_hydrostatic_centered_value;
}

GfsSourceGenericClass * gfs_source_hydrostatic_class (void)
{
  static GfsSourceGenericClass * klass = NULL;

  if (klass == NULL) {
    GtsObjectClassInfo gfs_source_hydrostatic_info = {
      "GfsSourceHydrostatic",
      sizeof (GfsSourceHydrostatic),
      sizeof (GfsSourceGenericClass),
      (GtsObjectClassInitFunc) gfs_source_hydrostatic_class_init,
      (GtsObjectInitFunc) NULL,
      (GtsArgSetFunc) NULL,
      (GtsArgGetFunc) NULL
    };
    klass = gts_object_class_new (GTS_OBJECT_CLASS (gfs_source_generic_class ()),
				  &gfs_source_hydrostatic_info);
  }

  return klass;
}


#endif
