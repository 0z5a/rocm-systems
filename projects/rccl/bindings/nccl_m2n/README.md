# nccl.m2n on ROCm

`nccl.m2n` (DTensor-style reshard between disjoint rank groups) from upstream
[NVIDIA/nccl-extensions](https://github.com/NVIDIA/nccl-extensions) `python/`, `nccl_m2n` part only, at
the commit in `UPSTREAM_VERSION`. It builds on the RCCL port of nccl4py (`../nccl4py`, whose
`nccl.bindings.cynccl` the Cython bindings use) and loads the `libnccl_m2n.so` that
`projects/rccl/contrib/nccl_m2n/Makefile.rocm` builds (`NCCL_M2N_LIBRARY`).

The C library in `projects/rccl/contrib/nccl_m2n` comes from the same upstream commit
(`contrib/nccl_m2n/UPSTREAM_VERSION`): resync both together, the bindings follow `nccl_m2n.h`.

Changes against upstream: the Cython bindings build against the port's `nccl.bindings.cynccl` (no CUDA
headers), `cuda.bindings`/`cuda.pathfinder` are not used on ROCm, `nccl.ep` and the CUDA packaging are
dropped. Added: `rocm/sitecustomize.py` (keeps one `librccl` per process when torch is loaded; active
when `rocm/` is on `PYTHONPATH`) and `tests/rocm/xdtensor_reshard_check.py` (DTensor reshard against a
torch reference, communicator from `nccl.core.Communicator` or `ProcessGroupNCCL._comm_ptr()`).

Build and checks: `projects/rccl/contrib/nccl_m2n/scripts/README.md`.
