/*
 * Unit tests for vt_string (src/utils/string_utils.cc)
 *
 * This file used to #include string_utils.hh and utility.hh and then call
 * neither -- every assertion exercised std::string from the standard library.
 * Both translation units measured 0% coverage as a result. These tests call the
 * real functions.
 */

#include <catch2/catch_all.hpp>
#include "src/utils/string_utils.hh"

#include <string>
#include <vector>

TEST_CASE("vt_string case conversion", "[string_utils][case]")
{
    SECTION("to_upper and to_lower")
    {
        REQUIRE(vt_string::to_upper("cheeseburger") == "CHEESEBURGER");
        REQUIRE(vt_string::to_lower("CHEESEBURGER") == "cheeseburger");
        REQUIRE(vt_string::to_upper("MiXeD 123") == "MIXED 123");
        REQUIRE(vt_string::to_lower("MiXeD 123") == "mixed 123");
    }

    SECTION("empty input is preserved")
    {
        REQUIRE(vt_string::to_upper("").empty());
        REQUIRE(vt_string::to_lower("").empty());
    }

    SECTION("non-alphabetic characters are untouched")
    {
        REQUIRE(vt_string::to_upper("$12.50 (x2)") == "$12.50 (X2)");
    }

    SECTION("to_title_case capitalises word starts")
    {
        REQUIRE(vt_string::to_title_case("grilled cheese") == "Grilled Cheese");
    }
}

TEST_CASE("vt_string trimming", "[string_utils][trim]")
{
    SECTION("trim removes both ends")
    {
        REQUIRE(vt_string::trim("  padded  ") == "padded");
        REQUIRE(vt_string::trim("\t tabbed \n") == "tabbed");
    }

    SECTION("trim_left and trim_right are one-sided")
    {
        REQUIRE(vt_string::trim_left("  padded  ") == "padded  ");
        REQUIRE(vt_string::trim_right("  padded  ") == "  padded");
    }

    SECTION("a string with no padding is unchanged")
    {
        REQUIRE(vt_string::trim("tight") == "tight");
    }

    SECTION("an all-whitespace string trims to empty")
    {
        REQUIRE(vt_string::trim("   \t\n  ").empty());
    }

    SECTION("normalize_spaces collapses internal runs")
    {
        REQUIRE(vt_string::normalize_spaces("a    b   c") == "a b c");
    }
}

TEST_CASE("vt_string searching", "[string_utils][search]")
{
    SECTION("contains is case sensitive by default")
    {
        REQUIRE(vt_string::contains("Cheeseburger", "burger"));
        REQUIRE_FALSE(vt_string::contains("Cheeseburger", "Burger"));
        REQUIRE(vt_string::contains("Cheeseburger", "Burger", false));
    }

    SECTION("starts_with and ends_with")
    {
        REQUIRE(vt_string::starts_with("check_1234", "check_"));
        REQUIRE_FALSE(vt_string::starts_with("check_1234", "drawer_"));
        REQUIRE(vt_string::ends_with("archive_000012.bak", ".bak"));
        REQUIRE_FALSE(vt_string::ends_with("archive_000012", ".bak"));
    }

    SECTION("empty needles and haystacks")
    {
        REQUIRE(vt_string::starts_with("anything", ""));
        REQUIRE(vt_string::ends_with("anything", ""));
        REQUIRE_FALSE(vt_string::starts_with("", "x"));
    }

    SECTION("compare_ignore_case orders case-insensitively")
    {
        REQUIRE(vt_string::compare_ignore_case("abc", "ABC") == 0);
        REQUIRE(vt_string::compare_ignore_case("abc", "abd") != 0);
    }
}

TEST_CASE("vt_string splitting", "[string_utils][split]")
{
    SECTION("splits on the delimiter")
    {
        const auto parts = vt_string::split("a,b,c", ',');
        REQUIRE(parts.size() == 3);
        REQUIRE(parts[0] == "a");
        REQUIRE(parts[1] == "b");
        REQUIRE(parts[2] == "c");
    }

    SECTION("a string without the delimiter yields one part")
    {
        const auto parts = vt_string::split("single", ',');
        REQUIRE(parts.size() == 1);
        REQUIRE(parts[0] == "single");
    }
}

TEST_CASE("vt_string path helpers", "[string_utils][path]")
{
    SECTION("get_filename and get_directory")
    {
        REQUIRE(vt_string::get_filename("/usr/viewtouch/dat/check_1234") == "check_1234");
        REQUIRE(vt_string::get_directory("/usr/viewtouch/dat/check_1234") == "/usr/viewtouch/dat");
    }

    SECTION("get_extension includes the leading dot")
    {
        REQUIRE(vt_string::get_extension("settings.dat") == ".dat");
        REQUIRE(vt_string::get_extension("archive_000012.bak") == ".bak");
    }

    SECTION("a name with no extension yields no extension")
    {
        REQUIRE(vt_string::get_extension("check_1234").empty());
    }

    SECTION("combine_paths joins with a single separator")
    {
        REQUIRE(vt_string::combine_paths("/usr/viewtouch", "dat") == "/usr/viewtouch/dat");
    }
}

TEST_CASE("vt_string classification", "[string_utils][classify]")
{
    SECTION("is_numeric")
    {
        REQUIRE(vt_string::is_numeric("12345"));
        REQUIRE_FALSE(vt_string::is_numeric("abc123"));
        REQUIRE_FALSE(vt_string::is_numeric(""));
    }

    SECTION("is_alpha")
    {
        REQUIRE(vt_string::is_alpha("abcDEF"));
        REQUIRE_FALSE(vt_string::is_alpha("abc123"));
    }

    SECTION("is_alphanumeric")
    {
        REQUIRE(vt_string::is_alphanumeric("abc123"));
        REQUIRE_FALSE(vt_string::is_alphanumeric("abc 123"));
    }

    SECTION("is_email")
    {
        REQUIRE(vt_string::is_email("staff@example.com"));
        REQUIRE_FALSE(vt_string::is_email("not-an-email"));
    }
}

TEST_CASE("vt_string::format", "[string_utils][format]")
{
    SECTION("formats like printf")
    {
        REQUIRE(vt_string::format("check %d", 1234) == "check 1234");
        REQUIRE(vt_string::format("%s: %.2f", "total", 12.5) == "total: 12.50");
    }

    SECTION("handles output longer than the internal buffer")
    {
        // format() falls back to a heap buffer when snprintf reports the result
        // does not fit; this is the path that would silently truncate.
        const std::string long_input(4096, 'x');
        const std::string result = vt_string::format("%s", long_input.c_str());
        REQUIRE(result.size() == long_input.size());
        REQUIRE(result == long_input);
    }
}
