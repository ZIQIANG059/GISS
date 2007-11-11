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

#include "domain.h"
#include "mpi_boundary.h"
#include "adaptive.h"

/* #define DEBUG */

static guint tag_shift = 32767/FTT_NEIGHBORS;

#define TAG(boundary)           (tag_shift*(boundary)->d + (boundary)->box->id)
#define MATCHING_TAG(boundary)  (tag_shift*FTT_OPPOSITE_DIRECTION ((boundary)->d) +\
                                 GFS_BOUNDARY_MPI (boundary)->id)

static void send (GfsBoundary * bb)
{
  GfsBoundaryPeriodic * boundary = GFS_BOUNDARY_PERIODIC (bb);
  GfsBoundaryMpi * mpi = GFS_BOUNDARY_MPI (bb);
  GfsDomain * domain = gfs_box_domain (bb->box);

  g_assert (boundary->sndcount <= boundary->sndbuf->len);
  if (GFS_BOUNDARY (boundary)->type == GFS_BOUNDARY_MATCH_VARIABLE) {
#ifdef DEBUG
fprintf (stderr, "%d send %d tag: %d\n",
	 domain->pid, 
	 mpi->process,
	 TAG (GFS_BOUNDARY (boundary)));
#endif
    MPI_Isend (&boundary->sndcount, 1, MPI_UNSIGNED,
	       mpi->process,
	       TAG (GFS_BOUNDARY (boundary)),
	       mpi->comm,
	       &(mpi->request[mpi->nrequest++]));
    gts_range_add_value (&domain->mpi_messages, sizeof (guint));
  }
#ifdef DEBUG
fprintf (stderr, "%d send %d tag: %d size: %d\n",
	 domain->pid, 
	 mpi->process,
	 TAG (GFS_BOUNDARY (boundary)),
	 boundary->sndcount);
#endif
  MPI_Isend (boundary->sndbuf->data, boundary->sndcount, MPI_DOUBLE,
	     mpi->process,
	     TAG (GFS_BOUNDARY (boundary)),
	     mpi->comm,
	     &(mpi->request[mpi->nrequest++]));
  gts_range_add_value (&domain->mpi_messages, 
                       sizeof (gdouble)*boundary->sndcount);
}

static void receive (GfsBoundary * bb,
		     FttTraverseFlags flags,
		     gint max_depth)
{
  GfsBoundaryPeriodic * boundary = GFS_BOUNDARY_PERIODIC (bb);
  GfsBoundaryMpi * mpi = GFS_BOUNDARY_MPI (bb);
  MPI_Status status;
  gint count;
#ifdef PROFILE_MPI
  GfsDomain * domain = gfs_box_domain (bb->box);
  gdouble start, end;

  start = MPI_Wtime ();
#endif /* PROFILE_MPI */

  if (GFS_BOUNDARY (boundary)->type == GFS_BOUNDARY_MATCH_VARIABLE) {
#ifdef DEBUG
fprintf (stderr, "%d wait %d %d match variable\n",
	 gfs_box_domain (bb->box)->pid,
	 mpi->process,
	 MATCHING_TAG (GFS_BOUNDARY (boundary)));
#endif
    MPI_Recv (&boundary->rcvcount, 1, MPI_UNSIGNED,
	      mpi->process,
	      MATCHING_TAG (GFS_BOUNDARY (boundary)),
	      mpi->comm,
	      &status);
#ifdef PROFILE_MPI
    end = MPI_Wtime ();
    gts_range_add_value (&domain->mpi_wait, end - start);
    start = MPI_Wtime ();
#endif /* PROFILE_MPI */
    if (boundary->rcvcount > boundary->rcvbuf->len)
      g_array_set_size (boundary->rcvbuf, boundary->rcvcount);
  }
  else
    boundary->rcvcount = boundary->sndcount;
#ifdef DEBUG
fprintf (stderr, "%d wait %d %d\n",
	 gfs_box_domain (bb->box)->pid,
	 mpi->process,
	 MATCHING_TAG (GFS_BOUNDARY (boundary)));
#endif
  g_assert (boundary->rcvcount <= boundary->rcvbuf->len);
  MPI_Recv (boundary->rcvbuf->data,
	    boundary->rcvcount,
	    MPI_DOUBLE,
	    mpi->process,
	    MATCHING_TAG (GFS_BOUNDARY (boundary)),
	    mpi->comm,
	    &status);
  MPI_Get_count (&status, MPI_DOUBLE, &count);
#ifdef DEBUG
  fprintf (stderr, "%d %d %d\n", status.MPI_SOURCE, status.MPI_TAG, status.MPI_ERROR);
  if (count == MPI_UNDEFINED) {
    fprintf (stderr, "%d %d count is undefined!\n",
	     gfs_box_domain (bb->box)->pid,
	     MATCHING_TAG (GFS_BOUNDARY (boundary)));
    g_assert_not_reached ();
  }
  else if (count != boundary->rcvcount) {
    fprintf (stderr, "%d %d count = %d boundary->rcvcount = %d\n",
	     gfs_box_domain (bb->box)->pid,
	     MATCHING_TAG (GFS_BOUNDARY (boundary)),
	     count, boundary->rcvcount);
    g_assert_not_reached ();
  }
#else
  g_assert (count == boundary->rcvcount);
#endif

#ifdef PROFILE_MPI
  end = MPI_Wtime ();
  gts_range_add_value (&domain->mpi_wait, end - start);
#endif /* PROFILE_MPI */

  (* gfs_boundary_periodic_class ()->receive) (bb, flags, max_depth);
}

static void synchronize (GfsBoundary * bb)
{
  GfsBoundaryMpi * boundary = GFS_BOUNDARY_MPI (bb);
  MPI_Status status;
  guint i;
#ifdef PROFILE_MPI
  GfsDomain * domain = gfs_box_domain (bb->box);
  gdouble start, end;

  start = MPI_Wtime ();
#endif /* PROFILE_MPI */

  /* wait for completion of non-blocking send(s) */
  for (i = 0; i < boundary->nrequest; i++)
    MPI_Wait (&(boundary->request[i]), &status);
#ifdef PROFILE_MPI
  end = MPI_Wtime ();
  gts_range_add_value (&domain->mpi_wait, end - start);
#endif /* PROFILE_MPI */
  boundary->nrequest = 0;
#ifdef DEBUG
  fprintf (stderr, "==== %d synchronised ====\n",
	   gfs_box_domain (bb->box)->pid);
#endif
  (* gfs_boundary_periodic_class ()->synchronize) (bb);
}

static void gfs_boundary_mpi_class_init (GfsBoundaryClass * klass)
{
  klass->send        = send;
  klass->receive     = receive;
  klass->synchronize = synchronize;
}

static void gfs_boundary_mpi_init (GfsBoundaryMpi * boundary)
{
  boundary->comm = MPI_COMM_WORLD;
  boundary->process = -1; 
  boundary->id = -1;

  boundary->nrequest = 0;
}

GfsBoundaryClass * gfs_boundary_mpi_class (void)
{
  static GfsBoundaryClass * klass = NULL;

  if (klass == NULL) {
    GtsObjectClassInfo gfs_boundary_mpi_info = {
      "GfsBoundaryMpi",
      sizeof (GfsBoundaryMpi),
      sizeof (GfsBoundaryClass),
      (GtsObjectClassInitFunc) gfs_boundary_mpi_class_init,
      (GtsObjectInitFunc) gfs_boundary_mpi_init,
      (GtsArgSetFunc) NULL,
      (GtsArgGetFunc) NULL
    };
    int * tagub, flag, maxtag;

    klass = gts_object_class_new (GTS_OBJECT_CLASS (gfs_boundary_periodic_class ()),
				  &gfs_boundary_mpi_info);
    MPI_Attr_get (MPI_COMM_WORLD, MPI_TAG_UB, &tagub, &flag);
    if (flag)
      maxtag = *tagub;
    else
      maxtag = 32767; /* minimum value from MPI standard specification */
    tag_shift = maxtag/FTT_NEIGHBORS;
  }

  return klass;
}

GfsBoundaryMpi * gfs_boundary_mpi_new (GfsBoundaryClass * klass,
				       GfsBox * box,
				       FttDirection d,
				       gint process,
				       gint id)
{
  GfsBoundaryMpi * boundary;
  int comm_size;

  MPI_Comm_size (MPI_COMM_WORLD, &comm_size);

  g_return_val_if_fail (process >= 0 && process < comm_size, NULL);

  boundary = GFS_BOUNDARY_MPI (gfs_boundary_periodic_new (klass, box, d, NULL));
  boundary->process = process;
  boundary->id = id;

  if (id >= tag_shift)
    g_warning ("GfsBoundaryMpi id (%d) is larger than the maximum MPI tag value\n"
	       "allowed on this system (%d)", id, tag_shift);

  return boundary;
}
