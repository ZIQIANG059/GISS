if test x$donotrun != xtrue; then
    if gerris3D $1 | gfsview-batch3D geo.gfv; then :
    else
	exit 1
    fi
fi

if cat <<EOF | python ; then :
from check import *
from sys import *
if (Curve('e',1,2) - Curve('e.ref',1,2)).max() > 1e-3:
    exit(1)
EOF
else
   exit 1
fi