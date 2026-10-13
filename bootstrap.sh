#!/bin/sh
# Bootstrap mole on a fresh system
# Only needed once — after this, mole manages itself

set -e

echo "building mole..."
make clean
make -j"$(nproc 2>/dev/null || echo 4)"

echo "installing mole..."
./mole install mole

echo "done. mole can now upgrade itself with: mole upgrade"
