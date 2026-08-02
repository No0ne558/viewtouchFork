#include "legacy_file_builder.hh"

#include <format>
#include <fstream>
#include <string>

namespace vt_test {

namespace {

// Must match kNewEncodeDigits in src/core/data_file.cc.
constexpr std::string_view kDigits =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

// Mirrors OutputDataFile::PutValue: emit 6-bit groups most significant first.
// The production writer fills a buffer backwards, which produces the same
// big-endian digit order this builds forwards.
std::string EncodeInt(int64_t signed_value)
{
    // The writer widens every integer width to uint64_t before encoding, so a
    // negative value is stored as its two's-complement bit pattern and narrowed
    // back on read.
    auto value = static_cast<uint64_t>(signed_value);

    std::string digits;
    do
    {
        digits.insert(digits.begin(), kDigits[value & 0x3FU]);
        value >>= 6U;
    } while (value > 0);

    return digits;
}

char Separator(bool line_break)
{
    return line_break ? '\n' : ' ';
}

} // namespace

LegacyFileBuilder &LegacyFileBuilder::Int(int64_t value, bool line_break)
{
    body_ += EncodeInt(value);
    body_ += Separator(line_break);
    return *this;
}

LegacyFileBuilder &LegacyFileBuilder::Str(std::string_view value, bool line_break)
{
    if (value.empty())
    {
        body_ += '~';
    }
    else
    {
        for (char ch : value)
        {
            // The lossy substitution: a space and a tilde are both stored as an
            // underscore, and the reader turns every underscore back into a
            // space. Reproduced here so tests see the real behaviour.
            body_ += (ch == ' ' || ch == '~') ? '_' : ch;
        }
    }
    body_ += Separator(line_break);
    return *this;
}

LegacyFileBuilder &LegacyFileBuilder::Real(double value, bool line_break)
{
    // std::format's default for double is the shortest round-trippable form,
    // which is what vt::cpp23::format_to_buffer produces in the writer.
    body_ += std::format("{}", value);
    body_ += Separator(line_break);
    return *this;
}

LegacyFileBuilder &LegacyFileBuilder::Time(int seconds_in_year, int year, bool line_break)
{
    Int(seconds_in_year);
    Int(year, line_break);
    return *this;
}

LegacyFileBuilder &LegacyFileBuilder::UnsetTime(bool line_break)
{
    return Time(0, 0, line_break);
}

LegacyFileBuilder &LegacyFileBuilder::Raw(std::string_view text)
{
    body_ += text;
    return *this;
}

std::string LegacyFileBuilder::Build(int version) const
{
    return "vtpos 0 " + std::to_string(version) + "\n" + body_;
}

void LegacyFileBuilder::WriteTo(const std::string &path, int version) const
{
    std::ofstream out(path, std::ios::binary);
    const std::string contents = Build(version);
    out.write(contents.data(), static_cast<std::streamsize>(contents.size()));
}

} // namespace vt_test
