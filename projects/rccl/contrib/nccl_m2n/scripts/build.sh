#!/bin/bash
# Build RCCL (gfx950), nccl_m2n lib + tests, benches, and the nccl4py + nccl.m2n wheels from $SRC.
# Run inside the container as yourself. Usage: bash build.sh [rccl|m2n|bench|py|all]
set -eo pipefail
D=$(dirname "$(readlink -f "$0")")
WHAT=${1:-all}
want() { [[ $WHAT == $1 || $WHAT == all ]]; }

if [[ $WHAT != py ]]; then
  . $D/env.sh
  echo "SRC=$SRC HEAD=$(git -C $SRC rev-parse --short HEAD) ROCM_PATH=$ROCM_PATH"
fi

if want rccl; then
  B=${RCCL_BUILD:-/tmp/m2n-rccl-build-$(id -u)}
  cmake -S $SRC/projects/rccl -B $B -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_CXX_COMPILER=$ROCM_PATH/llvm/bin/amdclang++ -DCMAKE_C_COMPILER=$ROCM_PATH/llvm/bin/amdclang \
    -DROCM_PATH=$ROCM_PATH -DCMAKE_PREFIX_PATH=$ROCM_PATH \
    -DGPU_TARGETS=gfx950 -DAMDGPU_TARGETS=gfx950 -DBUILD_TESTS=OFF \
    -DCMAKE_INSTALL_PREFIX=$RCCL_INSTALL > $WORK/logs/build_rccl_cmake.log 2>&1
  cmake --build $B -j ${JOBS:-128} > $WORK/logs/build_rccl.log 2>&1 || { tail -30 $WORK/logs/build_rccl.log; exit 1; }
  cmake --install $B > /dev/null
  ls -l $RCCL_INSTALL/lib/librccl.so
fi

if want m2n || want bench; then
  MK=(make -f Makefile.rocm -j32 ROCM_PATH=$ROCM_PATH NCCL_HOME=$RCCL_INSTALL RCCL_LIBDIR=$RCCL_INSTALL/lib
      GTEST_DIR=${GTEST_DIR:-$(dirname "$SRC")/googletest/googletest} MPI_HOME=/opt/openmpi OFFLOAD_ARCH=gfx950
      BUILDDIR=$WORK/mk)
  cd $SRC/projects/rccl/contrib/nccl_m2n
  if want m2n; then
    for t in lib tests; do
      "${MK[@]}" $t > $WORK/logs/build_m2n_$t.log 2>&1 || { tail -30 $WORK/logs/build_m2n_$t.log; exit 1; }
    done
    ls -l $BIN/basic_api_test_local $BIN/basic_api_test_mpi
  fi
  if want bench; then
    "${MK[@]}" reshard reshard_model > $WORK/logs/build_bench.log 2>&1 || { tail -30 $WORK/logs/build_bench.log; exit 1; }
    ls -l $BIN/reshard_bench $BIN/reshard_model_bench
  fi
fi

if want py; then
  # Wheels into $WORK/whl; install them as root afterwards (README).
  . $D/py_env.sh
  W=$WORK/whl; T=/tmp/m2n-py-build-$(id -u)
  rm -rf $T $W && mkdir -p $T $W
  cp -r --no-preserve=all $SRC/projects/rccl/bindings/nccl4py $T/nccl4py
  cp -r --no-preserve=all $M2N_PY $T/nccl_m2n
  (cd $T/nccl4py && SETUPTOOLS_SCM_PRETEND_VERSION=${NCCL4PY_VERSION:-0.2.0} \
    python -m pip wheel -q --no-build-isolation --no-deps -w $W .)
  # The nccl.m2n build needs the port importable (setup.py checks nccl._hip_compat).
  python -m pip install -q --no-deps --target $T/site $W/nccl4py-*.whl
  (cd $T/nccl_m2n && PYTHONPATH=$T/site python -m pip wheel -q --no-build-isolation --no-deps -w $W .)
  ls $W
fi
