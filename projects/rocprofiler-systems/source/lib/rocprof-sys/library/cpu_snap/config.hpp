// Copyright (c) Advanced Micro Devices, Inc.
// SPDX-License-Identifier: MIT

#pragma once

#include "logger/debug.hpp"

#include <cpu_snap/sampling/backend_kind.hpp>
#include <cpu_snap/sampling/clock_kind.hpp>
#include <cpu_snap/sampling/logger.hpp>
#include <cpu_snap/sampling/sampler.hpp>

#include <fmt/format.h>

#include <cmath>
#include <cstdint>
#include <stdexcept>

namespace rocprofsys::cpu_snap
{
/// rocprof-sys setting values the cpu-snap sampler is built from.
struct sampler_settings
{
    bool   cputime  = false;
    bool   realtime = false;
    double freq_hz  = 0.0;
};

/**
 * @brief cpu-snap configuration for @p settings, without sink and logger.
 *
 * Wall time (@c realtime) uses the signal trigger. CPU time, also the default when
 * neither clock is set, uses the perf trigger. Timestamps use CLOCK_BOOTTIME, the
 * rocprof-sys timeline clock.
 *
 * @throws std::logic_error When both clocks are set: configure_settings() rejects
 *         that combination for the cpu-snap engine.
 */
[[nodiscard]] inline ::cpu_snap::sampling::sampler::config
make_sampler_config(const sampler_settings& settings)
{
    if(settings.cputime && settings.realtime)
    {
        throw std::logic_error("cpu-snap: CPU time and wall time are both set; "
                               "configure_settings() should have rejected this");
    }

    auto const freq_hz = static_cast<std::uint32_t>(std::lround(settings.freq_hz));

    auto cfg    = ::cpu_snap::sampling::sampler::config{};
    cfg.backend = settings.realtime ? ::cpu_snap::sampling::backend_kind::signal
                                    : ::cpu_snap::sampling::backend_kind::perf;
    cfg.perf.sample_freq_hz   = freq_hz;
    cfg.signal.sample_freq_hz = freq_hz;
    cfg.clock                 = ::cpu_snap::sampling::clock_kind::boottime;
    return cfg;
}

/// Forwards a cpu-snap log record to the rocprof-sys logger.
inline void
forward_log_record(const ::cpu_snap::sampling::log_record& record)
{
    using ::cpu_snap::sampling::log_level;

    switch(record.level)
    {
        case log_level::debug: LOG_DEBUG("cpu-snap: {}", record.message); return;
        case log_level::info: LOG_INFO("cpu-snap: {}", record.message); return;
        case log_level::warn: LOG_WARNING("cpu-snap: {}", record.message); return;
        case log_level::error: LOG_ERROR("cpu-snap: {}", record.message); return;
        default:
            throw std::logic_error(fmt::format("cpu-snap log_level {} not handled",
                                               static_cast<int>(record.level)));
    }
}
}  // namespace rocprofsys::cpu_snap
