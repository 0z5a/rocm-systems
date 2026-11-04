// Copyright (c) Advanced Micro Devices, Inc.
// SPDX-License-Identifier: MIT

#include "profiler_hub_ctx.hpp"

#include "profiler-hub/cpp/storage.hpp"
#include "profiler-hub/cpp/writer.hpp"
#include "profiler-hub/cpp/writer_types.hpp"

#include <gtest/gtest.h>

#include <filesystem>
#include <memory>
#include <string>

namespace
{

using namespace profiler_hub;

class ph_ctx_test : public ::testing::Test
{
protected:
    void SetUp() override
    {
        const auto* test_info = ::testing::UnitTest::GetInstance()->current_test_info();
        m_db_path             = (std::filesystem::temp_directory_path() /
                     (std::string{ "ph_ctx_test_" } + test_info->name() + ".db"))
                        .string();
        std::filesystem::remove(m_db_path);
    }

    void TearDown() override { std::filesystem::remove(m_db_path); }

    [[nodiscard]] std::unique_ptr<writer_t> make_writer() const
    {
        return std::make_unique<writer_t>(std::make_unique<storage_t>(m_db_path, m_uuid));
    }

    // Registers node/process/thread, a track matching that topology, and one
    // region event on it, so ph_ctx sees exactly one non-empty track.
    void seed_track_with_one_event(writer_t& writer) const
    {
        const writer_types::node_info_t node_info{ 1, 42, "machine-1" };
        writer.register_node_info(node_info);

        writer_types::process_info_t process_info;
        process_info.pid     = 100;
        process_info.node_id = 1;
        writer.register_process_info(process_info);

        writer_types::thread_info_t thread_info;
        thread_info.thread_id  = 200;
        thread_info.node_id    = 1;
        thread_info.process_id = 100;
        writer.register_thread_info(thread_info);

        writer_types::track_info_t track_info;
        track_info.node_id    = 1;
        track_info.process_id = 100;
        track_info.thread_id  = 200;
        writer.register_track_info(track_info);

        writer_types::trace_environment_t trace_environment;
        trace_environment.node_id    = 1;
        trace_environment.process_id = 100;
        trace_environment.thread_id  = 200;

        writer_types::event_data_t event_data;
        event_data.stack_id = 1;

        writer_types::region_data_t region_data;
        region_data.name            = "test-region";
        region_data.start_timestamp = 1000;
        region_data.end_timestamp   = 2000;
        region_data.event           = event_data;
        writer.insert_region_data(region_data, trace_environment);
    }

    void seed_track_with_two_events(writer_t& writer) const
    {
        const writer_types::node_info_t node_info{ 1, 42, "machine-1" };
        writer.register_node_info(node_info);

        writer_types::process_info_t process_info;
        process_info.pid     = 100;
        process_info.node_id = 1;
        writer.register_process_info(process_info);

        writer_types::thread_info_t thread_info;
        thread_info.thread_id  = 200;
        thread_info.node_id    = 1;
        thread_info.process_id = 100;
        writer.register_thread_info(thread_info);

        writer_types::track_info_t track_info;
        track_info.node_id    = 1;
        track_info.process_id = 100;
        track_info.thread_id  = 200;
        writer.register_track_info(track_info);

        writer_types::trace_environment_t trace_environment;
        trace_environment.node_id    = 1;
        trace_environment.process_id = 100;
        trace_environment.thread_id  = 200;

        writer_types::event_data_t event_data;
        event_data.stack_id = 1;

        writer_types::region_data_t region_a;
        region_a.name            = "region-a";
        region_a.start_timestamp = 1000;
        region_a.end_timestamp   = 2000;
        region_a.event           = event_data;
        writer.insert_region_data(region_a, trace_environment);

        writer_types::region_data_t region_b;
        region_b.name            = "region-b";
        region_b.start_timestamp = 5000;
        region_b.end_timestamp   = 6000;
        region_b.event           = event_data;
        writer.insert_region_data(region_b, trace_environment);
    }

    std::string m_db_path;
    // Embedded verbatim into unquoted SQL table names by insert_statements, so it
    // must be a valid identifier fragment - no hyphens.
    std::string m_uuid = "testuuid0000";
};

TEST_F(ph_ctx_test, construction_populates_schema_version_and_node)
{
    auto writer = make_writer();
    seed_track_with_one_event(*writer);
    writer->flush_in_memory_data_to_disk();
    writer.reset();

    ph_ctx ctx{ m_db_path };

    const auto version = ctx.get_storage_version();
    EXPECT_GT(version.major + version.minor + version.patch, 0U);

    const auto node = ctx.get_node();
    EXPECT_EQ(node.info.id, 1U);
    EXPECT_STREQ(node.info.machine_id, "machine-1");
}

TEST_F(ph_ctx_test, construction_with_no_nodes_succeeds_with_empty_node_info)
{
    auto writer = make_writer();
    writer->flush_in_memory_data_to_disk();
    writer.reset();

    ph_ctx ctx{ m_db_path };

    const auto node = ctx.get_node();
    EXPECT_EQ(node.info.id, 0U);
    ASSERT_NE(node.info.machine_id, nullptr);
    EXPECT_STREQ(node.info.machine_id, "");
    ASSERT_NE(node.info.system_name, nullptr);
    EXPECT_STREQ(node.info.system_name, "");
    ASSERT_NE(node.info.hostname, nullptr);
    EXPECT_STREQ(node.info.hostname, "");
    ASSERT_NE(node.info.release, nullptr);
    EXPECT_STREQ(node.info.release, "");
    ASSERT_NE(node.info.version, nullptr);
    EXPECT_STREQ(node.info.version, "");
    ASSERT_NE(node.info.hardware_name, nullptr);
    EXPECT_STREQ(node.info.hardware_name, "");
    ASSERT_NE(node.info.domain_name, nullptr);
    EXPECT_STREQ(node.info.domain_name, "");
}

TEST_F(ph_ctx_test, get_track_list_returns_seeded_non_empty_track)
{
    auto writer = make_writer();
    seed_track_with_one_event(*writer);
    writer->flush_in_memory_data_to_disk();
    writer.reset();

    ph_ctx ctx{ m_db_path };

    const auto tracks = ctx.get_track_list();
    ASSERT_EQ(tracks.list_size, 1U);
    EXPECT_EQ(tracks.tracks[0].nid, 1U);
    EXPECT_EQ(tracks.tracks[0].pid, 100U);
    EXPECT_EQ(tracks.tracks[0].tid, 200U);
    EXPECT_EQ(tracks.tracks[0].event_count, 1U);
}

TEST_F(ph_ctx_test, has_track_reflects_seeded_and_unknown_ids)
{
    auto writer = make_writer();
    seed_track_with_one_event(*writer);
    writer->flush_in_memory_data_to_disk();
    writer.reset();

    ph_ctx ctx{ m_db_path };

    const auto tracks = ctx.get_track_list();
    ASSERT_EQ(tracks.list_size, 1U);

    EXPECT_TRUE(ctx.has_track(tracks.tracks[0].id));
    EXPECT_FALSE(ctx.has_track(tracks.tracks[0].id + 1000));
}

TEST_F(ph_ctx_test, get_track_events_returns_event_for_seeded_track)
{
    auto writer = make_writer();
    seed_track_with_one_event(*writer);
    writer->flush_in_memory_data_to_disk();
    writer.reset();

    ph_ctx ctx{ m_db_path };

    const auto tracks = ctx.get_track_list();
    ASSERT_EQ(tracks.list_size, 1U);

    const auto events = ctx.get_track_events(tracks.tracks[0].id, 0, 0);
    ASSERT_EQ(events.list_size, 1U);
    EXPECT_EQ(events.events[0].start, 1000U);
    EXPECT_EQ(events.events[0].end, 2000U);
}

TEST_F(ph_ctx_test, get_track_events_returns_empty_for_unknown_track)
{
    auto writer = make_writer();
    seed_track_with_one_event(*writer);
    writer->flush_in_memory_data_to_disk();
    writer.reset();

    ph_ctx ctx{ m_db_path };

    const auto events = ctx.get_track_events(999999, 0, 0);
    EXPECT_EQ(events.list_size, 0U);
    EXPECT_EQ(events.events, nullptr);
}

TEST_F(ph_ctx_test, get_track_events_start_only_filters_to_overlapping_events)
{
    auto writer = make_writer();
    seed_track_with_two_events(*writer);
    writer->flush_in_memory_data_to_disk();
    writer.reset();

    ph_ctx ctx{ m_db_path };

    const auto tracks = ctx.get_track_list();
    ASSERT_EQ(tracks.list_size, 1U);

    const auto events = ctx.get_track_events(tracks.tracks[0].id, 3000, 0);
    ASSERT_EQ(events.list_size, 1U);
    EXPECT_EQ(events.events[0].start, 5000U);
    EXPECT_EQ(events.events[0].end, 6000U);
}

TEST_F(ph_ctx_test, get_track_events_end_only_filters_to_overlapping_events)
{
    auto writer = make_writer();
    seed_track_with_two_events(*writer);
    writer->flush_in_memory_data_to_disk();
    writer.reset();

    ph_ctx ctx{ m_db_path };

    const auto tracks = ctx.get_track_list();
    ASSERT_EQ(tracks.list_size, 1U);

    const auto events = ctx.get_track_events(tracks.tracks[0].id, 0, 3000);
    ASSERT_EQ(events.list_size, 1U);
    EXPECT_EQ(events.events[0].start, 1000U);
    EXPECT_EQ(events.events[0].end, 2000U);
}

TEST_F(ph_ctx_test, get_track_events_both_bounds_filters_to_overlapping_events)
{
    auto writer = make_writer();
    seed_track_with_two_events(*writer);
    writer->flush_in_memory_data_to_disk();
    writer.reset();

    ph_ctx ctx{ m_db_path };

    const auto tracks = ctx.get_track_list();
    ASSERT_EQ(tracks.list_size, 1U);

    const auto events = ctx.get_track_events(tracks.tracks[0].id, 1200, 1800);
    ASSERT_EQ(events.list_size, 1U);
    EXPECT_EQ(events.events[0].start, 1000U);
    EXPECT_EQ(events.events[0].end, 2000U);
}

TEST_F(ph_ctx_test, get_track_events_no_bounds_returns_all_events)
{
    auto writer = make_writer();
    seed_track_with_two_events(*writer);
    writer->flush_in_memory_data_to_disk();
    writer.reset();

    ph_ctx ctx{ m_db_path };

    const auto tracks = ctx.get_track_list();
    ASSERT_EQ(tracks.list_size, 1U);

    const auto events = ctx.get_track_events(tracks.tracks[0].id, 0, 0);
    EXPECT_EQ(events.list_size, 2U);
}

TEST_F(ph_ctx_test, get_track_events_returns_memory_copies_written_without_a_queue)
{
    auto writer = make_writer();

    const writer_types::node_info_t node_info{ 1, 42, "machine-1" };
    writer->register_node_info(node_info);

    writer_types::process_info_t process_info;
    process_info.pid     = 100;
    process_info.node_id = 1;
    writer->register_process_info(process_info);

    writer_types::agent_info_t agent_info;
    agent_info.unique_id.agent_type = "GPU";
    agent_info.unique_id.type_index = 0;
    agent_info.uuid                 = 123;
    agent_info.node_id              = 1;
    agent_info.process_id           = 100;
    writer->register_agent_info(agent_info);

    writer_types::trace_environment_t trace_environment;
    trace_environment.node_id    = 1;
    trace_environment.process_id = 100;

    for(const uint64_t start : { 1000U, 5000U })
    {
        writer_types::memory_copy_data_t copy;
        copy.name            = "hipMemcpy";
        copy.region_name     = "hipMemcpy";
        copy.start_timestamp = start;
        copy.end_timestamp   = start + 500;
        copy.dst_agent_id    = agent_info.unique_id;
        copy.size            = 4096;
        copy.event           = writer_types::event_data_t{};
        writer->insert_memory_copy_data(copy, trace_environment);
    }
    writer->flush_in_memory_data_to_disk();
    writer.reset();

    ph_ctx ctx{ m_db_path };

    const auto        tracks     = ctx.get_track_list();
    const ph_track_t* copy_track = nullptr;
    for(uint32_t i = 0; i < tracks.list_size; ++i)
    {
        if(tracks.tracks[i].category == PH_TRACK_CATEGORY_MEMORY_COPY_AGENT_QUEUE)
        {
            copy_track = &tracks.tracks[i];
        }
    }
    ASSERT_NE(copy_track, nullptr);
    EXPECT_EQ(copy_track->queue_id, 0U);
    EXPECT_EQ(copy_track->event_count, 2U);

    const auto events = ctx.get_track_events(copy_track->id, 0, 0);
    EXPECT_EQ(events.list_size, copy_track->event_count);

    const auto windowed = ctx.get_track_events(copy_track->id, 4800, 6000);
    ASSERT_EQ(windowed.list_size, 1U);
    EXPECT_EQ(windowed.events[0].start, 5000U);
}

TEST_F(ph_ctx_test, get_track_samples_returns_counter_events_written_with_a_sample)
{
    auto writer = make_writer();
    seed_track_with_one_event(*writer);

    writer_types::agent_info_t agent_info;
    agent_info.unique_id.agent_type = "GPU";
    agent_info.unique_id.type_index = 0;
    agent_info.uuid                 = 123;
    agent_info.node_id              = 1;
    agent_info.process_id           = 100;
    writer->register_agent_info(agent_info);

    writer_types::pmc_info_t pmc_info;
    pmc_info.unique_id.name     = "occupancy";
    pmc_info.unique_id.agent_id = agent_info.unique_id;
    pmc_info.symbol             = "occupancy";
    pmc_info.node_id            = 1;
    pmc_info.process_id         = 100;
    writer->register_pmc_info(pmc_info);

    writer_types::track_info_t sample_track;
    sample_track.node_id    = 1;
    sample_track.process_id = 100;
    sample_track.thread_id  = 200;

    for(const uint64_t timestamp : { 1500U, 5500U })
    {
        writer_types::pmc_event_data_t pmc_event;
        pmc_event.event            = writer_types::event_data_t{};
        pmc_event.value            = static_cast<double>(timestamp) / 1000.0;
        pmc_event.sample.timestamp = timestamp;
        pmc_event.sample.track     = sample_track;
        writer->insert_pmc_event_data(pmc_event, pmc_info.unique_id);
    }
    writer->flush_in_memory_data_to_disk();
    writer.reset();

    ph_ctx ctx{ m_db_path };

    const auto        tracks    = ctx.get_track_list();
    const ph_track_t* pmc_track = nullptr;
    uint32_t          pmc_count = 0;
    for(uint32_t i = 0; i < tracks.list_size; ++i)
    {
        if(tracks.tracks[i].category == PH_TRACK_CATEGORY_PMC_AGENT)
        {
            pmc_track = &tracks.tracks[i];
            ++pmc_count;
        }
    }
    ASSERT_EQ(pmc_count, 1U);
    EXPECT_STREQ(pmc_track->track_name, "occupancy");
    EXPECT_EQ(pmc_track->event_count, 2U);
    EXPECT_EQ(pmc_track->start_ts, 1500U);
    EXPECT_EQ(pmc_track->end_ts, 5500U);

    const auto samples = ctx.get_track_samples(pmc_track->id, 0, 0);
    EXPECT_EQ(samples.list_size, pmc_track->event_count);

    const auto windowed = ctx.get_track_samples(pmc_track->id, 5000, 6000);
    ASSERT_EQ(windowed.list_size, 1U);
    EXPECT_EQ(windowed.samples[0].timestamp, 5500U);
    EXPECT_DOUBLE_EQ(windowed.samples[0].value, 5.5);
}

TEST_F(ph_ctx_test, get_track_samples_returns_empty_for_non_pmc_track)
{
    auto writer = make_writer();
    seed_track_with_one_event(*writer);
    writer->flush_in_memory_data_to_disk();
    writer.reset();

    ph_ctx ctx{ m_db_path };

    const auto tracks = ctx.get_track_list();
    ASSERT_EQ(tracks.list_size, 1U);

    const auto samples = ctx.get_track_samples(tracks.tracks[0].id, 0, 0);
    EXPECT_EQ(samples.list_size, 0U);
}

}  // namespace
