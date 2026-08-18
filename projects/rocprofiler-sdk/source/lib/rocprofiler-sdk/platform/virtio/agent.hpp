// MIT License
//
// Copyright (c) 2023-2026 Advanced Micro Devices, Inc. All rights reserved.
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, subject to the conditions in the MIT
// license text carried by the sibling platform backends.

#pragma once

#include "lib/rocprofiler-sdk/platform/agent.hpp"

#include <string_view>
#include <vector>

namespace rocprofiler
{
namespace platform
{
namespace virtio
{
// True if a virtio-gpu render node (driver "virtio_gpu") is present -- the
// guest-side transport for ROCm-over-virtio. Result is cached on first call.
bool
is_available();

// Simple first version mirroring the wsl backend: the virtio-gpu HSAKMT capset
// does not expose GPU identity, so this publishes a single identity-level GPU
// agent with topology fields left empty. Returns empty if virtio-gpu is absent.
std::vector<unique_agent_t>
enumerate();

constexpr std::string_view name = "virtio-gpu";
}  // namespace virtio
}  // namespace platform
}  // namespace rocprofiler
