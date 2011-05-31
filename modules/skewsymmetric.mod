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
#include "solid.h"



/* GfsSkewSymmetric: Header */

typedef struct _GfsSkewSymmetric              GfsSkewSymmetric;

struct _GfsSkewSymmetric {
  /*< private >*/
  GfsSimulation parent;

};

#define GFS_FACE_NORMAL_VALUE(fa)\
  (GFS_STATE ((fa)->cell)->f[(fa)->d].v)
#define GFS_FACE_NORMAL_VALUE_RIGHT(fa)\
  (GFS_STATE ((fa)->neighbor)->f[FTT_OPPOSITE_DIRECTION ((fa)->d)].v)

#define GFS_FACE_NORMAL_OLD_VELOCITY(fa)\
  (GFS_STATE ((fa)->cell)->f[(fa)->d].unold)
#define GFS_FACE_NORMAL_OLD_VELOCITY_RIGHT(fa)\
  (GFS_STATE ((fa)->neighbor)->f[FTT_OPPOSITE_DIRECTION ((fa)->d)].unold)

#define GFS_SKEW_SYMMETRIC(obj)            GTS_OBJECT_CAST (obj,		\
							 GfsSkewSymmetric,	\
							 gfs_skew_symmetric_class ())
#define GFS_IS_SKEW_SYMMETRIC(obj)         (gts_object_is_from_class (obj,	\
								   gfs_skew_symmetric_class ()))

GfsSimulationClass * gfs_skew_symmetric_class  (void);

typedef struct {
  GfsVariable **velfaces , **velold , **u, **veltmp; 
  GfsVariable *p;
  gdouble * dt; 
} FaceData;

typedef struct {
  GfsSourceDiffusion * d; 
  GfsFunction * alpha;
  FaceData * fd;
} DataDif;

/* GfsSkewSymmetric: Object */

static void gfs_skew_symmetric_run (GfsSimulation * sim);

static void gfs_skew_symmetric_class_init (GfsSimulationClass * klass) 
{
  klass->run =                        gfs_skew_symmetric_run;
}

GfsSimulationClass * gfs_skew_symmetric_class (void)
{
  static GfsSimulationClass * klass = NULL;

  if (klass == NULL) {
    GtsObjectClassInfo gfs_skew_symmetric_info = {
      "GfsSkewSymmetric",
      sizeof (GfsSkewSymmetric),
      sizeof (GfsSimulationClass),
      (GtsObjectClassInitFunc) gfs_skew_symmetric_class_init,
      (GtsObjectInitFunc) NULL,
      (GtsArgSetFunc) NULL,
      (GtsArgGetFunc) NULL
    };
    klass = gts_object_class_new (GTS_OBJECT_CLASS (gfs_simulation_class ()),
        &gfs_skew_symmetric_info);
  }

  return klass;
}

void get_face_values (FttCell * cell, FaceData * fd)
{
  g_return_if_fail (cell != NULL);

  GfsStateVector * s = GFS_STATE (cell);

  FttDirection d;
  for (d = 0; d < FTT_NEIGHBORS; d++) {
    FttComponent c = d/2;
    s->f[d].un   = 0.5*GFS_VALUE (cell, fd->u[c]);
    if (ftt_cell_neighbor (cell, d))
      s->f[d].un += 0.5*GFS_VALUE (ftt_cell_neighbor (cell, d), fd->u[c]);
    else
      s->f[d].un  = 0;
  }
}

static void initialize_unold (FttCell * cell, FaceData * fd)
{
  g_return_if_fail (cell != NULL);

  FttDirection d;
  for (d = 0; d < FTT_NEIGHBORS; d++) 
    GFS_VALUE (cell, fd->velold[d]) = GFS_VALUE (cell, fd->velfaces[d]);
}

static void get_velfaces (FttCell * cell, FaceData * fd)
{
  g_return_if_fail (cell != NULL);

  GfsStateVector * s = GFS_STATE (cell);
  FttDirection d;
  for (d = 0; d < FTT_NEIGHBORS; d++) 
    GFS_VALUE (cell, fd->velfaces[d]) = s->f[d].un;
}

static void get_cell_values (FttCell * cell, 
    FaceData * fd)
{
  g_return_if_fail (cell != NULL);

  FttComponent c;
  for (c = 0; c < FTT_DIMENSION; c++) 
    GFS_VALUE (cell, fd->u[c]) = 0.5*GFS_VALUE (cell, fd->velfaces[2*c])+0.5*GFS_VALUE (cell, fd->velfaces[2*c+1]);
}

static void advance_face_values (FttCell * cell, FaceData * fd)
{
  g_return_if_fail (cell != NULL);

  FttDirection d;
  for (d = 0; d < FTT_NEIGHBORS; d++) 
    GFS_STATE (cell)->f[d].un = 1.05*GFS_VALUE (cell, fd->velfaces[d])   - 0.05*GFS_VALUE (cell, fd->velold[d]);

}

static void save_values (FttCell * cell, FaceData * fd)
{
  g_return_if_fail (cell != NULL);

  GfsStateVector * s = GFS_STATE (cell);
  FttDirection d;
  for (d = 0; d < FTT_NEIGHBORS; d++) 
    GFS_VALUE (cell, fd->veltmp[d]) = GFS_VALUE (cell, fd->velfaces[d]);

}

static FttVector rpos[FTT_NEIGHBORS] = {
#if FTT_2D
  {1.,0.,0.}, {-1.,0.,0.}, {0.,1.,0.}, {0.,-1.,0.}
#else  /* FTT_3D */
  {1.,0.,0.}, {-1.,0.,0.}, {0.,1.,0.}, {0.,-1.,0.}, {0.,0.,1.}, {0.,0.,-1.}
#endif /* FTT_3D */
};

//d: direction of the face required
//d2: cell direction with respect to cellref
static gdouble interpolate_value_skew (FttCell * cellref,FttDirection d, FttDirection d2, FaceData * fd)
{
  guint i;
  FttCell * cell = ftt_cell_neighbor (cellref, d2);
  if (!cell) {
    g_warning ("warning: no bc implemented\n");
    return 0;
  }
  guint lref = ftt_cell_level (cellref);
  guint l    = ftt_cell_level (cell);
  if (l < lref) {
    FttVector posref,posinterp; 
    ftt_cell_pos (cellref, &posref);
    gdouble size = ftt_cell_size (cellref);
    posinterp.x = posref.x + size*(rpos[d2].x + rpos[d].x/2.);
    posinterp.y = posref.y + size*(rpos[d2].y + rpos[d].y/2.);
    posinterp.z = posref.z + size*(rpos[d2].z + rpos[d].z/2.);
    return gfs_interpolate (cell, posinterp, fd->u[d/2]);
  }
  if (!FTT_CELL_IS_LEAF (cell)) { 
    FttCellChildren child;
    guint n = ftt_cell_children_direction (cell, FTT_OPPOSITE_DIRECTION(d2), &child);
    gdouble vel = 0.;
    for (i = 0; i < n; i++)
      if (child.c[i])
        vel += GFS_VALUE (child.c[i],fd->velfaces[d]);
      return vel/n;
  }
  else 
    return GFS_VALUE (cell,fd->velfaces[d]);
}

//b Adaptative boolean
static gdouble traverse_advection (FttCell * cell, 
    FttComponent oc,
    FttDirection d,
    gdouble un,
    FaceData * fd,
    gboolean b)
{

  gdouble uauxbot, uauxtop;
  gdouble vn, vntop, vnbot, vndiag;

  if (!b) {
    vn     = GFS_VALUE (cell, fd->velfaces[2*oc]);
    vntop  = interpolate_value_skew (cell, 2*oc, d, fd);
    vndiag = interpolate_value_skew (cell, 2*oc+1, d, fd);
    vnbot  = GFS_VALUE (cell, fd->velfaces[2*oc+1]);
    uauxtop = interpolate_value_skew (cell, d, 2*oc, fd);
    uauxbot = interpolate_value_skew (cell, d, 2*oc+1, fd);
  } else {
    vn     = interpolate_value_skew (cell, 2*oc, FTT_OPPOSITE_DIRECTION(d), fd);
    vntop  = interpolate_value_skew (cell, 2*oc, FTT_OPPOSITE_DIRECTION(d), fd);
    vndiag = GFS_VALUE (cell, fd->velfaces[2*oc+1]);
    vnbot  = interpolate_value_skew (cell, 2*oc, FTT_OPPOSITE_DIRECTION(d), fd);
    uauxtop = interpolate_value_skew (cell, FTT_OPPOSITE_DIRECTION(d), 2*oc, fd);
    uauxbot = interpolate_value_skew (cell, FTT_OPPOSITE_DIRECTION(d), 2*oc+1, fd);

  }

  return 0.25*uauxtop*(vn + vntop) -  0.25*uauxbot*(vnbot + vndiag);
}

static void pressure_term (const FttCellFace * face, FaceData * fd)
{

  g_return_if_fail (face != NULL);

  gdouble pnext, pn;
  GfsStateVector * s = GFS_STATE (face->cell);

  if ((face->d % 2 ) != 0 ) {
    pn = GFS_VALUE (ftt_cell_neighbor(face->cell, face->d),fd->p);
    pnext = GFS_VALUE (face->cell,fd->p);
  }
  else { 
    pnext = GFS_VALUE (ftt_cell_neighbor(face->cell, face->d),fd->p);
    pn = GFS_VALUE (face->cell,fd->p);
  }

//  printf("pres %g %g %g \n", pn, pnext, fabs(pn-pnext));

  s->f[face->d].v -= 0.05*(pnext - pn);
}


static void advection_term (const FttCellFace * face, FaceData * fd)
{

  g_return_if_fail (face != NULL);

  gdouble un, unext, unprev;
  gdouble delta[2];

  GfsStateVector * s = GFS_STATE (face->cell);
  FttComponent c = face->d/2;
  FttDirection d;
  gboolean cond;

  un     = GFS_VALUE (face->cell,fd->velfaces[face->d]);
  if ((face->d % 2 ) != 0 ) {
    cond = TRUE;
    d = FTT_OPPOSITE_DIRECTION(face->d);
    unext  = GFS_VALUE (face->cell,fd->velfaces[FTT_OPPOSITE_DIRECTION(face->d)]);
    unprev = interpolate_value_skew (face->cell,face->d,face->d, fd); 
    delta[0] = ftt_cell_size(ftt_cell_neighbor(face->cell, face->d));
    delta[1] = ftt_cell_size (face->cell);
  }
  else { 
    cond = FALSE;
    d = face->d;
    unext = interpolate_value_skew (face->cell,face->d,face->d, fd);
    unprev = GFS_VALUE (face->cell, fd->velfaces[FTT_OPPOSITE_DIRECTION(face->d)]);
    delta[0] = delta[1] = ftt_cell_size(face->cell);
  }

  s->f[face->d].v = 0.25*(un + unext)*unext - 0.25*(un + unprev)*unprev;
#if FTT_2D
  s->f[face->d].v += traverse_advection (face->cell,FTT_ORTHOGONAL_COMPONENT (c),d,un,fd,cond);
#else  /* FTT_3D (probably: ckeck!!!*/
  static FttComponent orthogonal[FTT_DIMENSION][2] = {
    {FTT_Y, FTT_Z}, {FTT_X, FTT_Z}, {FTT_X, FTT_Y}
  };
  s->f[face->d].v += traverse_advection (face->cell,orthogonal[c][0],d,un,fd,cond);
  s->f[face->d].v += traverse_advection (face->cell,orthogonal[c][1],d,un,fd,cond); 
#endif
}



static void diffusion_term (FttCell * cell, DataDif * data)
{

  g_return_if_fail (cell != NULL);

  gdouble size = ftt_cell_size (cell); //I need to account for the metric
  gdouble un, unext, unprev,uauxbot, uauxtop;

  gdouble viscosity = 0.;  
  viscosity  = data->alpha ? gfs_function_value (data->alpha, cell) : 1.;
  viscosity *= gfs_diffusion_cell (data->d->D, cell);

  GfsStateVector * s = GFS_STATE (cell);

  FttComponent c;

  FttDirection d;
  /*I just go for d%2=0 (I regularize flux through faces later*/
  for (c = 0; c < FTT_DIMENSION; c++) {
    d = c*2;
    FttComponent oc = FTT_ORTHOGONAL_COMPONENT(c);
    if (ftt_cell_neighbor (cell, 2*c)) {
      FttCell * cell_next = ftt_cell_neighbor (cell, 2*c);
      if (!FTT_CELL_IS_LEAF (cell_next)) {
        FttCellChildren child;
        gint i,n = ftt_cell_children_direction (cell_next, FTT_OPPOSITE_DIRECTION(d), &child);
        for (i = 0; i < n; i++)
          if (child.c[i]) {

            un     = GFS_VALUE (child.c[i],data->fd->velfaces[FTT_OPPOSITE_DIRECTION(d)]);
            unext  = GFS_VALUE (child.c[i],data->fd->velfaces[d]);
            unprev = interpolate_value_skew (child.c[i],FTT_OPPOSITE_DIRECTION(d),FTT_OPPOSITE_DIRECTION(d), data->fd); 
            uauxtop = interpolate_value_skew (child.c[i], FTT_OPPOSITE_DIRECTION(d), 2*oc, data->fd);
            uauxbot = interpolate_value_skew (child.c[i], FTT_OPPOSITE_DIRECTION(d), 2*oc+1, data->fd);

            s->f[d].v -= viscosity*((unext - un)/size - (un - unprev)/size);
            s->f[d].v -= viscosity*((uauxtop - un)/size - (un - uauxbot)/size); 
          }
      }
      else {
        un = GFS_VALUE (cell, data->fd->velfaces[d]);
        unext = interpolate_value_skew (cell,d,d, data->fd);
        unprev = GFS_VALUE (cell, data->fd->velfaces[2*c+1]);
        uauxtop = interpolate_value_skew (cell, d, 2*oc, data->fd);
        uauxbot = interpolate_value_skew (cell, d, 2*oc+1, data->fd);

        s->f[d].v -= viscosity*((unext - un)/size - (un - unprev)/size);
        s->f[d].v -= viscosity*((uauxtop - un)/size - (un - uauxbot)/size); 
      } 
    }
  }  
}

static void update_vel (FttCell * cell, FaceData * fd)
{
  g_return_if_fail (cell != NULL);

  GfsStateVector * s = GFS_STATE (cell);
  gdouble size;

  FttComponent d;
  for (d = 0; d < FTT_NEIGHBORS; d++) {
    size = ftt_cell_size (cell);
    s->f[d].un = (0.10*GFS_VALUE (cell, fd->veltmp[d]) + 0.45*GFS_VALUE (cell, fd->velold[d])- s->f[d].v*(*fd->dt)/size)/0.55;
    GFS_VALUE (cell, fd->velold[d]) = GFS_VALUE (cell, fd->veltmp[d]);
  }
}

/* Same as in source.c used here to obtained viscosity */
static GfsSourceDiffusion * source_diffusion_viscosity (GfsVariable * v)
{
  if (v->sources) {
    GSList * i = GTS_SLIST_CONTAINER (v->sources)->items;

    while (i) {
      GtsObject * o = i->data;

      if (GFS_IS_SOURCE_DIFFUSION (o))
        return GFS_SOURCE_DIFFUSION (o);
      i = i->next;
    }
  }
  return NULL;
}

static void reset_face_velocity (const FttCellFace * face)
{                               
  g_return_if_fail (face != NULL);

  if (GFS_FACE_FRACTION_RIGHT (face) == 0.)
    return;

  if (ftt_face_type (face) == FTT_FINE_COARSE) 
    GFS_STATE (face->neighbor)->f[FTT_OPPOSITE_DIRECTION(face->d)].un = 0;
}

static void correct_face_velocity (const FttCellFace * face, GfsVariable * u0)
{                               
 gdouble u;

  g_return_if_fail (face != NULL);

  if (GFS_FACE_FRACTION_RIGHT (face) == 0.)
    return;

  u = GFS_STATE (face->cell)->f[face->d].un;

  switch (ftt_face_type (face)) {
  case FTT_FINE_FINE:
    GFS_STATE (face->neighbor)->f[FTT_OPPOSITE_DIRECTION(face->d)].un = u;
    break;
  case FTT_FINE_COARSE:
    GFS_STATE (face->neighbor)->f[FTT_OPPOSITE_DIRECTION(face->d)].un += 
      u*gfs_domain_face_fraction (u0->domain, face)/
      (gfs_domain_face_fraction_right (u0->domain, face)*FTT_CELLS_DIRECTION (face->d));
    break;
  default:
    g_assert_not_reached ();
  }
}

static void obtain_face_fluxes (const FttCell * cell)
{                               
  g_return_if_fail (cell != NULL);

  FttCellChildren child;
  GfsStateVector * s = GFS_STATE (cell);

  FttDirection d;
  for (d = 0; d < FTT_NEIGHBORS; d++) {
    FttCell * neighbor = ftt_cell_neighbor (cell, d);
    if (neighbor) {
      if (!FTT_CELL_IS_LEAF (neighbor)) {
        guint i, n = ftt_cell_children_direction (neighbor, FTT_OPPOSITE_DIRECTION(d), &child);
        s->f[d].v = 0;
        for (i = 0; i < n; i++)
          if (child.c[i])  
            s->f[d].v += GFS_STATE (child.c[i])->f[FTT_OPPOSITE_DIRECTION(d)].v;
        s->f[d].v /= n;
      }
      else if ((d % 2) > 0 && ftt_cell_level(cell) == ftt_cell_level(neighbor))
        s->f[d].v = GFS_STATE (neighbor)->f[FTT_OPPOSITE_DIRECTION(d)].v;
    }
  }
}

static void gfs_skew_symmetric_momentum (GfsSimulation * sim, FaceData * fd, GfsVariable **gmac)
{

  GfsDomain * domain = GFS_DOMAIN (sim);
  FttComponent c;
  FttDirection d;
  /*it is used for implementation of viscosity (improve?)*/
  GfsSourceDiffusion * dif = source_diffusion_viscosity (fd->u[0]);

  gfs_domain_cell_traverse (domain,
      FTT_PRE_ORDER, FTT_TRAVERSE_LEAFS, -1,
      (FttCellTraverseFunc) advance_face_values, fd);

  gfs_mac_projection (domain,
      &sim->projection_params, 
      &sim->advection_params,
      fd->p, sim->physical_params.alpha, gmac, NULL);

  gfs_domain_cell_traverse (domain, 
      FTT_PRE_ORDER, FTT_TRAVERSE_LEAFS, -1,
      (FttCellTraverseFunc) save_values, fd);

  gfs_domain_cell_traverse (domain, 
      FTT_PRE_ORDER, FTT_TRAVERSE_LEAFS, -1,
      (FttCellTraverseFunc) get_velfaces, fd);

  /*boundary conditions*/
  for (d = 0; d <  FTT_NEIGHBORS; d++)
    gfs_domain_bc (domain, FTT_TRAVERSE_LEAFS, -1, fd->velfaces[d]);

  gfs_domain_cell_traverse (domain, 
      FTT_PRE_ORDER, FTT_TRAVERSE_LEAFS, -1,
      (FttCellTraverseFunc) get_cell_values, fd);

  gfs_domain_face_traverse (domain, FTT_XYZ,
      FTT_PRE_ORDER, FTT_TRAVERSE_LEAFS, -1,
      (FttFaceTraverseFunc) advection_term, fd); 

  if (dif) { 
    DataDif dd = { dif , sim->physical_params.alpha, fd };
    gfs_domain_cell_traverse (domain,  
        FTT_PRE_ORDER, FTT_TRAVERSE_LEAFS, -1,
        (FttCellTraverseFunc) diffusion_term, &dd); 
  }

  for (d = 0; d <  FTT_NEIGHBORS; d++)
    gfs_domain_bc (domain, FTT_TRAVERSE_LEAFS, -1, fd->p);

  gfs_domain_face_traverse (domain, FTT_XYZ,
      FTT_PRE_ORDER, FTT_TRAVERSE_LEAFS, -1,
      (FttFaceTraverseFunc) pressure_term, fd); 

  /*regularize flux at faces*/
  for (c = 0; c <  FTT_DIMENSION; c++)
    gfs_domain_face_bc (domain, c, fd->u[c]);

  gfs_domain_cell_traverse (domain, 
      FTT_PRE_ORDER, FTT_TRAVERSE_LEAFS, -1,
      (FttCellTraverseFunc) obtain_face_fluxes, NULL);

  gfs_domain_cell_traverse (domain, 
      FTT_PRE_ORDER, FTT_TRAVERSE_LEAFS, -1,
      (FttCellTraverseFunc) update_vel, fd);

  gfs_domain_face_traverse (domain, FTT_XYZ,
                            FTT_PRE_ORDER, FTT_TRAVERSE_LEAFS, -1, 
                            (FttFaceTraverseFunc) reset_face_velocity, NULL);

  gfs_domain_face_traverse (domain, FTT_XYZ,
                            FTT_PRE_ORDER, FTT_TRAVERSE_LEAFS, -1, 
                            (FttFaceTraverseFunc) correct_face_velocity, fd->u[0]);


  /* source terms */
  /* check if sources*/
/*  for (c = 0; c <  FTT_DIMENSION; c++)
    gfs_domain_variable_centered_sources (domain, fd->u[c], fd->u[c], *(fd->dt));*/

}

static void gfs_skew_symmetric_run (GfsSimulation * sim)
{
  GfsVariable * p,  * res = NULL, * gmac[FTT_DIMENSION], * velfaces[FTT_NEIGHBORS], *velold[FTT_NEIGHBORS], *veltmp[FTT_NEIGHBORS];
  GfsDomain * domain;
  GSList * i;

  domain = GFS_DOMAIN (sim);

  p    = gfs_variable_from_name (domain->variables, "P");

  g_assert (p);
  FttComponent c;
  for (c = 0; c < FTT_DIMENSION; c++) 
    gmac[c] = gfs_temporary_variable (domain);

  gfs_variable_set_vector (gmac, FTT_DIMENSION);

  FttDirection d;
  for (d = 0; d < FTT_NEIGHBORS; d++) {
    velfaces[d] = gfs_temporary_variable (domain);
    velold[d]   = gfs_temporary_variable (domain);
    veltmp[d]   = gfs_temporary_variable (domain); 
  }

  gfs_simulation_refine (sim);
  gfs_simulation_init (sim);

  i = domain->variables;
  while (i) {
    if (GFS_IS_VARIABLE_RESIDUAL (i->data))
      res = i->data;
    i = i->next;
  }

  gfs_simulation_set_timestep (sim);

  GfsVariable **u = gfs_domain_velocity (domain);
  for (d = 0; d <  FTT_NEIGHBORS; d++)
    gfs_domain_bc (domain, FTT_TRAVERSE_LEAFS, -1, velfaces[d]);

  FaceData fd = { velfaces, velold, u, veltmp, p, &sim->advection_params.dt};

  if (sim->time.i == 0) {
    /* provisional solution to initialize the face velocities at t=0*/
    gfs_domain_cell_traverse (domain, 
        FTT_PRE_ORDER, FTT_TRAVERSE_LEAFS, -1,
        (FttCellTraverseFunc) get_face_values, &fd);
  
    gfs_mac_projection (domain,
        &sim->projection_params, 
        &sim->advection_params,
        p, sim->physical_params.alpha, gmac, NULL);
 
    gfs_domain_cell_traverse (domain, 
        FTT_PRE_ORDER, FTT_TRAVERSE_LEAFS, -1,
        (FttCellTraverseFunc) get_velfaces, &fd);

    for (d = 0; d <  FTT_NEIGHBORS; d++) 
      gfs_domain_bc (domain, FTT_TRAVERSE_LEAFS, -1, velfaces[d]);
    

    gfs_advance_tracers (domain, sim->advection_params.dt/2.);
  }

  /* initialize uold. WARNING!: If I restart the simulation, I should initialize properly */
  gfs_domain_cell_traverse (domain, 
      FTT_PRE_ORDER, FTT_TRAVERSE_LEAFS, -1,
      (FttCellTraverseFunc) initialize_unold, &fd);
      
  for (d = 0; d <  FTT_NEIGHBORS; d++) //required?
    gfs_domain_bc (domain, FTT_TRAVERSE_LEAFS, -1, velold[d]);


  while (sim->time.t < sim->time.end &&
      sim->time.i < sim->time.iend) {

    gdouble tstart = gfs_clock_elapsed (domain->timer);

    gfs_skew_symmetric_momentum (sim, &fd, gmac);


    sim->advection_params.dt = sim->advection_params.dt*1.05/0.55*2.; //the time step is divided by 2 in mac_projection
    gfs_mac_projection (domain,
        &sim->projection_params, 
        &sim->advection_params,
        p, sim->physical_params.alpha, gmac, NULL);
    sim->advection_params.dt = sim->advection_params.dt*0.55/1.05/2.;

    gfs_domain_cell_traverse (domain, 
        FTT_PRE_ORDER, FTT_TRAVERSE_LEAFS, -1,
        (FttCellTraverseFunc) get_velfaces, &fd);

    gfs_domain_cell_traverse (domain, 
      FTT_PRE_ORDER, FTT_TRAVERSE_LEAFS, -1,
      (FttCellTraverseFunc) get_cell_values, &fd);

    gts_container_foreach (GTS_CONTAINER (sim->events), (GtsFunc) gfs_event_do, sim);

    gfs_domain_cell_traverse (domain,
        FTT_POST_ORDER, FTT_TRAVERSE_NON_LEAFS, -1,
        (FttCellTraverseFunc) gfs_cell_coarse_init, domain);
    gfs_simulation_adapt (sim);

    sim->time.t = sim->tnext;
    sim->time.i++;

    gfs_simulation_set_timestep (sim);
    gfs_advance_tracers (domain, sim->advection_params.dt);

    gts_range_add_value (&domain->timestep, gfs_clock_elapsed (domain->timer) - tstart);
    gts_range_update (&domain->timestep);
    gts_range_add_value (&domain->size, gfs_domain_size (domain, FTT_TRAVERSE_LEAFS, -1));
    gts_range_update (&domain->size);
  }
  gts_container_foreach (GTS_CONTAINER (sim->events), (GtsFunc) gfs_event_do, sim);  
  gts_container_foreach (GTS_CONTAINER (sim->events), (GtsFunc) gts_object_destroy, NULL);

  for (c = 0; c < FTT_DIMENSION; c++) 
    gts_object_destroy (GTS_OBJECT (gmac[c]));
  

  for (d = 0; d < FTT_NEIGHBORS; d++){
    gts_object_destroy (GTS_OBJECT (velfaces[d]));
    gts_object_destroy (GTS_OBJECT (velold[d]));
    gts_object_destroy (GTS_OBJECT (veltmp[d]));
  }


}
/* Initialize module */

/* only define gfs_module_name for "official" modules (i.e. those installed in
   GFS_MODULES_DIR) */
const gchar gfs_module_name[] = "skewsymmetric";
const gchar * g_module_check_init (void);

const gchar * g_module_check_init (void)
{
  gfs_skew_symmetric_class ();
  return NULL;
} 
