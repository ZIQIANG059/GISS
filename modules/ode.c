/* Gerris - The GNU Flow Solver                       (-*-C-*-)
 * Copyright (C) 2009 National Institute of Water and Atmospheric Research
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

#include <ode/ode.h>
#include "moving.h"
#include "output.h"

#include "domain.c" //SPODE: the calculating part needs supports from domain.c

#define BDMAX 100 //SPODE: maximum solid numbers supported
#define _I(i,j) I[(i)*4+(j)-5] //SPODE: for moment of inertia calculation
#define PI 3.14159265359
static dWorldID world = 0; //SPODE: Initiating the world using in ODE
static gint K = 0, BUFF = 1; //SPODE: buffer time steps, solid will stay K steps
static gdouble Step_SP=0.;
static gint AF_Flag = 0;//Artificial force
static gdouble AF_Stf = 1e9; //SPODE: Stiffness factor
static gdouble L = 1., Ln = 1.;
typedef struct {
	gint buff_count;
	FttVector c,v,e,o;
	FttVector pf,vf,f,pm,vm,m;
	FttVector Mark;
} Oinfo;

typedef struct {
  /*< private >*/
  dBodyID body; //SPODE: a number refer to the body for the ode solver 
  dMass mass; //SPODE: this is a structure including the mass and the moment of inertia, refer ode manual to get the detail.
  FttVector c,ctmp; //SPODE: the center of the mass
  FttVector a_ex; // SPODE: addtional acceleration of the body
  FttVector m_ex; // SPODE: addtional angular acceleration of the body
  FttVector max, min;
  FttVector pf, vf, pm, vm; //SPODE: store the force and momentum applied on the body
  FttVector Mark;
  gdouble cradius; //SPODE: character length of the body
  gint f_tx, f_ty, f_tz, f_rx, f_ry, f_rz; //SPODE: trigger for the traslational and rotational degrees of freedom
  gint type; //SPODE: tell the type of the solid wall boundary
  gint axis; //SPODE: tell the axis of the solid wall boundary
  gint count;//mesh number
  Oinfo OPInfo;
} Bodyinfo; //SPODE: Used to store the body information

static Bodyinfo BDInfo[BDMAX]; //SPODE: the array to store the solids information, the BDInfo[0] refers to no solid
static gint BDNUM = 1; //SPODE: the number of bodys
static FttVector Mark, MarkOld;//Mark. marked cell, MarlNew,new cell, MarkOld,old cell
static gdouble MKDisOld=0.;//locate marked cell
static gint MKSWITCH = 0;//locate marked cell

/*SPODE:  ODEFoceCalculatior*/
#include "ode_calc.c"
/*SPODE:  GfsSurfaceBcODE*/
#include "ode_surf.c"
/*SPODE:  GfsSolidMovingODE*/
#include "ode_main.c"
/*SPODE:  GfsOutputSolidMovingODE*/
#include "ode_output.c"

/* Initialize module */

/* only define gfs_module_name for "official" modules (i.e. those installed in GFS_MODULES_DIR) */
const gchar gfs_module_name[] = "ode";
const gchar * g_module_check_init (void);

const gchar * g_module_check_init (void)
{
  dInitODE (); //SPODE:  Initiate the ode solver
  world = dWorldCreate (); //SPODE:  Initiate the world
  dWorldSetGravity (world, 0.0,0.0,0.0); //SPODE: the gravity(as accerleration) will be applied individually to each solid
  gfs_solid_moving_ode_class (); //SPODE: the main calculating part of this module
  gfs_output_solid_moving_ode_class (); //SPODE: the outputing part
  return NULL;
}