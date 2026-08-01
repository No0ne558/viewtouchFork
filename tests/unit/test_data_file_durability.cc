/*
 * Crash-safety tests for the data file writer (src/core/data_file.cc).
 *
 * These cover the single largest data-loss risk in the product. Every check,
 * drawer, archive and settings file is written through OutputDataFile, and it
 * opened the destination directly in truncating mode -- so the live file was
 * destroyed before a single new byte was written. A crash, a kill, a full disk
 * or a power cut anywhere in the middle left a truncated file where the good one
 * had been, with no copy anywhere: BackupFile has only two callers in the whole
 * tree, and checks, drawers and archives are not among them.
 *
 * Archive::SavePacked makes it worst: it rewrites an entire business day -- every
 * drawer, check, tip, exception and credit record -- as one truncate-and-rewrite.
 *
 * The property under test is that the destination is never in a partial state.
 * A write either leaves the previous contents completely intact or replaces them
 * completely, with no window in between. That is checked directly rather than by
 * killing a process: the destination is inspected while a write is in flight,
 * which is exactly the window a crash would land in.
 */

#include <catch2/catch_all.hpp>
#include "src/core/data_file.hh"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

namespace fs = std::filesystem;

namespace {

struct TempPath
{
    fs::path path;
    explicit TempPath(const std::string &name)
        : path(fs::temp_directory_path() / name)
    {
        std::error_code ec;
        fs::remove(path, ec);
    }
    ~TempPath()
    {
        std::error_code ec;
        fs::remove(path, ec);
    }
};

std::string ReadWhole(const fs::path &path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open())
        return {};
    return std::string((std::istreambuf_iterator<char>(in)),
                       std::istreambuf_iterator<char>());
}

// Writes a complete, valid file and returns its contents.
std::string WriteMarker(const fs::path &path, int marker, int compress = 0)
{
    OutputDataFile out;
    REQUIRE(out.Open(path.string(), 25, compress) == 0);
    out.Write(marker);
    out.Write("marker");
    REQUIRE(out.Close() == 0);
    return ReadWhole(path);
}

// Count every file whose name mentions the stem, including a dot-prefixed
// temporary, so a leak is visible.
std::size_t SiblingCount(const fs::path &path)
{
    std::size_t count = 0;
    const std::string stem = path.filename().string();
    for (const auto &entry : fs::directory_iterator(path.parent_path()))
    {
        if (entry.path().filename().string().find(stem) != std::string::npos)
            ++count;
    }
    return count;
}

} // namespace

TEST_CASE("An in-flight write does not destroy the previous file",
          "[data_file][durability][atomic]")
{
    SECTION("uncompressed")
    {
        TempPath file("vt_atomic_plain.dat");
        const std::string original = WriteMarker(file.path, 111);
        REQUIRE_FALSE(original.empty());

        {
            OutputDataFile out;
            REQUIRE(out.Open(file.path.string(), 25, 0) == 0);
            out.Write(222);

            // The crash window. A process killed here must leave the previous
            // contents readable, not a truncated file.
            REQUIRE(ReadWhole(file.path) == original);

            REQUIRE(out.Close() == 0);
        }

        // After a clean close the new contents are in place.
        const std::string replaced = ReadWhole(file.path);
        REQUIRE_FALSE(replaced.empty());
        REQUIRE(replaced != original);
    }

    SECTION("gzip compressed, as archives are written")
    {
        TempPath file("vt_atomic_gz.dat");
        const std::string original = WriteMarker(file.path, 111, 1);
        REQUIRE_FALSE(original.empty());

        {
            OutputDataFile out;
            REQUIRE(out.Open(file.path.string(), 25, 1) == 0);
            out.Write(222);

            REQUIRE(ReadWhole(file.path) == original);

            REQUIRE(out.Close() == 0);
        }

        REQUIRE(ReadWhole(file.path) != original);
    }
}

TEST_CASE("A completed write is readable by the reader",
          "[data_file][durability][roundtrip]")
{
    // Atomicity is worthless if it breaks the normal path, so the ordinary
    // write-then-read cycle is asserted alongside it.
    SECTION("uncompressed")
    {
        TempPath file("vt_atomic_rt_plain.dat");
        {
            OutputDataFile out;
            REQUIRE(out.Open(file.path.string(), 25, 0) == 0);
            out.Write(4242);
            out.Write("Cheeseburger");
            REQUIRE(out.Close() == 0);
        }

        int version = 0;
        InputDataFile in;
        REQUIRE(in.Open(file.path.string(), version) == 0);
        REQUIRE(version == 25);

        int value = 0;
        Str text;
        in.Read(value);
        in.Read(text);
        REQUIRE(value == 4242);
        REQUIRE(std::string(text.Value()) == "Cheeseburger");
    }

    SECTION("gzip compressed")
    {
        TempPath file("vt_atomic_rt_gz.dat");
        {
            OutputDataFile out;
            REQUIRE(out.Open(file.path.string(), 25, 1) == 0);
            out.Write(4242);
            out.Write("Cheeseburger");
            REQUIRE(out.Close() == 0);
        }

        int version = 0;
        InputDataFile in;
        REQUIRE(in.Open(file.path.string(), version) == 0);

        int value = 0;
        Str text;
        in.Read(value);
        in.Read(text);
        REQUIRE(value == 4242);
        REQUIRE(std::string(text.Value()) == "Cheeseburger");
    }
}

TEST_CASE("Writing through the destructor still commits",
          "[data_file][durability][destructor]")
{
    // Only about a dozen of the ~80 OutputDataFile call sites close explicitly;
    // the rest rely on the destructor. It must therefore commit, not discard.
    TempPath file("vt_atomic_dtor.dat");

    {
        OutputDataFile out;
        REQUIRE(out.Open(file.path.string(), 25, 0) == 0);
        out.Write(777);
    }   // destructor commits

    int version = 0;
    InputDataFile in;
    REQUIRE(in.Open(file.path.string(), version) == 0);
    int value = 0;
    in.Read(value);
    REQUIRE(value == 777);
}

TEST_CASE("No temporary files are left behind", "[data_file][durability][cleanup]")
{
    SECTION("a committed write leaves exactly one file")
    {
        TempPath file("vt_atomic_clean.dat");
        {
            OutputDataFile out;
            REQUIRE(out.Open(file.path.string(), 25, 0) == 0);
            out.Write(1);
        }
        REQUIRE(SiblingCount(file.path) == 1);
    }

    SECTION("rewriting repeatedly leaves exactly one file")
    {
        TempPath file("vt_atomic_repeat.dat");
        for (int i = 0; i < 5; ++i)
        {
            OutputDataFile out;
            REQUIRE(out.Open(file.path.string(), 25, 0) == 0);
            out.Write(i);
            REQUIRE(out.Close() == 0);
        }
        REQUIRE(SiblingCount(file.path) == 1);
    }
}

TEST_CASE("An in-flight temp file cannot be mistaken for real data",
          "[data_file][durability][naming]")
{
    // The data directories are scanned by filename prefix: System::LoadCurrentData
    // matches "check_" and "drawer_", LaborDB::Load matches "labor_". A temp file
    // named "check_57.dat.vtnew" would match "check_" and be half-read as a check
    // on the next start, turning a kill during a save into a corrupt load. The
    // temp name is dot-prefixed so it matches no scanner.
    TempPath file("check_57.dat");

    OutputDataFile out;
    REQUIRE(out.Open(file.path.string(), 25, 0) == 0);
    out.Write(1);

    // While the write is in flight, find whatever else the directory now holds.
    bool found_temp = false;
    for (const auto &entry : fs::directory_iterator(file.path.parent_path()))
    {
        const std::string name = entry.path().filename().string();
        if (name != "check_57.dat" && name.find("check_57") != std::string::npos)
        {
            found_temp = true;
            REQUIRE(name.rfind("check_", 0) != 0);   // no prefix match
            REQUIRE(name.front() == '.');            // hidden
        }
    }
    REQUIRE(found_temp);

    REQUIRE(out.Close() == 0);

    // Clean up the temp path too if anything went wrong.
    std::error_code ec;
    fs::remove(file.path.parent_path() / ".check_57.dat.vtnew", ec);
}

TEST_CASE("An unwritable destination is reported, not silently lost",
          "[data_file][durability][errors]")
{
    SECTION("opening in a non-existent directory fails")
    {
        const std::string bad =
            (fs::temp_directory_path() / "vt_no_such_dir_xyz" / "f.dat").string();

        OutputDataFile out;
        REQUIRE(out.Open(bad, 25, 0) != 0);
    }

    SECTION("an empty path fails")
    {
        OutputDataFile out;
        REQUIRE(out.Open("", 25, 0) != 0);
    }
}
