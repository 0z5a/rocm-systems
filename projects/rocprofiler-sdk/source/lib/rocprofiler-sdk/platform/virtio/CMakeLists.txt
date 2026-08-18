#
# virtio-gpu agent enumeration. Compiled unconditionally on Linux builds;
# runtime selection (agent.cpp) picks this path only when a virtio-gpu render
# node is present and neither the KFD-sysfs nor WSL/DXCore paths matched.
#

set(ROCPROFILER_PLATFORM_VIRTIO_HEADERS agent.hpp)
set(ROCPROFILER_PLATFORM_VIRTIO_SOURCES agent.cpp)

target_sources(rocprofiler-sdk-object-library PRIVATE ${ROCPROFILER_PLATFORM_VIRTIO_SOURCES}
                                                      ${ROCPROFILER_PLATFORM_VIRTIO_HEADERS})
