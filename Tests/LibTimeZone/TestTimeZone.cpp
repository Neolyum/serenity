/*
 * Copyright (c) 2022, Tim Flynn <trflynn89@pm.me>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <LibTest/TestCase.h>

#include <AK/StringView.h>
#include <AK/Time.h>
#include <LibTimeZone/TimeZone.h>

#if ENABLE_TIME_ZONE_DATA

#    include <LibTimeZone/TimeZoneData.h>

TEST_CASE(time_zone_from_string)
{
    EXPECT_EQ(TimeZone::time_zone_from_string("America/New_York"sv), TimeZone::TimeZone::America_New_York);
    EXPECT_EQ(TimeZone::time_zone_from_string("Europe/Paris"sv), TimeZone::TimeZone::Europe_Paris);
    EXPECT_EQ(TimeZone::time_zone_from_string("Etc/GMT+2"sv), TimeZone::TimeZone::Etc_GMT_Ahead_2);
    EXPECT_EQ(TimeZone::time_zone_from_string("Etc/GMT-5"sv), TimeZone::TimeZone::Etc_GMT_Behind_5);

    EXPECT(!TimeZone::time_zone_from_string("I don't exist"sv).has_value());
}

TEST_CASE(time_zone_from_string_link)
{
    auto test_link = [](auto tz1, auto tz2) {
        auto result1 = TimeZone::time_zone_from_string(tz1);
        EXPECT(result1.has_value());

        auto result2 = TimeZone::time_zone_from_string(tz2);
        EXPECT(result2.has_value());

        EXPECT_EQ(*result1, *result2);
    };

    test_link("America/New_York"sv, "US/Eastern"sv);

    test_link("Etc/GMT"sv, "GMT"sv);
    test_link("Etc/GMT+0"sv, "GMT"sv);
    test_link("Etc/GMT-0"sv, "GMT"sv);

    test_link("Etc/UTC"sv, "UTC"sv);
    test_link("Etc/Universal"sv, "UTC"sv);
    test_link("Universal"sv, "UTC"sv);
}

TEST_CASE(case_insensitive_time_zone_from_string)
{
    EXPECT_EQ(TimeZone::time_zone_from_string("UTC"sv), TimeZone::TimeZone::UTC);
    EXPECT_EQ(TimeZone::time_zone_from_string("utc"sv), TimeZone::TimeZone::UTC);
    EXPECT_EQ(TimeZone::time_zone_from_string("uTc"sv), TimeZone::TimeZone::UTC);
}

TEST_CASE(time_zone_to_string)
{
    EXPECT_EQ(TimeZone::time_zone_to_string(TimeZone::TimeZone::America_New_York), "America/New_York"sv);
    EXPECT_EQ(TimeZone::time_zone_to_string(TimeZone::TimeZone::Europe_Paris), "Europe/Paris"sv);
    EXPECT_EQ(TimeZone::time_zone_to_string(TimeZone::TimeZone::Etc_GMT_Ahead_2), "Etc/GMT+2"sv);
    EXPECT_EQ(TimeZone::time_zone_to_string(TimeZone::TimeZone::Etc_GMT_Behind_5), "Etc/GMT-5"sv);
}

TEST_CASE(time_zone_to_string_link)
{
    EXPECT_EQ(TimeZone::time_zone_to_string(TimeZone::TimeZone::Etc_UTC), "Etc/UTC"sv);
    EXPECT_EQ(TimeZone::time_zone_to_string(TimeZone::TimeZone::UTC), "Etc/UTC"sv);
    EXPECT_EQ(TimeZone::time_zone_to_string(TimeZone::TimeZone::Universal), "Etc/UTC"sv);
    EXPECT_EQ(TimeZone::time_zone_to_string(TimeZone::TimeZone::Etc_Universal), "Etc/UTC"sv);
}

TEST_CASE(canonicalize_time_zone)
{
    EXPECT_EQ(TimeZone::canonicalize_time_zone("America/New_York"sv), "America/New_York"sv);
    EXPECT_EQ(TimeZone::canonicalize_time_zone("AmErIcA/NeW_YoRk"sv), "America/New_York"sv);

    EXPECT_EQ(TimeZone::canonicalize_time_zone("UTC"sv), "UTC"sv);
    EXPECT_EQ(TimeZone::canonicalize_time_zone("GMT"sv), "UTC"sv);
    EXPECT_EQ(TimeZone::canonicalize_time_zone("GMT+0"sv), "UTC"sv);
    EXPECT_EQ(TimeZone::canonicalize_time_zone("GMT-0"sv), "UTC"sv);
    EXPECT_EQ(TimeZone::canonicalize_time_zone("Etc/UTC"sv), "UTC"sv);
    EXPECT_EQ(TimeZone::canonicalize_time_zone("Etc/GMT"sv), "UTC"sv);

    EXPECT(!TimeZone::canonicalize_time_zone("I don't exist"sv).has_value());
}

TEST_CASE(get_time_zone_offset)
{
    auto offset = [](i64 sign, i64 hours, i64 minutes, i64 seconds) {
        return sign * ((hours * 3600) + (minutes * 60) + seconds);
    };

    auto test_offset = [](auto time_zone, i64 time, i64 expected_offset) {
        auto actual_offset = TimeZone::get_time_zone_offset(time_zone, AK::Time::from_seconds(time));
        VERIFY(actual_offset.has_value());
        EXPECT_EQ(*actual_offset, expected_offset);
    };

    test_offset("America/Chicago"sv, -2717668237, offset(-1, 5, 50, 36)); // Sunday, November 18, 1883 12:09:23 PM
    test_offset("America/Chicago"sv, -2717668236, offset(-1, 6, 00, 00)); // Sunday, November 18, 1883 12:09:24 PM
    test_offset("America/Chicago"sv, -1067810460, offset(-1, 6, 00, 00)); // Sunday, March 1, 1936 1:59:00 AM
    test_offset("America/Chicago"sv, -1067810400, offset(-1, 5, 00, 00)); // Sunday, March 1, 1936 2:00:00 AM
    test_offset("America/Chicago"sv, -1045432860, offset(-1, 5, 00, 00)); // Sunday, November 15, 1936 1:59:00 AM
    test_offset("America/Chicago"sv, -1045432800, offset(-1, 6, 00, 00)); // Sunday, November 15, 1936 2:00:00 AM

    test_offset("Europe/London"sv, -3852662401, offset(-1, 0, 01, 15)); // Tuesday, November 30, 1847 11:59:59 PM
    test_offset("Europe/London"sv, -3852662400, offset(+1, 0, 00, 00)); // Wednesday, December 1, 1847 12:00:00 AM
    test_offset("Europe/London"sv, -37238401, offset(+1, 0, 00, 00));   // Saturday, October 26, 1968 11:59:59 PM
    test_offset("Europe/London"sv, -37238400, offset(+1, 1, 00, 00));   // Sunday, October 27, 1968 12:00:00 AM
    test_offset("Europe/London"sv, 57722399, offset(+1, 1, 00, 00));    // Sunday, October 31, 1971 1:59:59 AM
    test_offset("Europe/London"sv, 57722400, offset(+1, 0, 00, 00));    // Sunday, October 31, 1971 2:00:00 AM

    test_offset("UTC"sv, -1641846268, offset(+1, 0, 00, 00));
    test_offset("UTC"sv, 0, offset(+1, 0, 00, 00));
    test_offset("UTC"sv, 1641846268, offset(+1, 0, 00, 00));

    test_offset("Etc/GMT+4"sv, -1641846268, offset(-1, 4, 00, 00));
    test_offset("Etc/GMT+5"sv, 0, offset(-1, 5, 00, 00));
    test_offset("Etc/GMT+6"sv, 1641846268, offset(-1, 6, 00, 00));

    test_offset("Etc/GMT-12"sv, -1641846268, offset(+1, 12, 00, 00));
    test_offset("Etc/GMT-13"sv, 0, offset(+1, 13, 00, 00));
    test_offset("Etc/GMT-14"sv, 1641846268, offset(+1, 14, 00, 00));

    EXPECT(!TimeZone::get_time_zone_offset("I don't exist"sv, {}).has_value());
}

#endif
