/*
 * Builds legacy ViewTouch data files byte by byte, at any historical version.
 *
 * The production writers only ever emit the current version constant --
 * Check::Write refuses anything else, and System::SaveCheck always passes
 * CHECK_VERSION -- so there is no way to produce a version-19 file with the
 * application's own code. Testing the version ladders that Read() implements
 * therefore requires constructing the bytes directly.
 *
 * This mirrors OutputDataFile's encoding exactly (src/core/data_file.cc):
 *
 *   header   "vtpos 0 <version>\n"
 *   integers big-endian base-64 digits, most significant first, then a
 *            separator. Values are widened to uint64_t first, so negatives
 *            become large positives and read back correctly once narrowed.
 *   strings  empty is "~"; otherwise every ' ' and '~' becomes '_' -- the
 *            substitution that makes the format lossy for strings.
 *   TimeInfo two integers, (seconds_in_year, year); (0,0) means unset.
 *
 * The encoding is verified against the production writer in
 * test_golden_corpus.cc rather than trusted: a builder that silently disagreed
 * with OutputDataFile would make every test using it meaningless.
 */

#ifndef VT_LEGACY_FILE_BUILDER_HH
#define VT_LEGACY_FILE_BUILDER_HH

#include <cstdint>
#include <string>
#include <string_view>

namespace vt_test {

class LegacyFileBuilder
{
public:
    // Append an integer. `line_break` chooses '\n' over ' ' as the separator,
    // matching OutputDataFile's `bk` argument. It is purely cosmetic to the
    // reader, which treats all whitespace alike -- except where Check::ReadFix
    // counts tokens on a line.
    LegacyFileBuilder &Int(int64_t value, bool line_break = false);

    // Append a string using the writer's escaping rules.
    LegacyFileBuilder &Str(std::string_view value, bool line_break = false);

    // Append a Flt. Unlike integers these are written as plain decimal text --
    // OutputDataFile::Write(Flt) formats the double and the reader strtod's it
    // back -- so this is not base-64 encoded.
    LegacyFileBuilder &Real(double value, bool line_break = false);

    // Append a TimeInfo as its two integers.
    LegacyFileBuilder &Time(int seconds_in_year, int year, bool line_break = false);

    // Append an unset TimeInfo, which the writer encodes as (0, 0).
    LegacyFileBuilder &UnsetTime(bool line_break = false);

    // Append a raw fragment, for constructing malformed files deliberately.
    LegacyFileBuilder &Raw(std::string_view text);

    // The complete file contents, header included.
    [[nodiscard]] std::string Build(int version) const;

    // Write Build(version) to `path`.
    void WriteTo(const std::string &path, int version) const;

private:
    std::string body_;
};

} // namespace vt_test

#endif // VT_LEGACY_FILE_BUILDER_HH
