// MIT License
//
// Copyright (c) 2023-2026 Advanced Micro Devices, Inc. All rights reserved.
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, subject to the conditions in the MIT
// license text carried by the sibling platform backends.

#include "lib/rocprofiler-sdk/platform/virtio/agent.hpp"

#include "lib/common/logging.hpp"
#include "lib/common/string_entry.hpp"
#include "lib/common/utility.hpp"
#include "lib/rocprofiler-sdk/agent.hpp"

#include <rocprofiler-sdk/agent.h>
#include <rocprofiler-sdk/fwd.h>

#include <fmt/core.h>
#include <fmt/format.h>

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <limits>
#include <random>
#include <system_error>
#include <vector>

namespace rocprofiler
{
namespace platform
{
namespace virtio
{
namespace
{
using ::rocprofiler::agent::update_agent_runtime_visibility;
namespace fs = std::filesystem;

// Random per-process offset applied to rocprofiler_agent_id_t.handle. Kept
// identical to the other platform backends so agent IDs are non-stable across
// runs and downstream code cannot treat them as ordinals.
uint64_t
get_agent_offset()
{
    static const uint64_t _v = []() {
        auto gen = std::mt19937{std::random_device{}()};
        auto rng = std::uniform_int_distribution<uint64_t>{std::numeric_limits<uint8_t>::max(),
                                                           std::numeric_limits<uint16_t>::max()};
        return rng(gen);
    }();
    return _v;
}

// A virtio-gpu render node (driver "virtio_gpu") is the guest transport for
// ROCm-over-virtio. Detect it via sysfs so the backend stays dependency-free
// (no libdrm), mirroring how the wsl backend probes for /dev/dxg.
bool
detect_virtio_gpu()
{
    std::error_code ec;
    auto             it = fs::directory_iterator{fs::path{"/sys/class/drm"}, ec};
    if(ec) return false;
    for(const auto& entry : it)
    {
        const auto node = entry.path().filename().string();
        if(node.rfind("renderD", 0) != 0) continue;
        auto target = fs::read_symlink(entry.path() / "device" / "driver", ec);
        if(ec) continue;
        if(target.filename().string() == "virtio_gpu") return true;
    }
    return false;
}
}  // namespace

bool
is_available()
{
    static const bool _v = detect_virtio_gpu();
    return _v;
}

std::vector<unique_agent_t>
enumerate()
{
    std::vector<unique_agent_t> out;

    if(!is_available())
    {
        ROCP_WARNING << "virtio::enumerate: no virtio-gpu render node; returning empty topology";
        return out;
    }

    // Simple first version: the virtio-gpu HSAKMT capset does not expose GPU
    // identity (device id / gfx version), so publish a single identity-level
    // GPU agent with topology fields left empty, mirroring the wsl backend.
    // TODO: enrich (SIMD/CU/gfx/caches) via the virtio thunk GET_NODE_PROP.
    auto info                 = common::init_public_api_struct(rocprofiler_agent_t{});
    info.type                 = ROCPROFILER_AGENT_TYPE_GPU;
    info.logical_node_id      = 0;
    info.node_id              = 0;
    info.id.handle            = get_agent_offset();
    info.logical_node_type_id = 0;

    info.vendor_id      = 0x1002;  // AMD
    info.device_id      = 0;       // not exposed over the virtio capset yet
    info.location_id    = 0;
    info.domain         = 0;
    info.local_mem_size = 0;
    info.num_xcc        = 1;

    info.product_name = common::get_string_entry("AMD GPU (virtio-gpu)")->c_str();
    info.vendor_name  = common::get_string_entry("AMD")->c_str();
    info.name         = info.product_name;
    info.model_name   = common::get_string_entry("")->c_str();

    info.mem_banks_count = 0;
    info.caches_count    = 0;
    info.io_links_count  = 0;
    info.mem_banks       = nullptr;
    info.caches          = nullptr;
    info.io_links        = nullptr;
    std::memset(&info.uuid.bytes, 0, sizeof(info.uuid.bytes));

    update_agent_runtime_visibility(info);

    ROCP_INFO << "virtio::enumerate: published 1 identity-level GPU agent (virtio-gpu)";

    out.emplace_back(new rocprofiler_agent_t{info}, [](rocprofiler_agent_t* p) {
        if(p)
        {
            delete[] p->mem_banks;
            delete[] p->caches;
            delete[] p->io_links;
        }
        delete p;
    });

    return out;
}

}  // namespace virtio
}  // namespace platform
}  // namespace rocprofiler
