/* Gerris - The GNU Flow Solver
 * Copyright (C) 2001-2009 National Institute of Water and Atmospheric Research
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

#ifndef __RIVER_H__
#define __RIVER_H__

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#include "source.h"

/* GfsRiver: Header */

#define GFS_RIVER_NVAR 3

typedef struct _GfsRiver GfsRiver;

struct _GfsRiver {
  /*< private >*/
  GfsSimulation parent;
  gdouble cfl;

  /*< public >*/
  GfsVariable * v[GFS_RIVER_NVAR + 1], * v1[GFS_RIVER_NVAR], * zb, * H;
  GfsVariable * dv[FTT_DIMENSION][GFS_RIVER_NVAR + 1];
  GfsVariable * flux[GFS_RIVER_NVAR];
  gdouble g, dt;
  GfsCenterGradient gradient;
  guint time_order;
  gdouble dry;
};

#define GFS_RIVER(obj)            GTS_OBJECT_CAST (obj,\
					           GfsRiver,\
					           gfs_river_class ())
#define GFS_IS_RIVER(obj)         (gts_object_is_from_class (obj,\
						   gfs_river_class ()))

GfsSimulationClass * gfs_river_class        (void);

/* GfsBcSubcritical: Header */

#define GFS_IS_BC_SUBCRITICAL(obj)         (gts_object_is_from_class (obj,\
						 gfs_bc_subcritical_class ()))
GfsBcClass * gfs_bc_subcritical_class  (void);

/* GfsDischargeElevation: Header */

typedef struct _GfsDischargeElevation GfsDischargeElevation;

struct _GfsDischargeElevation {
  /*< private >*/
  GfsConstant parent;
  GfsVariable * P;
  gdouble flow;

  /*< public >*/
  GfsFunction * Q, * profile;
  gdouble tolerance;
};

#define GFS_DISCHARGE_ELEVATION(obj)            GTS_OBJECT_CAST (obj,\
					           GfsDischargeElevation,\
					           gfs_discharge_elevation_class ())
#define GFS_IS_DISCHARGE_ELEVATION(obj)         (gts_object_is_from_class (obj,\
						   gfs_discharge_elevation_class ()))

GfsEventClass * gfs_discharge_elevation_class (void);

/* GfsSourcePipe: Header */

typedef struct _GfsSourcePipe         GfsSourcePipe;

struct _GfsSourcePipe {
  /*< private >*/
  GfsSourceGeneric parent;
  FttCell * scell, * ecell;
  gdouble Q;

  /*< public >*/
  double (* flow_rate) (double z1, double h1, /* terrain elevation and flow depth at inlet */
			double z2, double h2, /* terrain elevation and flow depth at outlet */
			double l,             /* pipe length */
			double g,             /* acceleration of gravity */
			GfsSourcePipe * p);
  FttVector start, end;
  gdouble diameter;
};

#define GFS_SOURCE_PIPE(obj)            GTS_OBJECT_CAST (obj,\
					         GfsSourcePipe,\
					         gfs_source_pipe_class ())
#define GFS_IS_SOURCE_PIPE(obj)         (gts_object_is_from_class (obj,\
						 gfs_source_pipe_class ()))

GfsSourceGenericClass * gfs_source_pipe_class  (void);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __RIVER_H__ */
