/* Gerris - The GNU Flow Solver                       (-*-C-*-)
 * Copyright (C) 2010 Jose M. López-Herrera Sánchez
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

#include "simulation.h"
#include "source.h"
#include "adaptive.h"
#include "output.h"

/* GfsElectroHydro: Header */

typedef struct _GfsElectroHydro              GfsElectroHydro;

struct _GfsElectroHydro {
  /*< private >*/
  GfsSimulation parent;

  /*< public >*/
  GfsVariable * phi ;                             /* Electric potential */
  GfsVariable * E[FTT_DIMENSION] ;                /* Electric field; E=-Nabla Phi */
  GfsMultilevelParams electric_projection_params; /* Params for the electric potential */
  GfsVariable * rhoe ;                            /* volumetric charge density */
  GfsFunction * perm ;                            /* electric permittivity */
};

#define GFS_ELECTRO_HYDRO(obj)            GTS_OBJECT_CAST (obj,		\
							 GfsElectroHydro,	\
							 gfs_electro_hydro_class ())
#define GFS_IS_ELECTRO_HYDRO(obj)         (gts_object_is_from_class (obj,	\
								   gfs_electro_hydro_class ()))

GfsSimulationClass * gfs_electro_hydro_class  (void);

/* GfsElectroHydro: Object */

static gboolean strmatch (const gchar * s, const gchar * s1)
{
  gboolean m = !strcmp (s, s1);

  if (!m) {
    gchar * s2 = g_strconcat ("Gfs", s, NULL);
    m = !strcmp (s2, s1);
    g_free (s2);
  }
  return m;
}

static void gfs_electro_hydro_read (GtsObject ** o, GtsFile * fp)
{
  (* GTS_OBJECT_CLASS (gfs_electro_hydro_class ())->parent_class->read) (o, fp);
  if (fp->type == GTS_ERROR)
    return;

  GfsElectroHydro * elec = GFS_ELECTRO_HYDRO (*o);
  GfsSimulation * sim = GFS_SIMULATION (elec);

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

    if (!strcmp (fp->token->str, "perm")) {
      gts_file_next_token (fp);
      if (fp->type != '=')
	gts_file_error (fp, "expecting `='");
      else {
	gts_file_next_token (fp);
	gfs_function_read (elec->perm, sim, fp);
      }
    }

    /* ------------ GfsElectricProjectionParams ------------ */
    else if (strmatch (fp->token->str, "GfsElectricProjectionParams")) {
      gts_file_next_token (fp);
      gfs_multilevel_params_read (&elec->electric_projection_params, fp);
    }

    else
      gts_file_error (fp, "unknown keyword `%s'", fp->token->str);
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

static void gfs_electro_hydro_write (GtsObject * o, FILE * fp)
{
  (* GTS_OBJECT_CLASS (gfs_electro_hydro_class ())->parent_class->write) (o, fp);

  GfsElectroHydro * elect = GFS_ELECTRO_HYDRO (o);
  fputs (" {\n"
	 "  perm =", fp);
  gfs_function_write (elect->perm, fp);
  fputs ("\n"
	 "  GfsElectricProjectionParams ", fp);
  gfs_multilevel_params_write (&elect->electric_projection_params, fp);
  fputs ("\n"
	 "}", fp);
}

static void gfs_electro_hydro_destroy (GtsObject * object)
{
  gts_object_destroy (GTS_OBJECT (GFS_ELECTRO_HYDRO (object)->perm));
  
  (* GTS_OBJECT_CLASS (gfs_electro_hydro_class ())->parent_class->destroy) (object);
}

static void setting_E_from_phi (FttCellFace * f, GfsBc * b)
{
  if (b->v->component == f->d/2) {
    GfsVariable * phi = GFS_ELECTRO_HYDRO (gfs_object_simulation(b))->phi;
    GfsGradient g; 
    gfs_face_gradient (f, &g, phi->i, -1);
    double slope = (- g.b + g.a*GFS_VALUE (f->cell, phi))/ftt_cell_size (f->cell)
      *(FTT_FACE_DIRECT(f) ? 1 : -1);
    GFS_VALUE (f->cell, b->v) = - GFS_VALUE (f->neighbor, b->v) + 2.*slope;
  }
  else
    GFS_VALUE (f->cell, b->v) = GFS_VALUE (f->neighbor, b->v);
}

static void face_setting_E_from_phi(FttCellFace *f, GfsBc * b)
{
  if (b->v->component == f->d/2) {
    GfsVariable * phi = GFS_ELECTRO_HYDRO (gfs_object_simulation(b))->phi;
    GfsGradient g; 
    gfs_face_gradient (f, &g, phi->i, -1);
    double slope = (- g.b + g.a*GFS_VALUE (f->cell, phi))/ftt_cell_size (f->cell)
      *(FTT_FACE_DIRECT(f) ? 1 : -1);
    GFS_STATE (f->cell)->f[f->d].v = 
      GFS_STATE (f->neighbor)->f[FTT_OPPOSITE_DIRECTION (f->d)].v = slope;
  }
  else
    GFS_STATE (f->cell)->f[f->d].v = GFS_VALUE (f->neighbor, b->v);
}

static void gfs_electro_hydro_init (GfsElectroHydro * object)
{
  GfsDomain * domain = GFS_DOMAIN (object);
  static gchar name[][3] = {"Ex", "Ey", "Ez"};
  static gchar desc[][34] = {"x component of the electric field",
			     "y component of the electric field",
			     "z component of the electric field"};
  FttComponent c;  

  object->phi = gfs_domain_add_variable (domain, "Phi", "Electric potential");
  object->rhoe = gfs_variable_new (gfs_variable_tracer_class(), domain,
				   "Rhoe", "Volumetric charge density");
  object->rhoe->units = -3.;
  domain->variables = g_slist_append (domain->variables, object->rhoe);

  for (c = 0; c < FTT_DIMENSION; c++) {
    object->E[c] = gfs_domain_add_variable (domain , name[c], desc[c]);
    object->E[c]->units = -1.;
  }
  gfs_variable_set_vector (object->E, FTT_DIMENSION);

  gfs_multilevel_params_init (&object->electric_projection_params);
  object->perm = gfs_function_new (gfs_function_class (), 1.);
  gfs_function_set_units (object->perm, -1.);

  /* default BC for the electric field */
  for (c = 0; c < FTT_DIMENSION; c++) {
    GfsBc * bc = gfs_bc_new (gfs_bc_neumann_class(), object->E[c], FALSE);
    bc->bc      = (FttFaceTraverseFunc) setting_E_from_phi;
    bc->face_bc = (FttFaceTraverseFunc) face_setting_E_from_phi;
    gfs_variable_set_default_bc (object->E[c], bc);
  }
}

static void gfs_electro_hydro_run (GfsSimulation * sim);

static void gfs_electro_hydro_class_init (GfsSimulationClass * klass) 
{
  GTS_OBJECT_CLASS (klass)->destroy = gfs_electro_hydro_destroy;
  GTS_OBJECT_CLASS (klass)->read =    gfs_electro_hydro_read;
  GTS_OBJECT_CLASS (klass)->write =   gfs_electro_hydro_write;
  klass->run =                        gfs_electro_hydro_run;
}

GfsSimulationClass * gfs_electro_hydro_class (void)
{
  static GfsSimulationClass * klass = NULL;

  if (klass == NULL) {
    GtsObjectClassInfo gfs_electro_hydro_info = {
      "GfsElectroHydro",
      sizeof (GfsElectroHydro),
      sizeof (GfsSimulationClass),
      (GtsObjectClassInitFunc) gfs_electro_hydro_class_init,
      (GtsObjectInitFunc) gfs_electro_hydro_init,
      (GtsArgSetFunc) NULL,
      (GtsArgGetFunc) NULL
    };
    klass = gts_object_class_new (GTS_OBJECT_CLASS (gfs_simulation_class ()),
				  &gfs_electro_hydro_info);
  }

  return klass;
}

/* Setting div as - \int of rhoe on the cell volume */
static void rescale_div (FttCell * cell, gpointer * data)
{
  GfsVariable * divu = data[0];
  GfsVariable * div = data[1];
  gdouble size = ftt_cell_size (cell);

  GFS_VALUE (cell, div) = - GFS_VALUE (cell, divu)*size*size*
    gfs_domain_cell_fraction (div->domain, cell);
}

static void correct_div (GfsDomain * domain, GfsVariable * divu, GfsVariable * div)
{
  gpointer data[2];

  data[0] = divu;
  data[1] = div;

  gfs_domain_cell_traverse (domain, FTT_PRE_ORDER, FTT_TRAVERSE_LEAFS, -1,
			    (FttCellTraverseFunc) rescale_div, data);
}

/* Calculates -gradient of @v and write it in vector @g  */
static void minus_gradient (FttCell * cell, gpointer * data)
{
  GfsVariable * v = data[0];
  GfsVariable ** g = data[1];
  FttComponent c;
  gdouble size = ftt_cell_size (cell);

  for (c = 0; c < FTT_DIMENSION; c++)
    GFS_VALUE (cell, g[c]) = - gfs_center_gradient (cell, c, v->i)/size;
}

static void poisson_electric (GfsElectroHydro * elec)
{
  GfsMultilevelParams * par = &elec->electric_projection_params;
  GfsDomain * domain = GFS_DOMAIN (elec);
  GfsVariable * diae, * dive, * res1e;
  GfsVariable * phi = elec->phi; 
  GfsVariable ** e = elec->E;

  dive = gfs_temporary_variable (domain);
  correct_div (domain, elec->rhoe, dive);
  gfs_poisson_coefficients (domain, elec->perm);
  res1e = gfs_temporary_variable (domain);
  diae = gfs_temporary_variable (domain);
  gfs_domain_cell_traverse (domain, FTT_PRE_ORDER, FTT_TRAVERSE_ALL, -1,
			    (FttCellTraverseFunc) gfs_cell_reset, diae);
  par->poisson_solve (domain, par, phi, dive, res1e, diae, 1.);
  if (par->residual.infty > par->tolerance)
    g_warning ("poisson_electric: max residual %g > %g", par->residual.infty, par->tolerance);

  /* Set the electric field (-gradient of the potential) */
  gpointer data[2];
  data[0] = phi;
  data[1] = e;
  gfs_domain_cell_traverse (domain, FTT_PRE_ORDER, FTT_TRAVERSE_LEAFS, -1,
			    (FttCellTraverseFunc) minus_gradient, data);
  FttComponent c;
  for (c = 0; c < FTT_DIMENSION; c++)
    gfs_domain_bc (domain, FTT_TRAVERSE_LEAFS, -1, e[c]);

  gts_object_destroy (GTS_OBJECT (diae));
  gts_object_destroy (GTS_OBJECT (dive));
  gts_object_destroy (GTS_OBJECT (res1e));
}

static void gfs_electro_hydro_run (GfsSimulation * sim)
{
  GfsVariable * p, * pmac, * res = NULL, * g[FTT_DIMENSION], * gmac[FTT_DIMENSION];
  GfsVariable ** gc = sim->advection_params.gc ? g : NULL;
  GfsDomain * domain;
  GSList * i;
  GfsElectroHydro * elec;

  domain = GFS_DOMAIN (sim);
  elec = GFS_ELECTRO_HYDRO (sim) ;

  p = gfs_variable_from_name (domain->variables, "P");
  g_assert (p);
  pmac = gfs_variable_from_name (domain->variables, "Pmac");
  g_assert (pmac);

  FttComponent c;
  for (c = 0; c < FTT_DIMENSION; c++) {
    gmac[c] = gfs_temporary_variable (domain);
    if (sim->advection_params.gc) {
      g[c] = gfs_temporary_variable (domain);
    }
    else
      g[c] = gmac[c];
  }

  gfs_variable_set_vector (gmac, FTT_DIMENSION);
  gfs_variable_set_vector (g, FTT_DIMENSION);

  gfs_simulation_refine (sim);
  gfs_simulation_init (sim);

  i = domain->variables;
  while (i) {
    if (GFS_IS_VARIABLE_RESIDUAL (i->data))
      res = i->data;
    i = i->next;
  }

  gfs_simulation_set_timestep (sim);
  if (sim->time.i == 0) {
    gfs_approximate_projection (domain,
				&sim->approx_projection_params,
				&sim->advection_params,
				p, sim->physical_params.alpha, res, g, NULL);
    gfs_simulation_set_timestep (sim);
    gfs_advance_tracers (domain, sim->advection_params.dt/2.);
    poisson_electric (elec);
  }
  else if (sim->advection_params.gc)
    gfs_update_gradients (domain, p, sim->physical_params.alpha, g);

  while (sim->time.t < sim->time.end &&
	 sim->time.i < sim->time.iend) {
    gdouble tstart = gfs_clock_elapsed (domain->timer);

    gts_container_foreach (GTS_CONTAINER (sim->events), (GtsFunc) gfs_event_do, sim);

    gfs_predicted_face_velocities (domain, FTT_DIMENSION, &sim->advection_params);
    
    gfs_variables_swap (p, pmac);
    gfs_mac_projection (domain,
    			&sim->projection_params, 
    			&sim->advection_params,
			p, sim->physical_params.alpha, gmac, NULL);
    gfs_variables_swap (p, pmac);

    gts_container_foreach (GTS_CONTAINER (sim->events), (GtsFunc) gfs_event_half_do, sim);

    gfs_centered_velocity_advection_diffusion (domain,
					       FTT_DIMENSION,
					       &sim->advection_params,
					       gmac,
					       sim->time.i > 0 || !gc ? gc : gmac,
					       sim->physical_params.alpha);

    if (gc) {
      gfs_source_coriolis_implicit (domain, sim->advection_params.dt);
      gfs_correct_centered_velocities (domain, FTT_DIMENSION, sim->time.i > 0 ? gc : gmac, 
				       -sim->advection_params.dt);
    }
    else if (gfs_has_source_coriolis (domain)) {
      gfs_correct_centered_velocities (domain, FTT_DIMENSION, gmac, sim->advection_params.dt);
      gfs_source_coriolis_implicit (domain, sim->advection_params.dt);
      gfs_correct_centered_velocities (domain, FTT_DIMENSION, gmac, -sim->advection_params.dt);
    }

    gfs_domain_cell_traverse (domain,
			      FTT_POST_ORDER, FTT_TRAVERSE_NON_LEAFS, -1,
			      (FttCellTraverseFunc) gfs_cell_coarse_init, domain);
    gfs_simulation_adapt (sim);

    gfs_approximate_projection (domain,
   				&sim->approx_projection_params, 
    				&sim->advection_params, 
				p, sim->physical_params.alpha, res, g, NULL);

    sim->time.t = sim->tnext;
    sim->time.i++;

    gfs_simulation_set_timestep (sim);
    gfs_advance_tracers (domain, sim->advection_params.dt);
    poisson_electric (elec);

    gts_range_add_value (&domain->timestep, gfs_clock_elapsed (domain->timer) - tstart);
    gts_range_update (&domain->timestep);
    gts_range_add_value (&domain->size, gfs_domain_size (domain, FTT_TRAVERSE_LEAFS, -1));
    gts_range_update (&domain->size);
  }
  gts_container_foreach (GTS_CONTAINER (sim->events), (GtsFunc) gfs_event_do, sim);  
  gts_container_foreach (GTS_CONTAINER (sim->events), (GtsFunc) gts_object_destroy, NULL);

  for (c = 0; c < FTT_DIMENSION; c++) {
    gts_object_destroy (GTS_OBJECT (gmac[c]));
    if (sim->advection_params.gc)
      gts_object_destroy (GTS_OBJECT (g[c]));
  }
}

/* GfsSourceElectric: Header */

typedef struct _GfsSourceElectric         GfsSourceElectric;

struct _GfsSourceElectric {
  /*< private >*/
  GfsSourceVelocity parent;

  /*< public >*/
  GfsVariable * fe[FTT_DIMENSION];
};

#define GFS_SOURCE_ELECTRIC(obj)            GTS_OBJECT_CAST (obj,\
					         GfsSourceElectric,\
					         gfs_source_electric_class ())
#define GFS_IS_SOURCE_ELECTRIC(obj)         (gts_object_is_from_class (obj,\
						 gfs_source_electric_class ()))

GfsSourceGenericClass * gfs_source_electric_class  (void);

/* GfsSourceElectric: Object */

static void gfs_source_electric_destroy (GtsObject * o)
{
  FttComponent c;
  for (c = 0; c < FTT_DIMENSION; c++)
    if (GFS_SOURCE_ELECTRIC (o)->fe[c])
      gts_object_destroy (GTS_OBJECT (GFS_SOURCE_ELECTRIC (o)->fe[c]));
  
  (* GTS_OBJECT_CLASS (gfs_source_electric_class ())->parent_class->destroy) (o) ;
}

static void gfs_source_electric_read (GtsObject ** o, GtsFile * fp)
{
  (* GTS_OBJECT_CLASS (gfs_source_electric_class ())->parent_class->read) (o, fp);
  if (fp->type == GTS_ERROR)
    return;

  FttComponent c;
  for (c = 0 ; c < FTT_DIMENSION ; c++) {
    GfsVariable * v = GFS_SOURCE_VELOCITY (*o)->v[c];
    if (v->sources) {
      GSList * i = GTS_SLIST_CONTAINER (v->sources)->items;

      while (i) {
	if (i->data != *o && GFS_IS_SOURCE_ELECTRIC (i->data)) {
	  gts_file_error (fp, "variable '%s' cannot have multiple electric source terms", v->name);
	  return;
	}
	i = i->next;
      }
    }
  }

  GfsDomain * domain = GFS_DOMAIN (gfs_object_simulation (*o));
  for (c = 0; c < FTT_DIMENSION; c++)
    GFS_SOURCE_ELECTRIC (*o)->fe[c] = gfs_temporary_variable (domain);
}

static void save_fe (FttCell * cell, GfsSourceElectric * s)
{
  GfsElectroHydro * elec = GFS_ELECTRO_HYDRO (gfs_object_simulation (s));

  GfsFunction * perm = elec->perm;
  GfsVariable ** e = elec->E;
  GfsVariable * phi = elec->phi;

  FttComponent c;
  gdouble h = ftt_cell_size (cell);

  FttCellFace f;
  FttCellNeighbors n;
  gdouble fe[FTT_DIMENSION];

  for (c = 0; c < FTT_DIMENSION; c++)
    fe[c] = 0.;

  f.cell = cell;
  ftt_cell_neighbors (cell, &n);

  gdouble radc = gfs_domain_cell_fraction (GFS_DOMAIN (elec), cell);
  for (f.d = 0; f.d < FTT_NEIGHBORS; f.d++) {
    f.neighbor = n.c[f.d]; 
    gdouble permf = gfs_function_face_value (perm, &f);
    gdouble emod = 0.;
    GfsGradient g;
    gfs_face_gradient (&f, &g, phi->i, -1);
    gdouble en = (- g.b + g.a*GFS_VALUE (cell, phi))/h;
    gdouble sign = (FTT_FACE_DIRECT (&f) ? 1 : -1);

    gdouble radf = gfs_domain_face_fraction (GFS_DOMAIN (elec), &f);
    for (c = 0; c < FTT_DIMENSION; c++) {
      gdouble es = (c == f.d/2 ? sign*en : gfs_face_interpolated_value (&f, e[c]->i));
      emod += es*es; 
      fe[c] += permf*es*en*radf;
    }
    fe[f.d/2] -= sign*emod*permf*radc/2.;
  }

  /* fixme: we need to rescale, not entirely clear why... */
  gdouble scale = pow (GFS_SIMULATION (elec)->physical_params.L, -5.);
  for (c = 0; c < FTT_DIMENSION; c++)
    GFS_VALUE (cell, s->fe[c]) = scale*fe[c]/h/radc;
}

static gboolean gfs_source_electric_event (GfsEvent * event, GfsSimulation * sim)
{
  if ((* GFS_EVENT_CLASS (GTS_OBJECT_CLASS (gfs_source_electric_class ())->parent_class)->event)
      (event, sim)) {
    gfs_domain_cell_traverse (GFS_DOMAIN (sim), FTT_PRE_ORDER, FTT_TRAVERSE_LEAFS, -1,
			      (FttCellTraverseFunc) save_fe, event);
    return TRUE;
  }
  return FALSE;
}

static gdouble gfs_source_electric_centered_value (GfsSourceGeneric * s,
						   FttCell * cell,
						   GfsVariable * v)
{
  return GFS_VALUE (cell, GFS_SOURCE_ELECTRIC (s)->fe[v->component]);
}

static void gfs_source_electric_class_init (GfsSourceGenericClass * klass)
{
  GFS_EVENT_CLASS (klass)->event = gfs_source_electric_event;
  GTS_OBJECT_CLASS (klass)->read = gfs_source_electric_read;
  GTS_OBJECT_CLASS (klass)->destroy = gfs_source_electric_destroy;
}

static void gfs_source_electric_init (GfsSourceGeneric * s)
{
  s->mac_value = s->centered_value = gfs_source_electric_centered_value;
}

GfsSourceGenericClass * gfs_source_electric_class (void)
{
  static GfsSourceGenericClass * klass = NULL;

  if (klass == NULL) {
    GtsObjectClassInfo gfs_source_electric_info = {
      "GfsSourceElectric",
      sizeof (GfsSourceElectric),
      sizeof (GfsSourceGenericClass),
      (GtsObjectClassInitFunc) gfs_source_electric_class_init,
      (GtsObjectInitFunc) gfs_source_electric_init,
      (GtsArgSetFunc) NULL,
      (GtsArgGetFunc) NULL
    };
    klass = gts_object_class_new (GTS_OBJECT_CLASS (gfs_source_velocity_class ()),
				  &gfs_source_electric_info);
  }

  return klass;
}

/* GfsSourceCharge: Header */

typedef struct _GfsSourceCharge         GfsSourceCharge;

struct _GfsSourceCharge {
  /*< private >*/
  GfsSourceScalar parent;
  GfsVariable * c;

  /*< public >*/
  GfsFunction * conductivity;
};

#define GFS_SOURCE_CHARGE(obj)            GTS_OBJECT_CAST (obj,\
					         GfsSourceCharge,\
					         gfs_source_charge_class ())
#define GFS_IS_SOURCE_CHARGE(obj)         (gts_object_is_from_class (obj,\
						 gfs_source_charge_class ()))

GfsSourceGenericClass * gfs_source_charge_class  (void);

/* GfsSourceCharge: Object */

static void gfs_source_charge_destroy (GtsObject * o)
{
  GfsSourceCharge * s = GFS_SOURCE_CHARGE (o);
  if (s->conductivity)
    gts_object_destroy (GTS_OBJECT (s->conductivity));
  if (s->c)
    gts_object_destroy (GTS_OBJECT (s->c));

  (* GTS_OBJECT_CLASS (gfs_source_charge_class ())->parent_class->destroy) (o);
}

static void gfs_source_charge_read (GtsObject ** o, GtsFile * fp)
{
  (* GTS_OBJECT_CLASS (gfs_source_charge_class ())->parent_class->read) (o, fp);
  if (fp->type == GTS_ERROR)
    return;

  GfsSourceCharge * s = GFS_SOURCE_CHARGE (*o);
  s->conductivity = gfs_function_new (gfs_function_class(), 0.);
  gfs_function_set_units (s->conductivity, -1.);
  gfs_function_read (s->conductivity, gfs_object_simulation (s), fp);
  s->c = gfs_temporary_variable (GFS_DOMAIN (gfs_object_simulation (s)));
}

static void gfs_source_charge_write (GtsObject * o, FILE * fp)
{
  (* GTS_OBJECT_CLASS (gfs_source_charge_class ())->parent_class->write) (o, fp);
  gfs_function_write (GFS_SOURCE_CHARGE (o)->conductivity, fp);
}

static void source_charge (FttCell * cell, GfsSourceCharge * s)
{
  GfsDomain * domain = GFS_DOMAIN (gfs_object_simulation (s));
  GfsVariable * phi = GFS_ELECTRO_HYDRO (domain)->phi;
  gdouble value = 0., h = ftt_cell_size (cell);
  FttCellFace f;
  FttCellNeighbors n;

  f.cell = cell;
  ftt_cell_neighbors (cell, &n); 

  for (f.d = 0; f.d < FTT_NEIGHBORS; f.d++) {
    f.neighbor = n.c[f.d];
    GfsGradient g;
    gfs_face_weighted_gradient (&f, &g, phi->i, -1);
    value -= g.a*GFS_VALUE (cell, phi) - g.b;
  }
  GFS_VALUE (cell, s->c) = value/(h*h*gfs_domain_cell_fraction (domain, cell));
}

static void reset_coeff (FttCell * cell)
{
  FttDirection d;
  GfsFaceStateVector * f = GFS_STATE (cell)->f;
  
  for (d = 0; d < FTT_NEIGHBORS; d++)
    f[d].v = 0.;
}

typedef struct {
  gdouble lambda2[FTT_DIMENSION];
  GfsFunction * alpha;
  GfsDomain * domain;
} PoissonCoeff;

static void conduction_coeff (FttCellFace * face,
			   PoissonCoeff * p)
{
  gdouble alpha = gfs_function_face_value (p->alpha, face);
  gdouble v = p->lambda2[face->d/2]*alpha*gfs_domain_face_fraction (p->domain, face);

  GFS_STATE (face->cell)->f[face->d].v = v;
  
  switch (ftt_face_type (face)) {
  case FTT_FINE_FINE:
    GFS_STATE (face->neighbor)->f[FTT_OPPOSITE_DIRECTION (face->d)].v = v;
    break;
  case FTT_FINE_COARSE:
    GFS_STATE (face->neighbor)->f[FTT_OPPOSITE_DIRECTION (face->d)].v +=
      v/FTT_CELLS_DIRECTION (face->d);
    break;
  default:
    g_assert_not_reached ();
  }
}

static void face_coeff_from_below (FttCell * cell)
{
  FttDirection d;
  GfsFaceStateVector * f = GFS_STATE (cell)->f;
  guint neighbors = 0;

  for (d = 0; d < FTT_NEIGHBORS; d++) {
    FttCellChildren child;
    guint i, n;

    f[d].v = 0.;
    n = ftt_cell_children_direction (cell, d, &child);
    for (i = 0; i < n; i++)
      if (child.c[i])
	f[d].v += GFS_STATE (child.c[i])->f[d].v;
    f[d].v /= n;

    FttCell * neighbor;
    if (f[d].v > 0. && (neighbor = ftt_cell_neighbor (cell, d)) && !GFS_CELL_IS_BOUNDARY (neighbor))
      neighbors++;
  }

  if (neighbors == 1)
    for (d = 0; d < FTT_NEIGHBORS; d++)
      f[d].v = 0.;
}

/**
 * gfs_conduction_coefficients:
 * @domain: a #GfsDomain.
 * @alpha: the conductivity or %NULL.
 *
 * Initializes the face coefficients for the conduction term
 *
 */

void gfs_conduction_coefficients (GfsDomain * domain,
		  	          GfsFunction * alpha)
{
  PoissonCoeff p;
  FttComponent i;

  g_return_if_fail (domain != NULL);

  for (i = 0; i < FTT_DIMENSION; i++) {
    gdouble lambda = (&domain->lambda.x)[i];

    p.lambda2[i] = lambda*lambda;
  }
  gfs_domain_cell_traverse (domain,
			    FTT_PRE_ORDER, FTT_TRAVERSE_LEAFS, -1,
			    (FttCellTraverseFunc) reset_coeff, NULL);
  p.alpha = alpha;
  p.domain = domain;
  gfs_domain_face_traverse (domain, FTT_XYZ, 
			    FTT_PRE_ORDER, FTT_TRAVERSE_LEAFS, -1,
			    (FttFaceTraverseFunc) conduction_coeff, &p);
  gfs_domain_cell_traverse (domain,
			    FTT_POST_ORDER, FTT_TRAVERSE_NON_LEAFS, -1,
			    (FttCellTraverseFunc) face_coeff_from_below, NULL);
}



static gboolean gfs_source_charge_event (GfsEvent * event, GfsSimulation * sim)
{
  if ((* GFS_EVENT_CLASS (GTS_OBJECT_CLASS (gfs_source_charge_class ())->parent_class)->event)
      (event, sim)) {
    gfs_conduction_coefficients (GFS_DOMAIN (sim), GFS_SOURCE_CHARGE (event)->conductivity);
    gfs_domain_cell_traverse (GFS_DOMAIN (sim), FTT_PRE_ORDER, FTT_TRAVERSE_LEAFS, -1,
			      (FttCellTraverseFunc) source_charge, event);
    return TRUE;
  }
  return FALSE;
}

static void gfs_source_charge_class_init (GfsSourceGenericClass * klass)
{
  GTS_OBJECT_CLASS (klass)->read = gfs_source_charge_read;
  GTS_OBJECT_CLASS (klass)->write = gfs_source_charge_write;
  GTS_OBJECT_CLASS (klass)->destroy = gfs_source_charge_destroy;
  GFS_EVENT_CLASS (klass)->event = gfs_source_charge_event;
}

static gdouble source_charge_value (GfsSourceGeneric * s,
			            FttCell * cell,
				    GfsVariable * v)
{
  return GFS_VALUE (cell, GFS_SOURCE_CHARGE (s)->c);
}

static void gfs_source_charge_init (GfsSourceGeneric * s)
{
  s->mac_value = s->centered_value = source_charge_value;
}

GfsSourceGenericClass * gfs_source_charge_class (void)
{
  static GfsSourceGenericClass * klass = NULL;

  if (klass == NULL) {
    GtsObjectClassInfo gfs_source_charge_info = {
      "GfsSourceCharge",
      sizeof (GfsSourceCharge),
      sizeof (GfsSourceGenericClass),
      (GtsObjectClassInitFunc) gfs_source_charge_class_init,
      (GtsObjectInitFunc) gfs_source_charge_init,
      (GtsArgSetFunc) NULL,
      (GtsArgGetFunc) NULL
    };
    klass = gts_object_class_new (GTS_OBJECT_CLASS (gfs_source_scalar_class ()),
				  &gfs_source_charge_info);
  }

  return klass;
}

/* GfsElectroHydroAxi: Header */

GfsSimulationClass * gfs_electro_hydro_axi_class  (void);

/* GfsElectroHydroAxi: Object */

static void gfs_electro_hydro_axi_read (GtsObject ** o, GtsFile * fp)
{
  gfs_electro_hydro_read (o, fp);
  if (fp->type == GTS_ERROR)
    return;
  GFS_DOMAIN (*o)->refpos.y = 0.5;
}

static void gfs_electro_hydro_axi_class_init (GfsSimulationClass * klass) 
{
  GTS_OBJECT_CLASS (klass)->destroy = gfs_electro_hydro_destroy;
  GTS_OBJECT_CLASS (klass)->read =    gfs_electro_hydro_axi_read;
  GTS_OBJECT_CLASS (klass)->write =   gfs_electro_hydro_write;
  klass->run =                        gfs_electro_hydro_run;
}

GfsSimulationClass * gfs_electro_hydro_axi_class (void)
{
  static GfsSimulationClass * klass = NULL;

  if (klass == NULL) {
    GtsObjectClassInfo gfs_electro_hydro_axi_info = {
      "GfsElectroHydroAxi",
      sizeof (GfsElectroHydro),
      sizeof (GfsSimulationClass),
      (GtsObjectClassInitFunc) gfs_electro_hydro_axi_class_init,
      (GtsObjectInitFunc) gfs_electro_hydro_init,
      (GtsArgSetFunc) NULL,
      (GtsArgGetFunc) NULL
    };
    klass = gts_object_class_new (GTS_OBJECT_CLASS (gfs_axi_class ()),
				  &gfs_electro_hydro_axi_info);
  }

  return klass;
}

/* GfsOutputPotentialStats: Object */

static gboolean potential_stats_event (GfsEvent * event, GfsSimulation * sim)
{
  if ((* GFS_EVENT_CLASS (gfs_output_class())->event) (event, sim)) {
    GfsElectroHydro * elec = GFS_ELECTRO_HYDRO (sim);
    FILE * fp = GFS_OUTPUT (event)->file->fp;

    if (elec->electric_projection_params.niter > 0) {
      fprintf (fp, "Electric potential    before     after       rate\n");
      gfs_multilevel_params_stats_write (&elec->electric_projection_params, fp);
    }
    return TRUE;
  }
  return FALSE;
}

static void gfs_output_potential_stats_class_init (GfsEventClass * klass)
{
  klass->event = potential_stats_event;
}

GfsOutputClass * gfs_output_potential_stats_class (void);

GfsOutputClass * gfs_output_potential_stats_class (void)
{
  static GfsOutputClass * klass = NULL;

  if (klass == NULL) {
    GtsObjectClassInfo gfs_output_potential_stats_info = {
      "GfsOutputPotentialStats",
      sizeof (GfsOutput),
      sizeof (GfsOutputClass),
      (GtsObjectClassInitFunc) gfs_output_potential_stats_class_init,
      (GtsObjectInitFunc) NULL,
      (GtsArgSetFunc) NULL,
      (GtsArgGetFunc) NULL
    };
    klass = gts_object_class_new (GTS_OBJECT_CLASS (gfs_output_class ()),
				  &gfs_output_potential_stats_info);
  }

  return klass;
}

/* Initialize module */

/* only define gfs_module_name for "official" modules (i.e. those installed in
   GFS_MODULES_DIR) */
const gchar gfs_module_name[] = "electrohydro";
const gchar * g_module_check_init (void);
 
const gchar * g_module_check_init (void)
{
  gfs_electro_hydro_class ();
  gfs_electro_hydro_axi_class ();
  gfs_source_electric_class ();
  gfs_source_charge_class ();
  gfs_output_potential_stats_class ();
  return NULL;
} 
