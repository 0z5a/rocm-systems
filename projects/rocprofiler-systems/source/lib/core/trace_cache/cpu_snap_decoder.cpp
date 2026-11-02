// Copyright (c) Advanced Micro Devices, Inc.
// SPDX-License-Identifier: MIT

#include "core/trace_cache/cpu_snap_decoder.hpp"
#include "core/trace_cache/sample_type.hpp"
#include "logger/debug.hpp"
#include "rocprofiler-systems/categories.h"

#include <cpu_snap/core/reader.hpp>
#include <cpu_snap/core/records.hpp>

#include <fmt/format.h>

#include <cstdint>
#include <memory>
#include <ranges>
#include <span>
#include <stdexcept>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace rocprofsys::trace_cache
{
namespace
{
constexpr std::string_view k_empty_extdata = "{}";
// Name of category::timer_sampling (core/categories.hpp), which the perfetto processor
// matches to pick the track category.
constexpr std::string_view k_timer_sampling_category = "timer_sampling";
}  // namespace

struct cpu_snap_decoder::impl
{
    explicit impl(emit_fn emit_in)
    : emit{ std::move(emit_in) }
    {
        reader.on_event([this](const ::cpu_snap::core::record_variant& record) {
            on_record(record);
        });
    }

    void on_record(const ::cpu_snap::core::record_variant& record)
    {
        using ::cpu_snap::core::record_type;

        const auto type = ::cpu_snap::core::type_of(record);
        switch(type)
        {
            case record_type::sample:
                on_sample(std::get<::cpu_snap::core::sample_record>(record));
                return;
            case record_type::lost:
                lost_samples +=
                    std::get<::cpu_snap::core::lost_record>(record).lost_count_delta;
                return;
            // The reader keeps the frame trie itself; the rest carries no slice.
            case record_type::new_node:
            case record_type::checkpoint:
            case record_type::maps_init:
            case record_type::maps_delta:
            case record_type::eof: return;
            default:
                throw std::logic_error(fmt::format("cpu-snap record_type {} not handled",
                                                   static_cast<int>(type)));
        }
    }

    void on_sample(const ::cpu_snap::core::sample_record& sample)
    {
        auto const [previous, is_first] =
            previous_timestamp.try_emplace(sample.tid, sample.timestamp_ns);
        auto const start = std::exchange(previous->second, sample.timestamp_ns);
        // Leaf 0 is an intentionally empty stack.
        if(is_first || sample.leaf_node_id == 0)
        {
            return;
        }

        auto const frames = reader.resolve_stack(sample.leaf_node_id);
        if(frames.empty())
        {
            ++unresolved_samples;
            return;
        }

        auto const track_name = fmt::format("Thread {}", sample.tid);
        for(auto const address : frames | std::views::reverse)
        {
            auto const name = fmt::format("0x{:x}", address);
            emit(backtrace_region_sample{
                static_cast<std::uint32_t>(ROCPROFSYS_CATEGORY_TIMER_SAMPLING),
                sample.tid, track_name, name, start, sample.timestamp_ns,
                k_timer_sampling_category, std::string_view{}, std::string_view{},
                k_empty_extdata });
        }
    }

    emit_fn                  emit;
    ::cpu_snap::core::reader reader;
    // Timestamp of each thread's previous sample, by tid.
    std::unordered_map<std::uint32_t, std::uint64_t> previous_timestamp;
    std::uint64_t                                    lost_samples       = 0;
    std::uint64_t                                    unresolved_samples = 0;
};

cpu_snap_decoder::cpu_snap_decoder(emit_fn emit)
: m_impl{ std::make_unique<impl>(std::move(emit)) }
{}

cpu_snap_decoder::~cpu_snap_decoder() = default;

void
cpu_snap_decoder::feed(const cpu_snap_chunk_sample& chunk)
{
    m_impl->reader.feed(std::as_bytes(std::span{ chunk.bytes }));
}

void
cpu_snap_decoder::report_losses() const
{
    if(m_impl->lost_samples > 0)
    {
        LOG_WARNING("cpu-snap: {} samples lost by the kernel", m_impl->lost_samples);
    }
    if(m_impl->unresolved_samples > 0)
    {
        LOG_WARNING("cpu-snap: {} samples dropped, their call stack is unknown",
                    m_impl->unresolved_samples);
    }

    if(m_impl->reader.is_fatal())
    {
        LOG_WARNING("cpu-snap: the stream header is unreadable, nothing was decoded");
    }
    const auto& counters = m_impl->reader.counters();
    if(counters.parse_errors > 0 || counters.gap_count > 0 ||
       counters.chunks_rejected > 0)
    {
        LOG_WARNING("cpu-snap: damaged stream: {} parse errors, {} chunk gaps, {} "
                    "rejected chunks",
                    counters.parse_errors, counters.gap_count, counters.chunks_rejected);
    }
}
}  // namespace rocprofsys::trace_cache
