GTS_VERSION=`pkg-config gts --modversion`
GERRIS_VERSION=`pkg-config gerris2D --modversion`

cat <<EOF > debian/control
Source: gerris-snapshot
Section: math
Priority: extra
Maintainer: Stephane Popinet <popinet@users.sf.net>
Build-Depends: debhelper (>> 4), autotools-dev, netcdfg-dev, libgsl0-dev

Package: gerris-snapshot
Section: math
Priority: extra
Architecture: any
Depends: libc6-dev | libc-dev, libgts-snapshot-dev (>= $GTS_VERSION), pkg-config, gcc, sed, gawk, m4, proj, libnetcdf3, libgsl0
Conflicts: gerris
Replaces: gerris
Recommends: gfsview-snapshot
Suggests: imagemagick, ffmpeg
Description: Gerris Flow Solver (development snapshot)
 Gerris is a system for the solution of the partial differential
 equations describing fluid flow.
 .
 A brief summary of its main (current) features:
 .
    * Quadtree-based (Octree in 3D) spatial discretisation with
      automatic and dynamic refinement.
    * Multigrid Poisson solver.
    * Second-order Godunov type advection scheme.
    * Solves the time-dependent incompressible Euler, Stokes ans Navier-Stokes
      equations.
    * Support for complex solid boundaries (automatic locally-refined
      mesh generation).
 .
 See http://gfs.sf.net for more information and documentation.
EOF

version=`date +%y%m%d`
date=`date +"%a, %e %b %Y %T %z"`
cat <<EOF > debian/changelog
gerris-snapshot ($GERRIS_VERSION-$version) testing; urgency=low

  * gerris-snapshot release (based on Marcelo's official debian)

 -- Stephane Popinet <popinet@users.sf.net>  $date
EOF
