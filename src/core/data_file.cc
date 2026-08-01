
/*
 * Copyright ViewTouch, Inc., 1995, 1996, 1997, 1998, 2025, 2026

 *   This program is free software: you can redistribute it and/or modify 
 *   it under the terms of the GNU General Public License as published by 
 *   the Free Software Foundation, either version 3 of the License, or 
 *   (at your option) any later version.
 * 
 *   This program is distributed in the hope that it will be useful, 
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of 
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the 
 *   GNU General Public License for more details. 
 * 
 *   You should have received a copy of the GNU General Public License 
 *   along with this program.  If not, see <http://www.gnu.org/licenses/>. 
 *
 * data_file.cc - revision 26 (9/8/98)
 * Implementation of data_file module
 */

#include "data_file.hh"
#include "cpp23_utils.hh"

#include <algorithm>
#include <array>
#include <cassert>
#include <cctype>
#include <cerrno>
#include <charconv>
#include <climits>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <iostream>
#include <limits>
#include <optional>
#include <string_view>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <unistd.h>

#ifdef DMALLOC
#include <dmalloc.h>
#endif

namespace
{
constexpr std::string_view kOldEncodeDigits = R"(abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ1234567890!@#$%^&*(),./;'[]-=\<>?:"{}_+|)";
constexpr std::string_view kNewEncodeDigits = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
constexpr std::size_t kOldBase = kOldEncodeDigits.size();

using DecodeTable = std::array<uint8_t, 256>;

[[nodiscard]] DecodeTable build_decode_table(std::string_view alphabet)
{
    DecodeTable table{};
    for (std::size_t idx = 0; idx < alphabet.size(); ++idx)
    {
        table[static_cast<unsigned char>(alphabet[idx])] = static_cast<uint8_t>(idx);
    }
    return table;
}

[[nodiscard]] const DecodeTable& old_decode_table()
{
    static const DecodeTable table = build_decode_table(kOldEncodeDigits);
    return table;
}

[[nodiscard]] const DecodeTable& new_decode_table()
{
    static const DecodeTable table = build_decode_table(kNewEncodeDigits);
    return table;
}

[[nodiscard]] std::optional<int> parse_int(const char* text)
{
    if (text == nullptr)
    {
        return std::nullopt;
    }

    char* end = nullptr;
    errno = 0;
    const long parsed = std::strtol(text, &end, 10);
    if (text == end || *end != '\0' || errno == ERANGE ||
        parsed < std::numeric_limits<int>::min() || parsed > std::numeric_limits<int>::max())
    {
        return std::nullopt;
    }
    return static_cast<int>(parsed);
}

inline void write_raw(gzFile gz_fp, std::FILE* file_fp, bool compress, const char* data, std::size_t length)
{
    if (length == 0 || data == nullptr)
    {
        return;
    }

    if (compress)
    {
        gzwrite(gz_fp, data, static_cast<unsigned int>(length));
    }
    else
    {
        std::fwrite(data, 1, length, file_fp);
    }
}

inline void put_character(gzFile gz_fp, std::FILE* file_fp, bool compress, char ch)
{
    if (compress)
    {
        gzputc(gz_fp, static_cast<unsigned char>(ch));
    }
    else
    {
        std::fputc(static_cast<unsigned char>(ch), file_fp);
    }
}

inline bool is_space(int ch) noexcept
{
    return std::isspace(static_cast<unsigned char>(ch)) != 0;
}

} // namespace

#ifdef VT_TESTING
int ReportError(const std::string &message)
{
    FnTrace("ReportError()");
    std::cerr << message << std::endl;
    return 0;
}
#endif

/*********************************************************************
 * InputDataFile
 ********************************************************************/

InputDataFile::~InputDataFile()
{
    Close();
}

int InputDataFile::Open(const std::string &name, int &version)
{
    FnTrace("InputDataFile::Open()");
    if (name.empty())
    {
        ReportError("InputDataFile::Open: empty filename");
        return 1;
    }

    if (!std::ifstream(name).good())
    {
        ReportError("Unable to read non existing file: '" + name + "'");
        return 1;
    }

    Close();
    version = 0;
    end_of_file = false;
    old_format = false;

    fp = gzopen(name.c_str(), "r");
    if (fp == nullptr)
    {
        ReportError("Unable to read file: '" + name + "' errno: " + std::to_string(errno));
        return 1;
    }

    std::array<char, 256> token{};
    if (GetToken(token.data(), static_cast<int>(token.size())) != 0)
    {
        Close();
        ReportError("Unknown file format for file: '" + name + "'");
        return 1;
    }

    if (std::strncmp(token.data(), "version_", 8) == 0)
    {
        const auto parsed_version = parse_int(token.data() + 8);
        if (!parsed_version)
        {
            Close();
            ReportError("Invalid version token in file: '" + name + "'");
            return 1;
        }
        version = *parsed_version;
        old_format = true;
    }
    else if (std::strncmp(token.data(), "vtpos", 5) == 0)
    {
        if (GetToken(token.data(), static_cast<int>(token.size())) != 0)
        {
            Close();
            ReportError("Incomplete file header for file: '" + name + "'");
            return 1;
        }
        if (GetToken(token.data(), static_cast<int>(token.size())) != 0)
        {
            Close();
            ReportError("Missing version header for file: '" + name + "'");
            return 1;
        }
        const auto parsed_version = parse_int(token.data());
        if (!parsed_version)
        {
            Close();
            ReportError("Invalid version token in file: '" + name + "'");
            return 1;
        }
        version = *parsed_version;
        old_format = false;
    }
    else
    {
        Close();
        ReportError("Unknown file format for file: '" + name + "'");
        return 1;
    }

    filename = name;
    return 0;
}

int InputDataFile::Close() noexcept
{
    FnTrace("InputDataFile::Close()");
    if (fp != nullptr)
    {
        gzclose(fp);
        fp = nullptr;
    }
    old_format = false;
    end_of_file = false;
    return 0;
}

int InputDataFile::GetToken(char* buffer, int max_len)
{
    FnTrace("InputDataFile::GetToken()");
    if (fp == nullptr || buffer == nullptr || max_len <= 0)
    {
        return 1;
    }

    const std::size_t capacity = static_cast<std::size_t>(max_len);
    std::size_t index = 0;

    int ch = gzgetc(fp);
    while (ch >= 0 && is_space(ch))
    {
        ch = gzgetc(fp);
    }

    while (ch >= 0)
    {
        if (is_space(ch))
        {
            buffer[index] = '\0';
            return 0;
        }

        if (index + 1 >= capacity)
        {
            buffer[capacity - 1] = '\0';
            return 1;
        }

        buffer[index] = static_cast<char>(ch);
        ++index;
        ch = gzgetc(fp);
    }

    end_of_file = true;
    buffer[index] = '\0';
    return (index == 0) ? 1 : 0;
}

uint64_t InputDataFile::GetValue()
{
    FnTrace("InputDataFile::GetValue()");
    uint64_t value = 0;

    if (fp == nullptr)
    {
        end_of_file = true;
        return 0;
    }

    if (old_format)
    {
        const auto& decode = old_decode_table();
        for (;;)
        {
            const int ch = gzgetc(fp);
            if (ch < 0)
            {
                end_of_file = true;
                return 0;
            }
            if (is_space(ch))
            {
                return value;
            }
            value = (value * kOldBase) + decode[static_cast<unsigned char>(ch)];
        }
    }

    int ch = gzgetc(fp);
    while (ch >= 0 && is_space(ch))
    {
        ch = gzgetc(fp);
    }

    const auto& decode = new_decode_table();
    while (ch >= 0)
    {
        if (is_space(ch))
        {
            return value;
        }
        value = (value << 6) + decode[static_cast<unsigned char>(ch)];
        ch = gzgetc(fp);
    }

    end_of_file = true;
    return value;
}

int InputDataFile::Read(Flt &val)
{
    FnTrace("InputDataFile::Read(Flt &)");
    std::array<char, 256> token{};
    if (GetToken(token.data(), static_cast<int>(token.size())) != 0)
    {
        return 1;
    }

    char* end = nullptr;
    errno = 0;
    const double parsed = std::strtod(token.data(), &end);
    if (token.data() == end || errno == ERANGE)
    {
        return 1;
    }

    val = static_cast<Flt>(parsed);
    return 0;
}

int InputDataFile::Read(Str &s)
{
    FnTrace("InputDataFile::Read(Str &)");
    std::array<char, STRLONG> token{};
    if (GetToken(token.data(), static_cast<int>(token.size())) != 0)
    {
        return 1;
    }

    if (token[0] == '\0' || std::strcmp(token.data(), "~") == 0)
    {
        s.Clear();
    }
    else
    {
        s.Set(token.data());
        s.ChangeAtoB('_', ' ');
    }
    return 0;
}

int InputDataFile::Read(TimeInfo &timevar)
{
    FnTrace("InputDataFile::Read(TimeInfo &)");
    const auto seconds = static_cast<int>(GetValue());
    const auto year = static_cast<int>(GetValue());
    if (seconds == 0 && year == 0)
    {
        timevar.Clear();
    }
    else
    {
        timevar.Set(seconds, year);
    }
    return 0;
}

int InputDataFile::Read(int *val)
{
    FnTrace("InputDataFile::Read(int *)");
    if (val == nullptr)
    {
        return 0;
    }
    return Read(*val);
}

int InputDataFile::Read(Flt *val)
{
    FnTrace("InputDataFile::Read(Flt *)");
    if (val == nullptr)
    {
        return 0;
    }
    return Read(*val);
}

int InputDataFile::Read(Str *val)
{
    FnTrace("InputDataFile::Read(Str *)");
    if (val == nullptr)
    {
        return 0;
    }
    return Read(*val);
}

int InputDataFile::PeekTokens()
{
    FnTrace("InputDataFile::PeekTokens()");
    if (fp == nullptr)
    {
        return 0;
    }

    const auto savepos = gzseek(fp, 0L, SEEK_CUR);
    if (savepos < 0)
    {
        return 0;
    }

    int count = 0;
    bool newline_found = false;
    bool started = false;

    while (!newline_found && !end_of_file)
    {
        const int ch = gzgetc(fp);
        if (ch < 0)
        {
            end_of_file = true;
            break;
        }

        if (is_space(ch))
        {
            if (started)
            {
                ++count;
                if (ch == '\n')
                {
                    newline_found = true;
                }
            }
            started = true;
        }
    }

    gzseek(fp, savepos, SEEK_SET);
    return count;
}

const char* InputDataFile::ShowTokens(char* buffer, int lines)
{
    FnTrace("InputDataFile::ShowTokens()");
    if (fp == nullptr || lines <= 0)
    {
        static std::array<char, STRLONG> empty_buffer{};
        empty_buffer[0] = '\0';
        return empty_buffer.data();
    }

    static std::array<char, STRLONG> fallback{};
    char* out = (buffer != nullptr) ? buffer : fallback.data();

    const auto savepos = gzseek(fp, 0L, SEEK_CUR);
    if (savepos < 0)
    {
        out[0] = '\0';
        return out;
    }

    std::size_t index = 0;
    while (lines-- > 0 && !end_of_file)
    {
        int ch = gzgetc(fp);
        while (ch >= 0 && ch != '\n')
        {
            if (index + 1 < STRLONG)
            {
                out[index++] = static_cast<char>(ch);
            }
            ch = gzgetc(fp);
        }
        if (ch < 0)
        {
            end_of_file = true;
        }
        if (lines > 0 && index + 1 < STRLONG)
        {
            out[index++] = '\n';
        }
    }

    out[index] = '\0';
    gzseek(fp, savepos, SEEK_SET);
    return out;
}

/*********************************************************************
 * OutputDataFile
 ********************************************************************/

OutputDataFile::~OutputDataFile()
{
    Close();
}

int OutputDataFile::Open(const std::string &filepath, int version, int use_compression)
{
    FnTrace("OutputDataFile::Open()");

    if (filepath.empty())
    {
        ReportError("OutputDataFile::Open: empty filepath");
        return 1;
    }

    Close();

    filename = filepath;
    compress = (use_compression != 0);

    // Write to a sibling temp file rather than the destination. Opening the
    // destination directly would truncate it before a single byte of the
    // replacement existed, so any interruption -- crash, kill, power cut, full
    // disk -- destroyed the only copy. Checks, drawers and archives have no
    // backup rotation to fall back on. The temp file must be a sibling so the
    // rename in Finish() stays within one filesystem and is therefore atomic.
    //
    // The name is deliberately dot-prefixed rather than a plain suffix. The data
    // directories are scanned by filename prefix -- System::LoadCurrentData
    // matches "check_" and "drawer_", LaborDB::Load matches "labor_", and so on
    // -- so a temp file called "check_57.vtnew" left behind by a kill would be
    // picked up as a check and half-read on the next start. ".check_57.vtnew"
    // matches no prefix and is skipped.
    {
        const std::size_t slash = filepath.find_last_of('/');
        if (slash == std::string::npos)
            temp_filename = "." + filepath + ".vtnew";
        else
            temp_filename = filepath.substr(0, slash + 1) + "." +
                            filepath.substr(slash + 1) + ".vtnew";
    }

    const int fd = ::open(temp_filename.c_str(),
                          O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    if (fd < 0)
    {
        ReportError("OutputDataFile::Open error '" + std::to_string(errno) +
                    "' for '" + temp_filename + "'");
        temp_filename.clear();
        return 1;
    }

    // gzclose()/fclose() close the descriptor they are given, so keep a
    // duplicate to fsync through once the stream has been flushed.
    data_fd = ::dup(fd);

    if (compress)
    {
        gz_fp = gzdopen(fd, "wb");
    }
    else
    {
        file_fp = ::fdopen(fd, "w");
    }

    const bool open_failed = (compress && gz_fp == nullptr) || (!compress && file_fp == nullptr);
    if (open_failed)
    {
        ReportError("OutputDataFile::Open error '" + std::to_string(errno) + "' for '" + filepath + "'");
        ::close(fd);
        if (data_fd >= 0)
        {
            ::close(data_fd);
            data_fd = -1;
        }
        ::unlink(temp_filename.c_str());
        temp_filename.clear();
        return 1;
    }

    const std::string header = "vtpos 0 " + std::to_string(version) + "\n";
    write_raw(gz_fp, file_fp, compress, header.c_str(), header.size());
    return 0;
}

int OutputDataFile::Finish() noexcept
{
    if (temp_filename.empty())
        return 0;   // nothing in flight

    // Note the error state before closing: gzerror/ferror need the stream.
    const bool had_write_error = HasError();

    int close_error = 0;
    if (gz_fp != nullptr)
    {
        close_error = (gzclose(gz_fp) != Z_OK);
        gz_fp = nullptr;
    }
    if (file_fp != nullptr)
    {
        // fclose flushes; its result is where a full disk finally surfaces, and
        // it used to be discarded.
        close_error = (std::fclose(file_fp) != 0) || close_error;
        file_fp = nullptr;
    }

    bool sync_error = false;
    if (data_fd >= 0)
    {
        // Force the data out to the device before the rename is allowed to
        // publish it. Without this the rename can land while the contents are
        // still only in the page cache, so a power cut can leave a
        // correctly-named but empty file.
        sync_error = (::fsync(data_fd) != 0);
        ::close(data_fd);
        data_fd = -1;
    }

    const std::string temp = temp_filename;
    temp_filename.clear();

    if (had_write_error || close_error || sync_error)
    {
        // Leave the previous contents alone. A partially written replacement is
        // never better than the file it would have overwritten.
        ReportError("OutputDataFile: write failed, keeping previous contents of '" +
                    filename + "'");
        ::unlink(temp.c_str());
        return 1;
    }

    if (std::rename(temp.c_str(), filename.c_str()) != 0)
    {
        ReportError("OutputDataFile: could not replace '" + filename + "' (errno " +
                    std::to_string(errno) + ")");
        ::unlink(temp.c_str());
        return 1;
    }

    // Make the rename itself durable. Without this the directory entry can be
    // lost on power failure even though the data was synced.
    const std::size_t slash = filename.find_last_of('/');
    const std::string dir = (slash == std::string::npos) ? std::string(".")
                                                         : filename.substr(0, slash);
    const int dir_fd = ::open(dir.c_str(), O_RDONLY | O_CLOEXEC);
    if (dir_fd >= 0)
    {
        ::fsync(dir_fd);
        ::close(dir_fd);
    }

    return 0;
}

int OutputDataFile::Close() noexcept
{
    FnTrace("OutputDataFile::Close()");
    return Finish();
}

int OutputDataFile::PutValue(uint64_t val, int bk)
{
    FnTrace("OutputDataFile::PutValue()");
    std::array<char, 32> buffer{};
    std::size_t cursor = buffer.size();

    buffer[--cursor] = static_cast<char>(bk ? '\n' : ' ');
    do
    {
        buffer[--cursor] = kNewEncodeDigits[static_cast<std::size_t>(val & 0x3F)];
        val >>= 6U;
    }
    while (val > 0 && cursor > 0);

    const char* start = buffer.data() + cursor;
    const std::size_t length = buffer.size() - cursor;
    write_raw(gz_fp, file_fp, compress, start, length);
    return 0;
}

int OutputDataFile::Write(Flt val, int bk)
{
    FnTrace("OutputDataFile::Write(Flt)");
    std::array<char, 64> buffer{};
    const int written = bk
        ? vt::cpp23::format_to_buffer(buffer.data(), buffer.size(), "{}\n", static_cast<double>(val))
        : vt::cpp23::format_to_buffer(buffer.data(), buffer.size(), "{} ", static_cast<double>(val));
    if (written > 0)
    {
        write_raw(gz_fp, file_fp, compress, buffer.data(), static_cast<std::size_t>(written));
    }
    return 0;
}

int OutputDataFile::Write(const char* val, int bk)
{
    FnTrace("OutputDataFile::Write(const char* )");
    if (val == nullptr)
    {
        return 1;
    }

    const std::string_view view(val);
    if (view.empty())
    {
        const char* token = bk ? "~\n" : "~ ";
        write_raw(gz_fp, file_fp, compress, token, 2);
        return 0;
    }

    for (char ch : view)
    {
        char out = ch;
        if (out == '~' || out == ' ')
        {
            out = '_';
        }
        put_character(gz_fp, file_fp, compress, out);
    }
    put_character(gz_fp, file_fp, compress, static_cast<char>(bk ? '\n' : ' '));
    return 0;
}

int OutputDataFile::Write(TimeInfo &timevar, int bk)
{
    FnTrace("OutputDataFile::Write(TimeInfo &)");
    int error = 0;
    if (!timevar.IsSet())
    {
        const int seconds = 0;
        const int year = 0;
        error += Write(seconds);
        error += Write(year);
    }
    else
    {
        const int seconds = timevar.SecondsInYear();
        const int year = timevar.Year();
        error += Write(seconds);
        error += Write(year);
    }

    if (bk)
    {
        put_character(gz_fp, file_fp, compress, '\n');
    }
    return error;
}

int OutputDataFile::Write(int *val, int bk)
{
    FnTrace("OutputDataFile::Write(int *)");
    if (val == nullptr)
    {
        return 0;
    }
    return Write(*val, bk);
}

int OutputDataFile::Write(Flt *val, int bk)
{
    FnTrace("OutputDataFile::Write(Flt *)");
    if (val == nullptr)
    {
        return 0;
    }
    return Write(*val, bk);
}

int OutputDataFile::Write(Str *val, int bk)
{
    FnTrace("OutputDataFile::Write(Str *)");
    if (val == nullptr)
    {
        return 0;
    }
    return Write(*val, bk);
}

/*********************************************************************
 * KeyValueInputFile
 ********************************************************************/

KeyValueInputFile::KeyValueInputFile(int fd) :
    filedes(fd)
{
    FnTrace("KeyValueInputFile::KeyValueInputFile(int)");
}

KeyValueInputFile::KeyValueInputFile(const std::string &filename) :
    inputfile(filename)
{
    FnTrace("KeyValueInputFile::KeyValueInputFile(const std::string&)");
}

bool KeyValueInputFile::Open()
{
    FnTrace("KeyValueInputFile::Open()");
    bool retval = false;

    if (!inputfile.empty())
    {
        filedes = open(inputfile.c_str(), O_RDONLY);
        if (filedes > 0)
        {
            retval = true;
            bytesread = 0;
            keyidx = 0;
            validx = 0;
            buffidx = 0;
            comment = false;
            getvalue = false;
        }
        else if (filedes < 0)
        {
            ReportError("KeyValueInputFile::Open error '" + std::to_string(errno) + "' for '" + inputfile + "'");
        }
    }

    return retval;
}

bool KeyValueInputFile::Open(const std::string &filename)
{
    FnTrace("KeyValueInputFile::Open(const std::string&)");

    inputfile = filename;
    return Open();
}

bool KeyValueInputFile::IsOpen() const noexcept
{
    FnTrace("KeyValueInputFile::IsOpen()");
    return (filedes > 0);
}

int KeyValueInputFile::Set(int fd)
{
    FnTrace("KeyValueInputFile::Set(int)");
    filedes = fd;
    return 0;
}

int KeyValueInputFile::Set(const std::string &filename)
{
    FnTrace("KeyValueInputFile::Set(const std::string&)");
    inputfile = filename;
    return 0;
}

char KeyValueInputFile::SetDelim(char delim)
{
    FnTrace("KeyValueInputFile::SetDelim()");
    const char previous = delimiter;
    delimiter = delim;
    return previous;
}

int KeyValueInputFile::Close()
{
    FnTrace("KeyValueInputFile::Close()");
    if (filedes < 0)
    {
        return 0;
    }
    const int retval = close(filedes);
    filedes = -1;
    return retval;
}

int KeyValueInputFile::Reset()
{
    FnTrace("KeyValueInputFile::Reset()");
    int retval = 0;
    if (filedes > 0)
    {
        close(filedes);
        retval = 1;
    }

    filedes = -1;
    bytesread = 0;
    keyidx = 0;
    validx = 0;
    buffidx = 0;
    comment = false;
    getvalue = false;
    buffer.fill('\0');
    inputfile.clear();

    return retval;
}

int KeyValueInputFile::Read(char* key, char* value, int maxlen)
{
    FnTrace("KeyValueInputFile::Read()");
    if (filedes < 0 || key == nullptr || value == nullptr || maxlen <= 0)
    {
        return 0;
    }

    const std::size_t limit = static_cast<std::size_t>(maxlen);
    key[0] = '\0';
    value[0] = '\0';

    if (bytesread == 0)
    {
        const auto read_bytes = ::read(filedes, buffer.data(), buffer.size());
        if (read_bytes <= 0)
        {
            bytesread = 0;
            return 0;
        }
        bytesread = static_cast<std::size_t>(read_bytes);
        buffidx = 0;
    }

    int retval = 0;
    char last = '\0';

    constexpr char backslash = static_cast<char>(0x5C);
    while (bytesread > 0 && retval == 0)
    {
        while (buffidx < bytesread && retval == 0)
        {
            const char ch = buffer[buffidx];
            if (ch == '\n')
            {
                if (keyidx)
                {
                    key[keyidx] = '\0';
                    value[validx] = '\0';
                    StripWhiteSpace(key);
                    StripWhiteSpace(value);
                }
                keyidx = 0;
                validx = 0;
                comment = false;
                getvalue = false;
                retval = 1;
            }
            else if (ch == '#' && last != backslash)
            {
                comment = true;
            }
            else if (comment || ch == backslash)
            {
                // skip comment content and escaped characters
            }
            else if (getvalue && validx + 1 < limit)
            {
                value[validx++] = ch;
            }
            else if (ch == delimiter)
            {
                getvalue = true;
            }
            else if (keyidx + 1 < limit)
            {
                key[keyidx++] = ch;
            }

            last = ch;
            ++buffidx;
        }

        if (retval == 0)
        {
            const auto read_bytes = ::read(filedes, buffer.data(), buffer.size());
            if (read_bytes <= 0)
            {
                bytesread = 0;
            }
            else
            {
                bytesread = static_cast<std::size_t>(read_bytes);
                buffidx = 0;
            }
        }
    }

    if (retval == 1)
    {
        key[keyidx] = '\0';
        value[validx] = '\0';
    }

    return retval;
}

/*********************************************************************
 * KeyValueOutputFile
 ********************************************************************/

KeyValueOutputFile::KeyValueOutputFile(int fd) :
    filedes(fd)
{
    FnTrace("KeyValueOutputFile::KeyValueOutputFile(int)");
}

KeyValueOutputFile::KeyValueOutputFile(const std::string &filename) :
    outputfile(filename)
{
    FnTrace("KeyValueOutputFile::KeyValueOutputFile(const std::string&)");
}

int KeyValueOutputFile::Open()
{
    FnTrace("KeyValueOutputFile::Open()");
    int retval = 0;

    if (!outputfile.empty())
    {
        const int mode = S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP;
        filedes = open(outputfile.c_str(), O_WRONLY | O_CREAT | O_TRUNC, mode);
        if (filedes > 0)
        {
            retval = 1;
        }
        else if (filedes < 0)
        {
            ReportError("KeyValueOutputFile::Open error '" + std::to_string(errno) + "' for '" + outputfile + "'");
        }
    }

    return retval;
}

int KeyValueOutputFile::Open(const std::string &filename)
{
    FnTrace("KeyValueOutputFile::Open(const std::string&)");
    outputfile = filename;
    return Open();
}

bool KeyValueOutputFile::IsOpen() const noexcept
{
    FnTrace("KeyValueOutputFile::IsOpen()");
    return (filedes > 0);
}

char KeyValueOutputFile::SetDelim(char delim)
{
    FnTrace("KeyValueOutputFile::SetDelim()");
    const char previous = delimiter;
    delimiter = delim;
    return previous;
}

int KeyValueOutputFile::Close()
{
    FnTrace("KeyValueOutputFile::Close()");
    if (filedes < 0)
    {
        return 0;
    }
    const int retval = close(filedes);
    filedes = -1;
    return retval;
}

int KeyValueOutputFile::Reset()
{
    FnTrace("KeyValueOutputFile::Reset()");
    int retval = 0;
    if (filedes > 0)
    {
        close(filedes);
        retval = 1;
    }

    filedes = -1;
    outputfile.clear();

    return retval;
}

int KeyValueOutputFile::Write(const std::string &key, const std::string &value)
{
    FnTrace("KeyValueOutputFile::Write()");
    if (filedes < 0)
    {
        return 0;
    }

    std::string buffer = key;
    buffer += delimiter;
    buffer += ' ';
    buffer += value;
    buffer += '\n';

    return static_cast<int>(::write(filedes, buffer.c_str(), buffer.size()));
}
