// Copyright (c) Advanced Micro Devices, Inc.
// SPDX-License-Identifier: MIT

#include "backends/sqlite_backend.hpp"
#include "backends/sqlite_backend_impl.hpp"

#include <gtest/gtest.h>

#include <filesystem>
#include <string>

namespace
{

using profiler_hub::data_storage::sqlite_backend;

class sqlite_backend_discover_uuids_test : public ::testing::Test
{
protected:
    void SetUp() override
    {
        const auto* test_info = ::testing::UnitTest::GetInstance()->current_test_info();
        m_db_path             = (std::filesystem::temp_directory_path() /
                     (std::string{ "sqlite_backend_discover_uuids_test_" } +
                      test_info->name() + ".db"))
                        .string();
        std::filesystem::remove(m_db_path);
    }

    void TearDown() override { std::filesystem::remove(m_db_path); }

    void build_database(bool insert_node_row, bool keep_info_node_view) const
    {
        auto backend = sqlite_backend::create(
            m_db_path, m_suffix, sqlite_backend::storage_mode_t::on_disk);
        backend->initialize_schema();
        if(insert_node_row)
        {
            backend->execute("INSERT INTO rocpd_info_node_" + m_suffix +
                             " (guid, hash, machine_id) VALUES ('" + m_guid +
                             "', 1, 'machine-1');");
        }
        if(!keep_info_node_view)
        {
            backend->execute("DROP VIEW rocpd_info_node;");
        }
    }

    [[nodiscard]] std::string reopened_uuid() const
    {
        return sqlite_backend::create(
                   m_db_path, "caller-uuid", sqlite_backend::storage_mode_t::on_disk)
            ->get_uuid();
    }

    std::string       m_db_path;
    const std::string m_guid   = "00001eca-d4de-74de-b70e-c34ecf8c3a87";
    const std::string m_suffix = "00001eca_d4de_74de_b70e_c34ecf8c3a87";
};

TEST_F(sqlite_backend_discover_uuids_test,
       create_keeps_whole_hyphenated_guid_when_info_node_view_exists)
{
    build_database(true, true);
    EXPECT_EQ(reopened_uuid(), m_suffix);
}

TEST_F(sqlite_backend_discover_uuids_test,
       create_keeps_whole_hyphenated_guid_when_info_node_view_is_absent)
{
    build_database(true, false);
    EXPECT_EQ(reopened_uuid(), m_suffix);
}

TEST_F(sqlite_backend_discover_uuids_test,
       create_keeps_whole_hyphenated_guid_when_info_node_view_has_no_rows)
{
    build_database(false, true);
    EXPECT_EQ(reopened_uuid(), m_suffix);
}

}  // namespace
