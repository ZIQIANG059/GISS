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
#include <gmodule.h>
#include "config.h"
#include "gfsconfig.h"
#include "simulation.h"
#include "output.h"
#include "refine.h"
#include "solid.h"
#include "adaptive.h"
#include "source.h"
#include "vof.h"
#include "tension.h"

/* GfsVariableTracer: object */

static void variable_tracer_init (GfsVariableTracer * v)
{
  gfs_advection_params_init (&v->advection);
  v->advection.gradient = gfs_center_van_leer_gradient;
  v->advection.flux = gfs_face_advection_flux;
  v->advection.v = GFS_VARIABLE1 (v);
  v->advection.fv = gfs_res;

  gfs_multilevel_params_init (&v->diffusion);
  v->diffusion.tolerance = 1e-6;
}

GfsVariableClass * gfs_variable_tracer_class (void)
{
  static GfsVariableClass * klass = NULL;

  if (klass == NULL) {
    GtsObjectClassInfo gfs_variable_tracer_info = {
      "GfsVariableTracer",
      sizeof (GfsVariableTracer),
      sizeof (GfsVariableClass),
      (GtsObjectClassInitFunc) NULL,
      (GtsObjectInitFunc) variable_tracer_init,
      (GtsArgSetFunc) NULL,
      (GtsArgGetFunc) NULL
    };
    klass = gts_object_class_new (GTS_OBJECT_CLASS (gfs_variable_class ()), 
				  &gfs_variable_tracer_info);
  }

  return klass;
}

/* GfsSimulation: object */

static void simulation_destroy (GtsObject * object)
{
  GfsSimulation * sim = GFS_SIMULATION (object);
  GSList * i;

  if (sim->surface)
    gts_object_destroy (GTS_OBJECT (sim->surface));
  if (sim->stree)
    gts_bb_tree_destroy (sim->stree, TRUE);

  gts_container_foreach (GTS_CONTAINER (sim->refines),
			 (GtsFunc) gts_object_destroy, NULL);
  gts_object_destroy (GTS_OBJECT (sim->refines));

  gts_container_foreach (GTS_CONTAINER (sim->adapts),
			 (GtsFunc) gts_object_destroy, NULL);
  gts_object_destroy (GTS_OBJECT (sim->adapts));

  gts_container_foreach (GTS_CONTAINER (sim->events),
			 (GtsFunc) gts_object_destroy, NULL);
  gts_object_destroy (GTS_OBJECT (sim->events));

  i = sim->modules;
  while (i) {
    g_module_close (i->data);
    i = i->next;
  }
  g_slist_free (sim->modules);
  g_slist_free (sim->variables);

  (* GTS_OBJECT_CLASS (gfs_simulation_class ())->parent_class->destroy) 
    (object);
}

static void simulation_write (GtsObject * object, FILE * fp)
{
  GfsSimulation * sim = GFS_SIMULATION (object);
  GSList * i;
  GfsVariable * v;

  (* GTS_OBJECT_CLASS (gfs_simulation_class ())->parent_class->write)
    (object, fp);

  fputs (" {\n"
	 "  GfsTime ", fp);
  gfs_time_write (&sim->time, fp);
  fputs ("\n  GfsPhysicalParams ", fp);
  gfs_physical_params_write (&sim->physical_params, fp);
  fputs ("\n  GfsAdvectionParams ", fp);
  gfs_advection_params_write (&sim->advection_params, fp);
  fputs ("\n  GfsApproxProjectionParams ", fp);
  gfs_multilevel_params_write (&sim->approx_projection_params, fp);
  fputs ("\n  GfsProjectionParams ", fp);
  gfs_multilevel_params_write (&sim->projection_params, fp);
  fputc ('\n', fp);

  i = sim->variables;
  while (i) {
    fputs ("  ", fp);
    (* GTS_OBJECT (i->data)->klass->write) (i->data, fp);
    fputc ('\n', fp);
    i = i->next;
  }

  i = sim->modules;
  while (i) {
    fprintf (fp, "  GModule %s\n", g_module_name (i->data));
    i = i->next;
  }

  v = GFS_DOMAIN (sim)->variables;
  while (v) {
    if (v->surface_bc) {
      fputs ("  ", fp);
      (* GTS_OBJECT (v->surface_bc)->klass->write) (GTS_OBJECT (v->surface_bc), fp);
      fputc ('\n', fp);
    }
    v = v->next;
  }

  if (GFS_DOMAIN (sim)->max_depth_write < -1) {
    i = sim->refines->items;
    while (i) {
      GtsObject * object = i->data;
      
      fputs ("  ", fp);
      g_assert (object->klass->write);
      (* object->klass->write) (object, fp);
      fputc ('\n', fp);
      i = i->next;
    }
  }

  i = sim->adapts->items;
  while (i) {
    GtsObject * object = i->data;

    fputs ("  ", fp);
    g_assert (object->klass->write);
    (* object->klass->write) (object, fp);
    fputc ('\n', fp);
    i = i->next;
  }

  i = sim->events->items;
  while (i) {
    GtsObject * object = i->data;
    GfsEvent * event = i->data;
    
    if (event->t < event->end && event->i < event->iend) {
      fputs ("  ", fp);
      g_assert (object->klass->write);
      (* object->klass->write) (object, fp);
      fputc ('\n', fp);
    }
    i = i->next;
  }

  if (sim->surface) {
    fputs ("  GtsSurface { ", fp);
    gts_surface_write (sim->surface, fp);
    fputs ("}\n", fp);
  }
#if 1
  if (sim->interface) {
    fputs ("  GtsInterface { ", fp);
    gts_surface_write (sim->interface, fp);
    fputs ("}\n", fp);
  }
#endif
  fputc ('}', fp);
}

static void check_solid_surface (GtsSurface * s, 
				 const gchar * fname,
				 GtsFile * fp)
{
  GtsSurface * self;
  GString * name = g_string_new ("surface");

  if (fname) {
    g_string_append (name, " `");
    g_string_append (name, fname);
    g_string_append_c (name, '\'');
  }

  if (!gts_surface_is_orientable (s))
    gts_file_error (fp, "%s is not orientable", name->str);
  else if (!gts_surface_is_closed (s))
    gts_file_error (fp, "%s is not closed", name->str);
  else if ((self = gts_surface_is_self_intersecting (s))) {
    gts_object_destroy (GTS_OBJECT (self));
    gts_file_error (fp, "%s is self-intersecting", name->str);
  }
  g_string_free (name, TRUE);
}

static GtsSurface * read_surface_file (GtsFile * fp, GtsSurface * surface)
{
  GtsSurface * s;
  FILE * fptr;
  GtsFile * fp1;
  
  gts_file_next_token (fp);
  if (fp->type != GTS_STRING) {
    gts_file_error (fp, "expecting a string (filename)");
    return NULL;
  }
  s = gts_surface_new (gts_surface_class (),
		       gts_face_class (),
		       gts_edge_class (),
		       surface ? surface->vertex_class : gts_vertex_class ());
  fptr = fopen (fp->token->str, "rt");
  if (fptr == NULL) {
    gts_file_error (fp, "cannot open file `%s'", fp->token->str);
    return NULL;
  }
  fp1 = gts_file_new (fptr);
  if (gts_surface_read (s, fp1)) {
    gts_file_error (fp, 
		    "file `%s' is not a valid GTS file\n"
		    "%s:%d:%d: %s",
		    fp->token->str, fp->token->str,
		    fp1->line, fp1->pos, fp1->error);
    gts_file_destroy (fp1);
    fclose (fptr);
    gts_object_destroy (GTS_OBJECT (s));
    return NULL;
  }
  gts_file_destroy (fp1);
  fclose (fptr);
  
  check_solid_surface (s, fp->token->str, fp);
  if (fp->type == GTS_ERROR) {
    gts_object_destroy (GTS_OBJECT (s));
    return NULL;
  }
  
  if (surface) {
    GtsSurface * self;
    
    gts_surface_merge (surface, s);
    gts_object_destroy (GTS_OBJECT (s));
    if ((self = gts_surface_is_self_intersecting (surface))) {
      gts_object_destroy (GTS_OBJECT (self));
      gts_file_error (fp, "merged surface is self-intersecting");
      return NULL;
    }
    return surface;
  }
  return s;
}

static GtsSurface * read_surface (GtsFile * fp, GtsSurface * surface)
{
  GtsSurface * s;

  gts_file_next_token (fp);
  if (fp->type != '{') {
    gts_file_error (fp, "expecting an opening brace");
    return NULL;
  }
  fp->scope_max++;
  gts_file_next_token (fp);
  
  s = gts_surface_new (gts_surface_class (),
		       gts_face_class (),
		       gts_edge_class (),
		       surface ? surface->vertex_class : gts_vertex_class ());
  
  if (gts_surface_read (s, fp)) {
    gts_object_destroy (GTS_OBJECT (s));
    return NULL;
  }
  if (fp->type != '}') {
    gts_object_destroy (GTS_OBJECT (s));
    gts_file_error (fp, "expecting a closing brace");
    return NULL;
  }
  fp->scope_max--;
  
  check_solid_surface (s, NULL, fp);
  if (fp->type == GTS_ERROR) {
    gts_object_destroy (GTS_OBJECT (s));
    return NULL;
  }
  
  if (surface) {
    GtsSurface * self;
    
    gts_surface_merge (surface, s);
    gts_object_destroy (GTS_OBJECT (s));
    if ((self = gts_surface_is_self_intersecting (surface))) {
      gts_object_destroy (GTS_OBJECT (self));
      gts_file_error (fp, "merged surface is self-intersecting");
      return NULL;
    }
    return surface;
  }
  return s;
}

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

static void simulation_read (GtsObject ** object, GtsFile * fp)
{
  GfsSimulation * sim = GFS_SIMULATION (*object);
  
  if (GTS_OBJECT_CLASS (gfs_simulation_class ())->parent_class->read)
    (* GTS_OBJECT_CLASS (gfs_simulation_class ())->parent_class->read)
      (object, fp);
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

    /* ------------ GtsSurface ------------ */
    if (strmatch (fp->token->str, "GtsSurface")) {
      GtsSurface * s;
      
      if ((s = read_surface (fp, sim->surface)) == NULL)
	return;
      sim->surface = s;
      gts_file_next_token (fp);
    }

    /* ------------ GtsSurfaceFile ------------ */
    else if (strmatch (fp->token->str, "GtsSurfaceFile")) {
      GtsSurface * s;

      if ((s = read_surface_file (fp, sim->surface)) == NULL)
	return;
      sim->surface = s;
      gts_file_next_token (fp);
    }

    /* ------------ GtsInterface ------------ */
    else if (strmatch (fp->token->str, "GtsInterface")) {
      GtsSurface * s;

      if (!sim->interface)
	sim->interface = gts_surface_new (gts_surface_class (),
					  gts_face_class (),
					  gts_edge_class (),
					  gts_vertex_normal_class ());
      if ((s = read_surface (fp, sim->interface)) == NULL)
	return;
      sim->interface = s;
      gts_file_next_token (fp);
    }

    /* ------------ GtsInterfaceFile ------------ */
    else if (strmatch (fp->token->str, "GtsInterfaceFile")) {
      GtsSurface * s;

      if (!sim->interface)
	sim->interface = gts_surface_new (gts_surface_class (),
					  gts_face_class (),
					  gts_edge_class (),
					  gts_vertex_normal_class ());
      if ((s = read_surface_file (fp, sim->interface)) == NULL)
	return;
      sim->interface = s;
      gts_file_next_token (fp);
    }

    /* ------------ GModule ------------ */
    else if (strmatch (fp->token->str, "GModule")) {
      gts_file_next_token (fp);
      if (fp->type != GTS_STRING) {
	gts_file_error (fp, "expecting a string (filename)");
	return;
      }
      if (!g_module_supported ())
	g_warning ("modules are not supported on this system");
      else {
	gchar * name, * path;
	GModule * module;

	name = g_strconcat (fp->token->str, FTT_DIMENSION == 2 ? "2D" : "3D");
	path = g_module_build_path (GFS_MODULES_DIR, name);
	g_free (name);
	module = g_module_open (path, 0);
	g_free (path);
	if (module == NULL)
	  module = g_module_open (fp->token->str, 0);
	if (module == NULL) {
	  gts_file_error (fp, "cannot load module: %s", g_module_error ());
	  return;
	}
	g_module_make_resident (module);
	sim->modules = g_slist_prepend (sim->modules, module);
      }
      gts_file_next_token (fp);
    }

    /* ------------ GfsTime ------------ */
    else if (strmatch (fp->token->str, "GfsTime")) {
      gts_file_next_token (fp);
      gfs_time_read (&sim->time, fp);
      if (fp->type == GTS_ERROR)
	return;
    }

    /* ------------ GfsPhysicalParams ------------ */
    else if (strmatch (fp->token->str, "GfsPhysicalParams")) {
      gts_file_next_token (fp);
      gfs_physical_params_read (&sim->physical_params, fp);
      if (fp->type == GTS_ERROR)
	return;
    }

    /* ------------ GfsProjectionParams ------------ */
    else if (strmatch (fp->token->str, "GfsProjectionParams")) {
      gts_file_next_token (fp);
      gfs_multilevel_params_read (&sim->projection_params, fp);
      if (fp->type == GTS_ERROR)
	return;
    }

    /* ------------ GfsApproxProjectionParams ------------ */
    else if (strmatch (fp->token->str, "GfsApproxProjectionParams")) {
      gts_file_next_token (fp);
      gfs_multilevel_params_read (&sim->approx_projection_params, fp);
      if (fp->type == GTS_ERROR)
	return;
    }

    /* ------------ GfsAdvectionParams ------------ */
    else if (strmatch (fp->token->str, "GfsAdvectionParams")) {
      gts_file_next_token (fp);
      gfs_advection_params_read (&sim->advection_params, fp);
      if (fp->type == GTS_ERROR)
	return;
    }

    /* ------------ GtsObject ------------ */
    else {
      GtsObjectClass * klass = gfs_object_class_from_name (fp->token->str);
      GtsObject * object;

      if (klass == NULL ||
	  (!gts_object_class_is_from_class (klass, gfs_refine_class ()) &&
	   !gts_object_class_is_from_class (klass, gfs_event_class ()) &&
	   !gts_object_class_is_from_class (klass, gfs_variable_class ()) &&
	   !gts_object_class_is_from_class (klass, gfs_surface_generic_bc_class ()))) {
	gts_file_error (fp, "unknown keyword `%s'", fp->token->str);
	return;
      }

      object = gts_object_new (klass);
      gfs_object_simulation (object) = sim;

      g_assert (klass->read);
      (* klass->read) (&object, fp);
      if (fp->type == GTS_ERROR) {
	gts_object_destroy (object);
	return;
      }

      if (GFS_IS_REFINE (object))
	gts_container_add (GTS_CONTAINER (sim->refines), 
			   GTS_CONTAINEE (object));
      else if (GFS_IS_ADAPT (object))
	gts_container_add (GTS_CONTAINER (sim->adapts),
			   GTS_CONTAINEE (object));
      else if (GFS_IS_EVENT (object))
	gts_container_add (GTS_CONTAINER (sim->events), 
			   GTS_CONTAINEE (object));
      else if (GFS_IS_VARIABLE (object)) {
	GfsVariable * v = GFS_VARIABLE1 (object);
	GfsVariable * old = gfs_variable_from_name (GFS_DOMAIN (sim)->variables, v->name);

	if (old == NULL)
	  gfs_domain_add_new_variable (GFS_DOMAIN (sim), v);
	else {
	  gfs_domain_replace_variable (GFS_DOMAIN (sim), old, v);
	  gts_object_destroy (GTS_OBJECT (old));
	  sim->variables = g_slist_remove (sim->variables, old);
	}
	sim->variables = g_slist_append (sim->variables, v);
      }
      else if (GFS_IS_SURFACE_GENERIC_BC (object))
	;
      else
	g_assert_not_reached ();
    }
  }
  
  if (fp->type != '}') {
    gts_file_error (fp, "expecting a closing brace");
    return;
  }
  fp->scope_max--;
  gts_file_next_token (fp);

  if (sim->surface) {
    if (sim->stree)
      gts_bb_tree_destroy (sim->stree, TRUE);
    sim->stree = gts_bb_tree_surface (sim->surface);
    if (gts_surface_volume (sim->surface) < 0.)
      sim->is_open = TRUE;
  }
  if (sim->interface) {
    if (sim->itree)
      gts_bb_tree_destroy (sim->itree, TRUE);
    sim->itree = gts_bb_tree_surface (sim->interface);
    if (gts_surface_volume (sim->interface) < 0.)
      sim->i_is_open = TRUE;
  }
  sim->refines->items = g_slist_reverse (sim->refines->items);
  sim->adapts->items = g_slist_reverse (sim->adapts->items);
  sim->events->items = g_slist_reverse (sim->events->items);
  sim->modules = g_slist_reverse (sim->modules);
  sim->advection_params.rho = sim->physical_params.rho;
}

static void simulation_run (GfsSimulation * sim)
{
  GfsVariable * v, * c, * ch;
  GfsDomain * domain;

  domain = GFS_DOMAIN (sim);

  gfs_simulation_refine (sim);

  gfs_simulation_event_init (sim, sim->events->items);
  gfs_simulation_event_init (sim, sim->adapts->items);

  gfs_set_merged (domain);
  v = domain->variables;
  while (v) {
    gfs_domain_bc (domain, FTT_TRAVERSE_LEAFS, -1, v);
    v = v->next;
  }
  c = gfs_variable_from_name (domain->variables, "C");
  g_assert (c == NULL);
  ch = gfs_variable_from_name (domain->variables, "CH");
  g_assert (ch == NULL);

  sim->advection_params.c = c;
  gfs_approximate_projection (domain,
      			      &sim->approx_projection_params,
      			      &sim->advection_params);

  gts_range_init (&domain->mpi_wait);
  while (sim->time.t < sim->time.end &&
	 sim->time.i < sim->time.iend) {
    gfs_domain_cell_traverse (domain,
			      FTT_POST_ORDER, FTT_TRAVERSE_NON_LEAFS, -1,
			      (FttCellTraverseFunc) gfs_cell_coarse_init, domain);
    gfs_simulation_event (sim, sim->events->items);

    g_timer_start (domain->timer);

    gfs_simulation_set_timestep (sim);

    sim->advection_params.c = c;

    gfs_predicted_face_velocities (domain, FTT_DIMENSION, &sim->advection_params);
    gfs_mac_projection (domain, 
    			&sim->projection_params, 
    			&sim->advection_params);

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

    gfs_simulation_event_half (sim, sim->events->items);

    sim->advection_params.c = ch;
    gfs_centered_velocity_advection_diffusion (domain,
					       FTT_DIMENSION,
					       &sim->advection_params,
					       &sim->diffusion_params);
    gfs_simulation_adapt (sim, NULL);
    gfs_approximate_projection (domain,
   				&sim->approx_projection_params, 
    				&sim->advection_params);

    sim->time.t = sim->tnext;
    sim->time.i++;

    g_timer_stop (domain->timer);
    gts_range_add_value (&domain->timestep,
			 g_timer_elapsed (domain->timer, NULL));
    gts_range_update (&domain->timestep);
    gts_range_add_value (&domain->size, 
			 gfs_domain_size (domain, FTT_TRAVERSE_LEAFS, -1));
    gts_range_update (&domain->size);
  }
  gfs_simulation_event (sim, sim->events->items);
  gts_container_foreach (GTS_CONTAINER (sim->events),
			 (GtsFunc) gts_object_destroy, NULL);
}

static void gfs_simulation_class_init (GfsSimulationClass * klass)
{
  GTS_OBJECT_CLASS (klass)->write =   simulation_write;
  GTS_OBJECT_CLASS (klass)->read =    simulation_read;
  GTS_OBJECT_CLASS (klass)->destroy = simulation_destroy;

  klass->run = simulation_run;
}

static void gfs_simulation_init (GfsSimulation * object)
{
  gfs_time_init (&object->time);
  gfs_physical_params_init (&object->physical_params);

  gfs_advection_params_init (&object->advection_params);
  object->advection_params.flux = gfs_face_velocity_advection_flux;
  object->advection_params.fv = gfs_res;
  gfs_multilevel_params_init (&object->diffusion_params);
  object->diffusion_params.tolerance = 1e-6;

  gfs_multilevel_params_init (&object->projection_params);
  gfs_multilevel_params_init (&object->approx_projection_params);

  object->surface = NULL;
  object->stree = NULL;
  object->is_open = FALSE;

  object->interface = NULL;
  object->itree = NULL;
  object->i_is_open = FALSE;

  object->refines = GTS_SLIST_CONTAINER (gts_container_new 
					 (GTS_CONTAINER_CLASS 
					  (gts_slist_container_class ())));
  object->adapts = GTS_SLIST_CONTAINER (gts_container_new 
					(GTS_CONTAINER_CLASS 
					 (gts_slist_container_class ())));
  gfs_adapt_stats_init (&object->adapts_stats);
  object->events = GTS_SLIST_CONTAINER (gts_container_new 
					(GTS_CONTAINER_CLASS 
					 (gts_slist_container_class ())));
  object->modules = NULL;
  
  object->tnext = 0.;
}

GfsSimulationClass * gfs_simulation_class (void)
{
  static GfsSimulationClass * klass = NULL;

  if (klass == NULL) {
    GtsObjectClassInfo gfs_simulation_info = {
      "GfsSimulation",
      sizeof (GfsSimulation),
      sizeof (GfsSimulationClass),
      (GtsObjectClassInitFunc) gfs_simulation_class_init,
      (GtsObjectInitFunc) gfs_simulation_init,
      (GtsArgSetFunc) NULL,
      (GtsArgGetFunc) NULL
    };
    klass = gts_object_class_new (GTS_OBJECT_CLASS (gfs_domain_class ()),
				  &gfs_simulation_info);
  }

  return klass;
}

GfsSimulation * gfs_simulation_new (GfsSimulationClass * klass)
{
  GfsSimulation * object;

  object = GFS_SIMULATION (gts_graph_new (GTS_GRAPH_CLASS (klass),
					 GTS_GNODE_CLASS (gfs_box_class ()),
					 GTS_GEDGE_CLASS (gfs_gedge_class ())));

  return object;
}

static void box_init_solid_fractions (GfsBox * box, GfsSimulation * sim)
{
  gfs_cell_init_solid_fractions (box->root, 
				 sim->surface, sim->stree, sim->is_open,
				 TRUE, (FttCellCleanupFunc) gfs_cell_cleanup,
				 NULL);
  if (FTT_CELL_IS_DESTROYED (box->root)) {
    FttVector p;

    ftt_cell_pos (box->root, &p);
    g_warning ("%s centered at (%g,%g,%g) is entirely filled by a solid.\n"
	       "Aborting...\n", 
	       GTS_OBJECT (box)->klass->info.name,
	       p.x, p.y, p.z);
    exit (1);
  }
}

static void refine_cell_corner (FttCell * cell, GfsDomain * domain)
{
  if (ftt_refine_corner (cell))
    ftt_cell_refine_single (cell, (FttCellInitFunc) gfs_cell_init, domain);
}

static void check_face (FttCellFace * f, guint * nf)
{
  GfsSolidVector * s = GFS_STATE (f->cell)->solid;

  if (s && !f->neighbor && s->s[f->d] > 0. && s->s[f->d] < 1.)
    (*nf)++;
}

static void check_solid_fractions (GfsBox * box, gpointer * data)
{
  GfsSimulation * sim = data[0];
  guint * nf = data[1];
  FttDirection d;

  gfs_cell_check_solid_fractions (box->root, sim->surface, sim->stree, sim->is_open);
  for (d = 0; d < FTT_NEIGHBORS; d++)
    ftt_face_traverse_boundary (box->root, d, FTT_PRE_ORDER, FTT_TRAVERSE_LEAFS, -1,
				(FttFaceTraverseFunc) check_face, nf);
}

/**
 * gfs_simulation_refine:
 * @sim: a #GfsSimulation.
 *
 * Calls the @refine() methods of the #GfsRefine of @sim. Matches the
 * boundaries by calling gfs_domain_match().
 */
void gfs_simulation_refine (GfsSimulation * sim)
{
  GSList * i;
  guint depth, nf = 0;
  gint l;
  gpointer data[2];

  g_return_if_fail (sim != NULL);

  i = sim->refines->items;
  while (i) {
    GfsRefine * refine = i->data;
    GSList * next = i->next;
    
    g_assert (GFS_REFINE_CLASS (GTS_OBJECT (refine)->klass)->refine);
    (* GFS_REFINE_CLASS (GTS_OBJECT (refine)->klass)->refine) (refine, sim);
    i = next;
  }

  depth = gfs_domain_depth (GFS_DOMAIN (sim));
  for (l = depth - 2; l >= 0; l--)
    gfs_domain_cell_traverse (GFS_DOMAIN (sim),
			     FTT_PRE_ORDER, FTT_TRAVERSE_LEVEL, l,
			     (FttCellTraverseFunc) refine_cell_corner, 
			      GFS_DOMAIN (sim));
  gfs_domain_match (GFS_DOMAIN (sim));

  if (sim->surface) {
    gts_container_foreach (GTS_CONTAINER (sim), (GtsFunc) box_init_solid_fractions, sim);
    gfs_domain_match (GFS_DOMAIN (sim));
  }
  data[0] = sim;
  data[1] = &nf;
  gts_container_foreach (GTS_CONTAINER (sim), (GtsFunc) check_solid_fractions, data);
  if (nf > 0)
    g_warning ("the solid surface cuts %d boundary cells,\n"
	       "this may cause errors for diffusion terms\n", nf);
}

/**
 * gfs_simulation_read:
 * @fp: a #GtsFile.
 *
 * Reads a simulation file from @fp.
 *
 * Returns: the #GfsSimulation or %NULL if an error occured, in which
 * case the @pos and @error fields of @fp are set.
 */
GfsSimulation * gfs_simulation_read (GtsFile * fp)
{
  GfsDomain * d;

  g_return_val_if_fail (fp != NULL, NULL);

  d = gfs_domain_read (fp);
  if (d != NULL && !GFS_IS_SIMULATION (d)) {
    gts_file_error (fp, "parent graph is not a GfsSimulation");
    gts_object_destroy (GTS_OBJECT (d));
    return NULL;
  }
  return GFS_SIMULATION (d);
}

/**
 * gfs_simulation_write:
 * @sim: a #GfsSimulation.
 * @max_depth: the maximum depth at which to stop writing cell tree
 * data (-1 means no limit).
 * @fp: a file pointer.
 *
 * Writes in @fp a text representation of @sim. If @max_depth is
 * smaller or equal to -2, no cell tree data is written.  
 */
void gfs_simulation_write (GfsSimulation * sim,
			   gint max_depth,		  
			   FILE * fp)
{
  gint depth;
  GfsDomain * domain;

  g_return_if_fail (sim != NULL);
  g_return_if_fail (fp != NULL);

  fprintf (fp, "# Gerris Flow Solver %dD version %s\n",
	   FTT_DIMENSION, GFS_VERSION);
  domain = GFS_DOMAIN (sim);
  depth = domain->max_depth_write;
  domain->max_depth_write = max_depth;
  gts_graph_write (GTS_GRAPH (sim), fp);
  domain->max_depth_write = depth;
}

/**
 * gfs_simulation_set_timestep:
 * @sim: a #GfsSimulation.
 *
 * Sets the time step for the next iteration of @sim using the CFL
 * (computed using gfs_domain_cfl()) and taking into account the
 * timings of the various #GfsEvent associated to @sim.
 *
 * More precisely, the time step is adjusted (if necessary) so that
 * the time of the closest event is exactly reached after the
 * iteration.  
 */
void gfs_simulation_set_timestep (GfsSimulation * sim)
{
  gdouble t;
  GSList * i;

  g_return_if_fail (sim != NULL);

  t = sim->time.t;
  sim->advection_params.dt =
    sim->advection_params.cfl*gfs_domain_cfl (GFS_DOMAIN (sim), FTT_TRAVERSE_LEAFS, -1);
  if (sim->advection_params.dt > sim->time.dtmax)
    sim->advection_params.dt = sim->time.dtmax;
  sim->tnext = t + sim->advection_params.dt;

  i = sim->events->items;
  while (i) {
    GfsEvent * event = i->data;
    GSList * next = i->next;

    if (t < event->t && sim->tnext > event->t) {
      sim->advection_params.dt = event->t - t;
      sim->tnext = event->t;
    }
    i = next;
  }
  if (sim->tnext > sim->time.end) {
    sim->advection_params.dt = sim->time.end - t;
    sim->tnext = sim->time.end;
  }
}

/**
 * gfs_simulation_event:
 * @sim: a #GfsSimulation.
 * @events: a list of #GfsEvent.
 * 
 * Checks if any event associated with @sim must be activated and
 * activates it if necessary.
 */
void gfs_simulation_event (GfsSimulation * sim,
			   GSList * events)
{
  g_return_if_fail (sim != NULL);

  while (events) {
    GfsEvent * event = events->data;
    GSList * next = events->next;

    g_assert (GFS_EVENT_CLASS (GTS_OBJECT (event)->klass)->event);
    (* GFS_EVENT_CLASS (GTS_OBJECT (event)->klass)->event) (event, sim);
    
    events = next;
  }
}

/**
 * gfs_simulation_event_half:
 * @sim: a #GfsSimulation.
 * @events: a list of #GfsEvent.
 * 
 * Checks if any half-timestep event associated with @sim must be
 * activated and activates it if necessary.
 */
void gfs_simulation_event_half (GfsSimulation * sim,
				GSList * events)
{
  g_return_if_fail (sim != NULL);

  while (events) {
    GfsEvent * event = events->data;
    GSList * next = events->next;

    if (event->realised && GFS_EVENT_CLASS (GTS_OBJECT (event)->klass)->event_half)
      (* GFS_EVENT_CLASS (GTS_OBJECT (event)->klass)->event_half) (event, sim);
    
    events = next;
  }
}

/**
 * gfs_simulation_event_init:
 * @sim: a #GfsSimulation.
 * @events: a list of #GfsEvent.
 *
 * Initalizes the events associated with @sim. In particular, all the
 * "init" events are activated by this function.
 */
void gfs_simulation_event_init (GfsSimulation * sim,
				GSList * events)
{
  g_return_if_fail (sim != NULL);

  while (events) {
    GfsEvent * event = events->data;
    GSList * next = events->next;

    if (GFS_DOMAIN (sim)->pid > 0 &&
	GFS_IS_OUTPUT (event) && 
	(!strcmp (GFS_OUTPUT (event)->format, "stderr") ||
	 !strcmp (GFS_OUTPUT (event)->format, "stdout")))
      gfs_output_mute (GFS_OUTPUT (event));

    if (event->start < 0.) { /* "init" event */
      g_assert (GFS_EVENT_CLASS (GTS_OBJECT (event)->klass)->event);
      (* GFS_EVENT_CLASS (GTS_OBJECT (event)->klass)->event) (event, sim);
    }
    else if (event->end_event)
      event->t = event->start = G_MAXDOUBLE/2.;
    else {
      if (event->istep < G_MAXINT)
	while (event->i < sim->time.i) {
	  event->n++;
	  event->i += event->istep;
	}
      else
	while (event->t < sim->time.t) {
	  event->n++;
	  event->t = event->start + event->n*event->step;
	}
    }
    events = next;
  }
}

/**
 * gfs_time_write:
 * @t: the time structure.
 * @fp: a file pointer.
 *
 * Writes in @fp a text representation of the time structure @t.
 */
void gfs_time_write (GfsTime * t, FILE * fp)
{
  g_return_if_fail (t != NULL);
  g_return_if_fail (fp != NULL);

  fprintf (fp, "{ i = %u t = %g ", t->i, t->t);
  if (t->start != 0.)
    fprintf (fp, "start = %g ", t->start);
  if (t->istart != 0)
    fprintf (fp, "start = %u ", t->istart);
  if (t->end < G_MAXDOUBLE)
    fprintf (fp, "end = %g ", t->end);
  if (t->iend < G_MAXINT)
    fprintf (fp, "iend = %u ", t->iend);
  if (t->dtmax < G_MAXDOUBLE)
    fprintf (fp, "dtmax = %g ", t->dtmax);
  fputc ('}', fp);
}

/**
 * gfs_time_init:
 * @t: the #GfsTime.
 *
 * Initializes the time structure @t with default values.
 */
void gfs_time_init (GfsTime * t)
{
  g_return_if_fail (t != NULL);
  
  t->t = t->start = 0.;
  t->end = G_MAXDOUBLE;

  t->i = t->istart = 0;
  t->iend = G_MAXINT;

  t->dtmax = G_MAXDOUBLE;
}

/**
 * gfs_time_read:
 * @t: the #GfsTime.
 * @fp: the #GtsFile.
 *
 * Reads a time structure from @fp and puts it in @t.
 */
void gfs_time_read (GfsTime * t, GtsFile * fp)
{
  GtsFileVariable var[] = {
    {GTS_DOUBLE, "t",      TRUE},
    {GTS_DOUBLE, "start",  TRUE},
    {GTS_DOUBLE, "end",    TRUE},
    {GTS_UINT,   "i",      TRUE},
    {GTS_UINT,   "istart", TRUE},
    {GTS_UINT,   "iend",   TRUE},
    {GTS_DOUBLE, "dtmax",  TRUE},
    {GTS_NONE}
  };

  g_return_if_fail (t != NULL);
  g_return_if_fail (fp != NULL);

  var[0].data = &t->t;
  var[1].data = &t->start;
  var[2].data = &t->end;
  var[3].data = &t->i;
  var[4].data = &t->istart;
  var[5].data = &t->iend;
  var[6].data = &t->dtmax;

  gfs_time_init (t);
  gts_file_assign_variables (fp, var);

  if (t->t < t->start)
    t->t = t->start;
  if (t->i < t->istart)
    t->i = t->istart;
}

/**
 * gfs_physical_params_write:
 * @p: the physical parameters structure.
 * @fp: a file pointer.
 *
 * Writes in @fp a text representation of the physical parameters
 * structure @p.  
 */
void gfs_physical_params_write (GfsPhysicalParams * p, FILE * fp)
{
  g_return_if_fail (p != NULL);
  g_return_if_fail (fp != NULL);

  fprintf (fp, "{ rho = %g sigma = %g g = %g }", p->rho, p->sigma, p->g);
}

/**
 * gfs_physical_params_init:
 * @p: the #GfsPhysicalParams.
 *
 * Initializes the physical parameters structure @p with default values.
 */
void gfs_physical_params_init (GfsPhysicalParams * p)
{
  g_return_if_fail (p != NULL);
  
  p->rho = 1.;
  p->sigma = 0.;
  p->g = 1.;
}

/**
 * gfs_physical_params_read:
 * @p: the #GfsPhysicalParams.
 * @fp: the #GtsFile.
 *
 * Reads a physical parameters structure from @fp and puts it in @p.
 */
void gfs_physical_params_read (GfsPhysicalParams * p, GtsFile * fp)
{
  GtsFileVariable var[] = {
    {GTS_DOUBLE, "rho",   TRUE},
    {GTS_DOUBLE, "sigma", TRUE},
    {GTS_DOUBLE, "g",     TRUE},
    {GTS_NONE}
  };

  g_return_if_fail (p != NULL);
  g_return_if_fail (fp != NULL);

  var[0].data = &p->rho;
  var[1].data = &p->sigma;
  var[2].data = &p->g;

  gfs_physical_params_init (p);
  gts_file_assign_variables (fp, var);
  if (p->rho <= 0.)
    gts_file_variable_error (fp, var, "rho", "rho must be strictly positive");
  if (p->sigma < 0.)
    gts_file_variable_error (fp, var, "sigma", "sigma must be positive");
}

/**
 * gfs_simulation_run:
 * @sim: a #GfsSimulation.
 *
 * Runs @sim.
 */
void gfs_simulation_run (GfsSimulation * sim)
{
  g_return_if_fail (sim != NULL);

  (* GFS_SIMULATION_CLASS (GTS_OBJECT (sim)->klass)->run) (sim);
}
