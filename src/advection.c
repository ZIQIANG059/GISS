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
#include "advection.h"
#include "source.h"


#include "solid.h"

#if 1
static gdouble transverse_term (FttCell * cell,
				gdouble size,
				FttComponent c,
				const GfsAdvectionParams * par)
{
  GfsStateVector * s = GFS_STATE (cell);
  gdouble vtan = par->use_centered_velocity ? 
    GFS_VARIABLE (cell, GFS_VELOCITY_INDEX (c)) :
    (s->f[2*c].un + s->f[2*c + 1].un)/2.;
  FttCellFace f;
  GfsGradient gf;
  gdouble g;
  
  f.d = vtan > 0. ? 2*c + 1 : 2*c;
  f.cell = cell;
  f.neighbor = ftt_cell_neighbor (cell, f.d);
  gfs_face_gradient (&f, &gf, par->v->i, -1);
  g = gf.b - gf.a*GFS_VARIABLE (cell, par->v->i);
  if (vtan > 0.) g = - g;
  return par->dt*vtan*g/(2.*size);
}

/**
 * gfs_cell_advected_face_values:
 * @cell: a #FttCell.
 * @par: the advection parameters.
 *
 * Fills the face variable (@v field of #GfsFaceStateVector) of all the
 * faces of @cell with the advected value of variable @par->v at time
 * t + dt/2.
 */
void gfs_cell_advected_face_values (FttCell * cell,
				    const GfsAdvectionParams * par)
{
  FttComponent c;
  gdouble size;
  GfsStateVector * s;

  g_return_if_fail (cell != NULL);
  g_return_if_fail (par != NULL);

  s = GFS_STATE (cell);
  size = ftt_cell_size (cell);
  for (c = 0; c < FTT_DIMENSION; c++) {
    gdouble unorm = par->use_centered_velocity ?
      par->dt*GFS_VARIABLE (cell, GFS_VELOCITY_INDEX (c))/size :
      par->dt*(s->f[2*c].un + s->f[2*c + 1].un)/(2.*size);
    gdouble g = (* par->gradient) (cell, c, par->v->i);
    gdouble vl = GFS_VARIABLE (cell, par->v->i) + 
      MIN ((1. - unorm)/2., 0.5)*g;
    gdouble vr = GFS_VARIABLE (cell, par->v->i) + 
      MAX ((- 1. - unorm)/2., -0.5)*g;
    gdouble src = par->dt*gfs_variable_mac_source (par->v, cell)/2.;
    gdouble dv;

#if FTT_2D
    dv = transverse_term (cell, size, FTT_ORTHOGONAL_COMPONENT (c), par);
#else  /* FTT_3D */
    static FttComponent orthogonal[FTT_DIMENSION][2] = {
      {FTT_Y, FTT_Z}, {FTT_X, FTT_Z}, {FTT_X, FTT_Y}
    };

    dv =  transverse_term (cell, size, orthogonal[c][0], par);
    dv += transverse_term (cell, size, orthogonal[c][1], par);
#endif /* FTT_3D */

    s->f[2*c].v     = vl + src - dv;
    s->f[2*c + 1].v = vr + src - dv;
  }
}
#else
/**
 * gfs_cell_advected_face_values:
 * @cell: a #FttCell.
 * @par: the advection parameters.
 *
 * Fills the face variable (@v field of #GfsFaceStateVector) of all the
 * faces of @cell with the advected value of variable @par->v at time
 * t + dt/2.
 */
void gfs_cell_advected_face_values (FttCell * cell,
				    const GfsAdvectionParams * par)
{
  FttComponent c;
  gdouble size;
  gdouble g[FTT_DIMENSION];

  g_return_if_fail (cell != NULL);
  g_return_if_fail (par != NULL);

  size = ftt_cell_size (cell);
  for (c = 0; c < FTT_DIMENSION; c++)
    g[c] = (* par->gradient) (cell, c, par->v);

  for (c = 0; c < FTT_DIMENSION; c++) {
    gdouble u = par->dt*GFS_VARIABLE (cell, GFS_VELOCITY (c))/size;
    gdouble vl = GFS_VARIABLE (cell, par->v) + MIN ((1. - u)/2., 0.5)*g[c];
    gdouble vr = GFS_VARIABLE (cell, par->v) + MAX ((-1. - u)/2., -0.5)*g[c];
    gdouble dv;
    FttComponent cp;
#if FTT_2D

    cp = FTT_ORTHOGONAL_COMPONENT (c);
    dv = par->dt*GFS_VARIABLE (cell, GFS_VELOCITY (cp))*g[cp]/(2.*size);
#else  /* FTT_3D */
    static FttComponent orthogonal[FTT_DIMENSION][2] = {
      {FTT_Y, FTT_Z}, {FTT_X, FTT_Z}, {FTT_X, FTT_Y}
    };

    cp = orthogonal[c][0];
    dv = par->dt*GFS_VARIABLE (cell, GFS_VELOCITY (cp))*g[cp]/(2.*size);
    cp = orthogonal[c][1];
    dv += par->dt*GFS_VARIABLE (cell, GFS_VELOCITY (cp))*g[cp]/(2.*size);
#endif /* FTT_3D */

    cell->s.f[2*c].v     = vl - dv;
    cell->s.f[2*c + 1].v = vr - dv;
  }
}
#endif

/**
 * gfs_cell_non_advected_face_values:
 * @cell: a #FttCell.
 * @par: the (non)advection parameters.
 *
 * Fills the face variable (@v field of #GfsFaceStateVector) of all the
 * faces of @cell with the non-advected value of variable @par->v at time
 * t + dt/2.
 */
void gfs_cell_non_advected_face_values (FttCell * cell,
					const GfsAdvectionParams * par)
{
  FttComponent c;
  GfsStateVector * s;

  g_return_if_fail (cell != NULL);
  g_return_if_fail (par != NULL);

  s = GFS_STATE (cell);
  for (c = 0; c < FTT_DIMENSION; c++) {
    gdouble g = (* par->gradient) (cell, c, par->v->i);
    gdouble vl = GFS_VARIABLE (cell, par->v->i) + g/2.;
    gdouble vr = GFS_VARIABLE (cell, par->v->i) - g/2.;
    gdouble src = par->dt*gfs_variable_mac_source (par->v, cell)/2.;

    s->f[2*c].v     = vl + src;
    s->f[2*c + 1].v = vr + src;
  }
}

#if (FTT_2D || FTT_2D3)

static gdouble interpolate_1D1 (const FttCell * cell,
				FttDirection dright,
				FttDirection dup,
				gdouble x)
{
  FttCell * n;
  FttDirection dleft;
  GfsStateVector * s;

  g_return_val_if_fail (cell != NULL, 0.);

  dleft = FTT_OPPOSITE_DIRECTION (dright);
  n = ftt_cell_neighbor (cell, dup);
  s = GFS_STATE (cell);
  if (n && !GFS_CELL_IS_BOUNDARY (n)) {
    gdouble s1 = s->solid ? s->solid->s[dleft] : 1., s2;
    gdouble v1 = s->f[dleft].v, v2;

    g_assert (s1 > 0.);
    /* check for corner refinement violation (topology.fig) */
    g_assert (ftt_cell_level (n) == ftt_cell_level (cell));

    if (FTT_CELL_IS_LEAF (n)) {
      v2 = GFS_STATE (n)->f[dleft].v;
      s2 = GFS_IS_MIXED (n) ? GFS_STATE (n)->solid->s[dleft] : 1.;
    }
    else {
      FttDirection d[FTT_DIMENSION];

      d[0] = dleft;
      d[1] = FTT_OPPOSITE_DIRECTION (dup);
      n = ftt_cell_child_corner (n, d);
      if (n) {
	v2 = GFS_STATE (n)->f[dleft].v;
	s2 = GFS_IS_MIXED (n) ? GFS_STATE (n)->solid->s[dleft]/2. : 0.5;
      }
      else
	s2 = v2 = 0.;
    }
    return s2 > 0. ? (v2*(s1 - 1. + 2.*x) + v1*(s2 + 1. - 2.*x))/(s1 + s2) : v1;
  }
  return s->f[dleft].v;
}

#else /* FTT_3D */

static gdouble interpolate_2D1 (const FttCell * cell,
				FttDirection dright,
				FttDirection d1, FttDirection d2,
				gdouble x, gdouble y)
{
  FttCell * n1, * n2;
  gdouble x1 = 0., y1 = 1.;
  gdouble x2 = 1., y2 = 0.;
  gdouble v0, v1, v2;
  FttDirection dleft;

  g_return_val_if_fail (cell != NULL, 0.);
  g_return_val_if_fail (!GFS_IS_MIXED (cell), 0.);

  dleft = FTT_OPPOSITE_DIRECTION (dright);
  v0 = GFS_STATE (cell)->f[dleft].v;

  n1 = ftt_cell_neighbor (cell, d1);
  if (n1 && !GFS_CELL_IS_BOUNDARY (n1)) {
    /* check for corner refinement violation (topology.fig) */
    g_assert (ftt_cell_level (n1) == ftt_cell_level (cell));

    if (!FTT_CELL_IS_LEAF (n1)) {
      FttDirection d[FTT_DIMENSION];

      d[0] = FTT_OPPOSITE_DIRECTION (dright);
      d[1] = FTT_OPPOSITE_DIRECTION (d1);
      d[2] = d2;
      n1 = ftt_cell_child_corner (n1, d);
      /* check for mixed cell refinement violation */
      g_assert (n1);
      x1 = 1./4.;
      y1 = 3./4.;
    }
    v1 = GFS_STATE (n1)->f[dleft].v;
  }
  else
    v1 = v0;

  n2 = ftt_cell_neighbor (cell, d2);
  if (n2 && !GFS_CELL_IS_BOUNDARY (n2)) {
    /* check for corner refinement violation (topology.fig) */
    g_assert (ftt_cell_level (n2) == ftt_cell_level (cell));

    if (!FTT_CELL_IS_LEAF (n2)) {
      FttDirection d[FTT_DIMENSION];

      d[0] = FTT_OPPOSITE_DIRECTION (dright);
      d[1] = FTT_OPPOSITE_DIRECTION (d2);
      d[2] = d1;
      n2 = ftt_cell_child_corner (n2, d);
      /* check for mixed cell refinement violation */
      g_assert (n2);
      x2 = 3./4.;
      y2 = 1./4.;
    }
    v2 = GFS_STATE (n2)->f[dleft].v;
  }
  else
    v2 = v0;

  return ((v1 - v0)*(x*y2 - x2*y) + (v2 - v0)*(x1*y - x*y1))/
    (x1*y2 - x2*y1) + v0;
}

#endif /* FTT_3D */

/**
 * gfs_face_upwinded_value:
 * @face: a #FttCellFace.
 * @upwinding: type of upwinding.
 *
 * This function assumes that the face variable has been previously
 * defined using gfs_cell_advected_face_values().
 *
 * Returns: the upwinded value of the face variable.  
 */
gdouble gfs_face_upwinded_value (const FttCellFace * face,
				 GfsUpwinding upwinding)
{
  gdouble un = 0.;

  g_return_val_if_fail (face != NULL, 0.);

  if (GFS_FACE_FRACTION (face) == 0.)
    return 0.;

  switch (upwinding) {
  case GFS_CENTERED_UPWINDING:
    un = gfs_face_interpolated_value (face, GFS_VELOCITY_INDEX (face->d/2)); break;
  case GFS_FACE_UPWINDING:
    un = GFS_FACE_NORMAL_VELOCITY (face); break;
  case GFS_NO_UPWINDING:
    break;
  }
  if (!FTT_FACE_DIRECT (face))
    un = - un;

  switch (ftt_face_type (face)) {
  case FTT_FINE_FINE:
    return 
      un > 0. ? GFS_STATE (face->cell)->f[face->d].v :
      un < 0. ? GFS_STATE (face->neighbor)->f[FTT_OPPOSITE_DIRECTION (face->d)].v :
      (GFS_STATE (face->cell)->f[face->d].v +
       GFS_STATE (face->neighbor)->f[FTT_OPPOSITE_DIRECTION (face->d)].v)/2.;
  case FTT_FINE_COARSE:
    if (un > 0.)
      return GFS_STATE (face->cell)->f[face->d].v;
    else {
      gdouble vcoarse;
#if (FTT_2D || FTT_2D3)
      gint dp;
      static gint perpendicular[FTT_NEIGHBORS_2D][FTT_CELLS] = 
      {{-1,  2, -1,  3},
       { 2, -1,  3, -1},
       { 1,  0, -1, -1},
       {-1, -1,  1,  0}};
#else  /* FTT_3D */
      gint * dp;
      static gint perpendicular[FTT_NEIGHBORS][FTT_CELLS][2] = 
      {{{-1,-1},{2,4},{-1,-1},{3,4},{-1,-1},{2,5},{-1,-1},{3,5}},
       {{2,4},{-1,-1},{3,4},{-1,-1},{2,5},{-1,-1},{3,5},{-1,-1}},
       {{4,1},{4,0},{-1,-1},{-1,-1},{5,1},{5,0},{-1,-1},{-1,-1}},
       {{-1,-1},{-1,-1},{4,1},{4,0},{-1,-1},{-1,-1},{5,1},{5,0}},
       {{1,2},{0,2},{1,3},{0,3},{-1,-1},{-1,-1},{-1,-1},{-1,-1}},
       {{-1,-1},{-1,-1},{-1,-1},{-1,-1},{1,2},{0,2},{1,3},{0,3}}};
#endif /* FTT_3D */

#if FTT_2D3
      g_assert (face->d < FTT_NEIGHBORS_2D);
#endif

      dp = perpendicular[face->d][FTT_CELL_ID (face->cell)];
#if (FTT_2D || FTT_2D3)
      g_assert (dp >= 0);
      vcoarse = interpolate_1D1 (face->neighbor, face->d, dp, 1./4.);
#else  /* FTT_3D */
      g_assert (dp[0] >= 0 && dp[1] >= 0);
      vcoarse = interpolate_2D1 (face->neighbor, face->d,
				 dp[0], dp[1], 
				 1./4., 1./4.);
#endif /* FTT_3D */
      if (un == 0.)
	return (GFS_STATE (face->cell)->f[face->d].v + vcoarse)/2.;
      else
	return vcoarse;
    }
  default:
    g_assert_not_reached ();
  }
  return 0.;
}

/**
 * gfs_face_advection_flux:
 * @face: a #FttCellFace.
 * @par: the advection parameters.
 *
 * Adds to variable @par->fv, the value of the (conservative)
 * advection flux of the face variable through @face.
 *
 * This function assumes that the face variable has been previously
 * defined using gfs_cell_advected_face_values().
 */
void gfs_face_advection_flux (const FttCellFace * face,
			      const GfsAdvectionParams * par)
{
  gdouble flux;

  g_return_if_fail (face != NULL);
  g_return_if_fail (par != NULL);

  flux = GFS_FACE_FRACTION (face)*GFS_FACE_NORMAL_VELOCITY (face)*par->dt*
    gfs_face_upwinded_value (face, FALSE)/ftt_cell_size (face->cell);
  if (!FTT_FACE_DIRECT (face))
    flux = - flux;
  GFS_VARIABLE (face->cell, par->fv->i) -= flux;

  switch (ftt_face_type (face)) {
  case FTT_FINE_FINE:
    GFS_VARIABLE (face->neighbor, par->fv->i) += flux;
    break;
  case FTT_FINE_COARSE:
    GFS_VARIABLE (face->neighbor, par->fv->i) += flux/FTT_CELLS;
    break;
  default:
    g_assert_not_reached ();
  }
}

/**
 * gfs_face_velocity_advection_flux:
 * @face: a #FttCellFace.
 * @par: the advection parameters.
 *
 * Adds to variable @par->fv, the value of the (conservative)
 * advection flux through @face of variable @par->v (a component
 * of the velocity).
 *
 * This function assumes that the @g field of the cells sharing @face
 * are filled with the pressure gradient at time t + dt/2.  
 *
 * This function also assumes that the face value of @par->v has been
 * previously defined using gfs_cell_advected_face_values().  
 */
void gfs_face_velocity_advection_flux (const FttCellFace * face,
				       const GfsAdvectionParams * par)
{
  gdouble flux;
  FttComponent c;

  g_return_if_fail (face != NULL);
  g_return_if_fail (par != NULL);

  c = GFS_VELOCITY_COMPONENT (par->v->i);
  g_return_if_fail (c >= 0 && c < FTT_DIMENSION);

  flux = GFS_FACE_FRACTION (face)*GFS_FACE_NORMAL_VELOCITY (face)*par->dt
    /ftt_cell_size (face->cell);
#if 0
  if (c == face->d/2) /* normal component */
    flux *= GFS_FACE_NORMAL_VELOCITY (face);
  else /* tangential component */
#else
    flux *= gfs_face_upwinded_value (face, par->upwinding)
      /* pressure correction */
      - gfs_face_interpolated_value (face, GFS_GRADIENT_INDEX (c))*par->dt/2.;
#endif
  if (!FTT_FACE_DIRECT (face))
    flux = - flux;
  GFS_VARIABLE (face->cell, par->fv->i) -= flux;

  switch (ftt_face_type (face)) {
  case FTT_FINE_FINE:
    GFS_VARIABLE (face->neighbor, par->fv->i) += flux;
    break;
  case FTT_FINE_COARSE:
    GFS_VARIABLE (face->neighbor, par->fv->i) += flux/FTT_CELLS;
    break;
  default:
    g_assert_not_reached ();
  }
}

/**
 * gfs_face_velocity_convective_flux:
 * @face: a #FttCellFace.
 * @par: the advection parameters.
 *
 * Adds to variable @par->fv, the value of the (non-conservative)
 * convective flux through @face of variable @par->v (a component
 * of the velocity).
 *
 * This function assumes that the @g field of the cells sharing @face
 * are filled with the pressure gradient at time t + dt/2.  
 *
 * This function also assumes that the face value of @par->v has been
 * previously defined using gfs_cell_advected_face_values().  
 */
void gfs_face_velocity_convective_flux (const FttCellFace * face,
					const GfsAdvectionParams * par)
{
  gdouble u;
  FttComponent c;

  g_return_if_fail (face != NULL);
  g_return_if_fail (par != NULL);
  g_return_if_fail (GFS_FACE_FRACTION (face) == 1.);

  c = GFS_VELOCITY_COMPONENT (par->v->i);
  g_return_if_fail (c >= 0 && c < FTT_DIMENSION);

#if 0
  if (c == face->d/2) /* normal component */
    u = GFS_FACE_NORMAL_VELOCITY (face);
  else /* tangential component */
    u = gfs_face_upwinded_value (face, par->upwinding)
      /* pressure correction */
      - gfs_face_interpolated_value (face, GFS_GRADIENT_INDEX (c))*par->dt/2.;
#else
  u = gfs_face_upwinded_value (face, par->upwinding)
    /* pressure correction */
    - gfs_face_interpolated_value (face, GFS_GRADIENT_INDEX (c))*par->dt/2.;
#endif
  u *= par->dt/(2.*ftt_cell_size (face->cell));
  if (!FTT_FACE_DIRECT (face))
    u = - u;
  GFS_VARIABLE (face->cell, par->fv->i) -= 
    u*(GFS_STATE (face->cell)->f[face->d].un + 
       GFS_STATE (face->cell)->f[FTT_OPPOSITE_DIRECTION (face->d)].un);

  switch (ftt_face_type (face)) {
  case FTT_FINE_FINE:
    GFS_VARIABLE (face->neighbor, par->fv->i) += 
      u*(GFS_STATE (face->neighbor)->f[face->d].un + 
	 GFS_STATE (face->neighbor)->f[FTT_OPPOSITE_DIRECTION (face->d)].un);
    break;
  case FTT_FINE_COARSE:
    GFS_VARIABLE (face->neighbor, par->fv->i) += 
      u*(GFS_STATE (face->neighbor)->f[face->d].un + 
	 GFS_STATE (face->neighbor)->f[FTT_OPPOSITE_DIRECTION (face->d)].un)
      /FTT_CELLS;
    break;
  default:
    g_assert_not_reached ();
  }
}

/**
 * gfs_face_advected_normal_velocity:
 * @face: a #FttCellFace.
 * @upwinding: the type of upwinding.
 *
 * Fills the normal component of the velocity at @face with the value
 * advected (to time t + dt/2) from the centered velocities.
 *
 * This function assumes that the face variable has been previously
 * defined for the correct component of the velocity using
 * gfs_cell_advected_face_values().  
 */
void gfs_face_advected_normal_velocity (const FttCellFace * face,
					GfsUpwinding * upwinding)
{
  gdouble u;

  g_return_if_fail (face != NULL);

  if (GFS_FACE_FRACTION (face) == 0.)
    return;

  GFS_FACE_NORMAL_VELOCITY_LEFT (face) = u = gfs_face_upwinded_value (face, *upwinding);

  switch (ftt_face_type (face)) {
  case FTT_FINE_FINE:
    GFS_FACE_NORMAL_VELOCITY_RIGHT (face) = u;
    break;
  case FTT_FINE_COARSE:
    GFS_FACE_NORMAL_VELOCITY_RIGHT (face) += 
      u*GFS_FACE_FRACTION_LEFT (face)/(GFS_FACE_FRACTION_RIGHT (face)*
				       FTT_CELLS_DIRECTION (face->d));
    break;
  default:
    g_assert_not_reached ();
  }
}

/**
 * gfs_face_interpolated_normal_velocity:
 * @face: a #FttCellFace.
 *
 * Fills the normal component of the velocity at @face with the value
 * interpolated from the centered velocities.
 */
void gfs_face_interpolated_normal_velocity (const FttCellFace * face)
{
  gdouble u;

  g_return_if_fail (face != NULL);

  if (GFS_FACE_FRACTION (face) == 0.)
    return;

  GFS_FACE_NORMAL_VELOCITY_LEFT (face) = u = 
    gfs_face_interpolated_value (face, GFS_VELOCITY_INDEX (face->d/2));

  switch (ftt_face_type (face)) {
  case FTT_FINE_FINE:
    GFS_FACE_NORMAL_VELOCITY_RIGHT (face) = u;
    break;
  case FTT_FINE_COARSE:
    GFS_FACE_NORMAL_VELOCITY_RIGHT (face) += 
      u*GFS_FACE_FRACTION_LEFT (face)/(GFS_FACE_FRACTION_RIGHT (face)*
				       FTT_CELLS_DIRECTION (face->d));
    break;
  default:
    g_assert_not_reached ();
  }
}

/**
 * gfs_face_reset_normal_velocity:
 * @face: a #FttCellFace.
 *
 * Set velocity normal to @face to zero.
 */
void gfs_face_reset_normal_velocity (const FttCellFace * face)
{
  g_return_if_fail (face != NULL);

  GFS_FACE_NORMAL_VELOCITY_RIGHT (face) = 
    GFS_FACE_NORMAL_VELOCITY_LEFT (face) = 0.;
}

static void set_merged (FttCell * cell)
{
  GfsSolidVector * solid = GFS_STATE (cell)->solid;

  if (!GFS_IS_SMALL (cell))
    solid->merged = NULL;
  else {
    FttCellNeighbors neighbor;
    gdouble abest = 0.;
    FttDirection i;

    ftt_cell_neighbors (cell, &neighbor);
    for (i = 0; i < FTT_NEIGHBORS && abest < 1.; i++)
      if (neighbor.c[i] && !GFS_CELL_IS_BOUNDARY (neighbor.c[i]) && solid->s[i] > 0.) {
	gdouble a = GFS_IS_MIXED (neighbor.c[i]) ? GFS_STATE (neighbor.c[i])->solid->a : 1.;
	
	if (a > abest) {
	  abest = a;
	  solid->merged = neighbor.c[i];
	}
      }
    if (abest == 0.)
      g_warning ("file %s: line %d (%s): cannot merge small cell: %g",
		 __FILE__, __LINE__, G_GNUC_PRETTY_FUNCTION,
		 solid->a);
  }
}

/**
 * gfs_set_merged:
 * @domain: the domain to traverse.
 *
 * Sets the @merged field of the mixed cells of the domain defined
 * by @domain. 
 */
void gfs_set_merged (GfsDomain * domain)
{
  g_return_if_fail (domain != NULL);

  gfs_domain_traverse_mixed (domain, FTT_PRE_ORDER, FTT_TRAVERSE_LEAFS,
			     (FttCellTraverseFunc) set_merged, NULL);
}

static void add_merged (GSList ** merged, FttCell * cell)
{
  if ((cell->flags & GFS_FLAG_USED) == 0) {
    FttCellNeighbors neighbor;
    FttDirection i;
    GfsSolidVector * solid = GFS_STATE (cell)->solid;

    *merged = g_slist_prepend (*merged, cell);
    cell->flags |= GFS_FLAG_USED;

    if (GFS_IS_MIXED (cell) && solid->merged)
      add_merged (merged, solid->merged);

    ftt_cell_neighbors (cell, &neighbor);
    for (i = 0; i < FTT_NEIGHBORS; i++)
      if (neighbor.c[i]) {
	if (!FTT_CELL_IS_LEAF (neighbor.c[i])) {
	  FttCellChildren child;
	  guint j;
#if FTT_2D3
	  g_assert_not_implemented ();
#endif
	  ftt_cell_children_direction (neighbor.c[i], FTT_OPPOSITE_DIRECTION (i), &child);
	  for (j = 0; j < FTT_CELLS/2; j++)
	    if (GFS_IS_MIXED (child.c[j]) &&
		GFS_STATE (child.c[j])->solid->merged == cell)
	      add_merged (merged, child.c[j]);
	}	
	else if (GFS_IS_MIXED (neighbor.c[i]) && 
		 GFS_STATE (neighbor.c[i])->solid->merged == cell)
	  add_merged (merged, neighbor.c[i]);
      }
  }
}

static void traverse_merged (FttCell * cell, gpointer * datum)
{
  if ((cell->flags & GFS_FLAG_USED) == 0) {
    GfsMergedTraverseFunc func = (GfsMergedTraverseFunc) datum[0];
    gpointer data = datum[1];
    GSList * merged = NULL;

    add_merged (&merged, cell);
    (* func) (merged, data);
    g_slist_free (merged);
  }
}

static void traverse_non_merged (FttCell * cell, gpointer * datum)
{
  if ((cell->flags & GFS_FLAG_USED) != 0)
    cell->flags &= ~GFS_FLAG_USED;
  else {
    GfsMergedTraverseFunc func = (GfsMergedTraverseFunc) datum[0];
    gpointer data = datum[1];
    GSList * merged = g_slist_prepend (NULL, cell);

    (* func) (merged, data);
    g_slist_free (merged);    
  }
}

/**
 * gfs_domain_traverse_merged:
 * @domain: the domain to traverse.
 * @func: the function to call for each visited merged cells.
 * @data: user data to pass to @func.
 *
 * Traverses the merged leaf cells of the domain defined by @domain. A
 * list of merged cells is passed to @func. No cell belongs to more
 * than one merged list.  
 */
void gfs_domain_traverse_merged (GfsDomain * domain,
				GfsMergedTraverseFunc func,
				gpointer data)
{
  gpointer datum[2];
  
  g_return_if_fail (domain != NULL);
  g_return_if_fail (func != NULL);

  datum[0] = func;
  datum[1] = data;
  gfs_domain_traverse_mixed (domain, FTT_PRE_ORDER, FTT_TRAVERSE_LEAFS,
			    (FttCellTraverseFunc) traverse_merged, datum);
  gfs_domain_cell_traverse (domain, FTT_PRE_ORDER, FTT_TRAVERSE_LEAFS, -1,
			   (FttCellTraverseFunc) traverse_non_merged, datum);
}

/**
 * gfs_advection_update:
 * @merged: a list of merged #FttCell.
 * @par: the advection parameters.
 *
 * Updates the @v variable of @par for the merged cells of @merged
 * using the @fv update of each merged cell.
 *
 * The @v variable in each cell of the @merged list is set to its
 * average updated value over the composite cell defined by all the
 * cells in @merged.  
 */
void gfs_advection_update (GSList * merged, const GfsAdvectionParams * par)
{
  g_return_if_fail (merged != NULL);
  g_return_if_fail (par != NULL);

  if (merged->next == NULL) { /* cell is not merged */
    FttCell * cell = merged->data;

    if (GFS_IS_MIXED (cell)) {
#if 1
      GFS_VARIABLE (cell, par->v->i) += 
	GFS_VARIABLE (cell, par->fv->i)/GFS_STATE (cell)->solid->a;
#else /* D. Calhoun approach */
      FttDirection d;
      gdouble mins = G_MAXDOUBLE;
      GfsSolidVector * solid = GFS_STATE (cell)->solid;

      for (d = 0; d < FTT_NEIGHBORS; d++)
	if (solid->s[d] > 0. && 1./solid->s[d] < mins)
	  mins = 1./solid->s[d];
#if 0
fprintf (stderr, "%g %g %g\n",
	 solid->a, mins, 
	 GFS_VARIABLE (cell, par->fv)/(mins*solid->a));
#endif
      if (mins*solid->a > 0.01)
	GFS_VARIABLE (cell, par->v->i) += 
	  GFS_VARIABLE (cell, par->fv->i)/(mins*solid->a);
      else
	GFS_VARIABLE (cell, par->v->i) += 100.*GFS_VARIABLE (cell, par->fv->i);
      g_assert (GFS_VARIABLE (cell, par->v->i) < 10.);
#endif
    }
    else
      GFS_VARIABLE (cell, par->v->i) += GFS_VARIABLE (cell, par->fv->i);
  }
  else {
#if 0 /* J.J. Quirk volume-weighted values */
    GSList * i = merged;
    gdouble w = 0., total_vol = 0.;

    while (i) {
      FttCell * cell = i->data;
      gdouble a = GFS_IS_MIXED (cell) ? GFS_STATE (cell)->solid->a : 1.;
      
      total_vol += a;
      w += GFS_VARIABLE (cell, par->fv->i);
      i = i->next;
    }
    w /= total_vol;

    i = merged;
    while (i) {
      FttCell * cell = i->data;

      GFS_VARIABLE (cell, par->v->i) += w;
      i = i->next;
    }
#else /* average value */
    GSList * i = merged;
    gdouble w = 0., total_vol = 0.;

    while (i) {
      FttCell * cell = i->data;
      gdouble vol = ftt_cell_volume (cell);
      gdouble a = GFS_IS_MIXED (cell) ? GFS_STATE (cell)->solid->a : 1.;
      
      total_vol += vol*a;
      w += vol*(a*GFS_VARIABLE (cell, par->v->i) + 
		GFS_VARIABLE (cell, par->fv->i));
      i = i->next;
    }
    w /= total_vol;

    i = merged;
    while (i) {
      FttCell * cell = i->data;

      GFS_VARIABLE (cell, par->v->i) = w;
      i = i->next;
    }
#endif
  }
}

void gfs_advection_params_write (GfsAdvectionParams * par, FILE * fp)
{
  g_return_if_fail (par != NULL);
  g_return_if_fail (fp != NULL);

  fprintf (fp,
           "{\n"
	   "  cfl      = %g\n"
	   "  gradient = %s\n"
	   "  flux     = %s\n",
	   par->cfl,
	   par->gradient == gfs_center_gradient ? 
	   "gfs_center_gradient" :
	   "gfs_center_van_leer_gradient",
	   par->flux == gfs_face_advection_flux ?
	   "gfs_face_advection_flux" :
	   par->flux == gfs_face_velocity_advection_flux ?
	   "gfs_face_velocity_advection_flux" :
	   par->flux == gfs_face_velocity_convective_flux ?
	   "gfs_face_velocity_convective_flux" : "NULL");
  switch (par->scheme) {
  case GFS_GODUNOV: fputs ("  scheme   = godunov\n", fp); break;
  case GFS_VOF:     fputs ("  scheme   = vof\n", fp); break;
  case GFS_NONE:    fputs ("  scheme   = none\n", fp); break;
  }
  fputc ('}', fp);
}

void gfs_advection_params_init (GfsAdvectionParams * par)
{
  g_return_if_fail (par != NULL);

  par->cfl = 0.8;
  par->dt = 1.;
  par->gradient = gfs_center_gradient;
  par->upwinding = GFS_FACE_UPWINDING;
  par->use_centered_velocity = TRUE;
  par->scheme = GFS_GODUNOV;
  par->rho = 1.;
  par->c = NULL;
}

void gfs_advection_params_read (GfsAdvectionParams * par, GtsFile * fp)
{
  GtsFileVariable var[] = {
    {GTS_DOUBLE, "cfl",      TRUE},
    {GTS_STRING, "gradient", TRUE},
    {GTS_STRING, "flux",     TRUE},
    {GTS_STRING, "scheme",   TRUE},
    {GTS_NONE}
  };
  gchar * gradient = NULL, * flux = NULL, * scheme = NULL;

  g_return_if_fail (par != NULL);
  g_return_if_fail (fp != NULL);

  var[0].data = &par->cfl;
  var[1].data = &gradient;
  var[2].data = &flux;
  var[3].data = &scheme;

  gfs_advection_params_init (par);
  gts_file_assign_variables (fp, var);

  if (fp->type != GTS_ERROR && (par->cfl <= 0. || par->cfl > 1.))
    gts_file_variable_error (fp, var, "cfl", 
			     "cfl `%g' is out of range `]0,1]'", par->cfl);

  if (gradient) {
    if (!strcmp (gradient, "gfs_center_gradient"))
      par->gradient = gfs_center_gradient;
    else if (!strcmp (gradient, "gfs_center_van_leer_gradient"))
      par->gradient = gfs_center_van_leer_gradient;
    else if (fp->type != GTS_ERROR)
      gts_file_variable_error (fp, var, "gradient",
			       "unknown gradient parameter `%s'", gradient);
    g_free (gradient);
  }

  if (flux) {
    if (!strcmp (flux, "gfs_face_advection_flux"))
      par->flux = gfs_face_advection_flux;
    else if (!strcmp (flux, "gfs_face_velocity_advection_flux"))
      par->flux = gfs_face_velocity_advection_flux;
    else if (!strcmp (flux, "gfs_face_velocity_convective_flux"))
      par->flux = gfs_face_velocity_convective_flux;
    else if (fp->type != GTS_ERROR)
      gts_file_variable_error (fp, var, "flux",
			       "unknown flux parameter `%s'", flux);
    g_free (flux);
  }

  if (scheme) {
    if (!strcmp (scheme, "godunov"))
      par->scheme = GFS_GODUNOV;
    else if (!strcmp (scheme, "none"))
      par->scheme = GFS_NONE;
    else if (!strcmp (scheme, "vof")) {
      par->scheme = GFS_VOF;
      if (fp->type != GTS_ERROR && (par->cfl <= 0. || par->cfl > 0.5))
	gts_file_variable_error (fp, var, "cfl", 
				 "cfl `%g' is out of range `]0,0.5]'", 
				 par->cfl);
    }
    else if (fp->type != GTS_ERROR)
      gts_file_variable_error (fp, var, "scheme",
			       "unknown scheme parameter `%s'", scheme);
    g_free (scheme);
  }
}
