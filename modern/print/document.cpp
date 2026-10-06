#include "print/document.hh"

#include <algorithm>

namespace vt::print {

Document &Document::text(std::string s, Align a, bool bold, bool big)
{
    lines.push_back({Kind::Text, std::move(s), {}, a, bold, big});
    return *this;
}

Document &Document::columns(std::string left, std::string right, bool bold, bool big)
{
    lines.push_back({Kind::Columns, std::move(left), std::move(right), Align::Left, bold, big});
    return *this;
}

Document &Document::rule()
{
    lines.push_back({Kind::Rule});
    return *this;
}

Document &Document::blank()
{
    lines.push_back({Kind::Blank});
    return *this;
}

Document &Document::image(std::shared_ptr<const Raster> raster)
{
    if (raster && raster->width > 0 && raster->height > 0) {
        Line l;
        l.kind = Kind::Image;
        l.raster = std::move(raster);
        lines.push_back(std::move(l));
    }
    return *this;
}

namespace {

bool isContinuation(unsigned char c) { return (c & 0xC0) == 0x80; }

// First `n` code points of s.
std::string takeChars(const std::string &s, std::size_t n)
{
    std::size_t i = 0, count = 0;
    while (i < s.size() && count < n) {
        ++i;
        while (i < s.size() && isContinuation(static_cast<unsigned char>(s[i])))
            ++i;
        ++count;
    }
    return s.substr(0, i);
}

std::string dropChars(const std::string &s, std::size_t n)
{
    return s.substr(takeChars(s, n).size());
}

// Word-wrap to `width` characters.
std::vector<std::string> wrap(const std::string &s, std::size_t width)
{
    std::vector<std::string> out;
    std::string rest = s;
    while (displayWidth(rest) > width) {
        std::string head = takeChars(rest, width);
        const std::size_t space = head.rfind(' ');
        if (space != std::string::npos && space > 0)
            head = head.substr(0, space);
        out.push_back(head);
        rest = rest.substr(head.size());
        while (!rest.empty() && rest.front() == ' ')
            rest.erase(0, 1);
    }
    out.push_back(rest);
    return out;
}

std::string pad(std::size_t n) { return std::string(n, ' '); }

// Plain-text rows for one line at a given character width.
std::vector<std::string> layoutLine(const Document::Line &l, std::size_t width)
{
    std::vector<std::string> rows;
    switch (l.kind) {
    case Document::Kind::Image:   // not text
        break;
    case Document::Kind::Blank:
        rows.emplace_back();
        break;
    case Document::Kind::Rule:
        rows.push_back(std::string(width, '-'));
        break;
    case Document::Kind::Text:
        for (const std::string &part : wrap(l.text, width)) {
            const std::size_t w = displayWidth(part);
            if (l.align == Document::Align::Center)
                rows.push_back(pad((width - std::min(w, width)) / 2) + part);
            else if (l.align == Document::Align::Right)
                rows.push_back(pad(width - std::min(w, width)) + part);
            else
                rows.push_back(part);
        }
        break;
    case Document::Kind::Columns: {
        const std::size_t rw = displayWidth(l.right);
        const std::size_t room = width > rw + 1 ? width - rw - 1 : 1;
        std::vector<std::string> left = wrap(l.text, room);
        for (std::size_t i = 0; i < left.size(); ++i) {
            if (i + 1 < left.size()) {
                rows.push_back(left[i]);
            } else {
                const std::size_t lw = displayWidth(left[i]);
                rows.push_back(left[i] + pad(width - std::min(width, lw + rw)) + l.right);
            }
        }
        break;
    }
    }
    return rows;
}

// Latin-1 letters to ASCII; anything else outside ASCII becomes '?'.
std::string toAscii(const std::string &s)
{
    static const char *latin1 =
        "AAAAAAACEEEEIIIIDNOOOOOxOUUUUYTs"   // U+00C0..U+00DF
        "aaaaaaaceeeeiiiidnooooo/ouuuuyty";  // U+00E0..U+00FF
    std::string out;
    for (std::size_t i = 0; i < s.size();) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        if (c < 0x80) {
            out += char(c);
            ++i;
            continue;
        }
        unsigned int cp = 0;
        std::size_t len = (c & 0xE0) == 0xC0 ? 2 : (c & 0xF0) == 0xE0 ? 3 : (c & 0xF8) == 0xF0 ? 4 : 1;
        cp = len == 2 ? (c & 0x1F) : len == 3 ? (c & 0x0F) : (c & 0x07);
        for (std::size_t k = 1; k < len && i + k < s.size(); ++k)
            cp = (cp << 6) | (static_cast<unsigned char>(s[i + k]) & 0x3F);
        i += len;
        if (cp >= 0xC0 && cp <= 0xFF)
            out += latin1[cp - 0xC0];
        else if (cp == 0x2014 || cp == 0x2013)
            out += '-';
        else if (cp == 0x2019 || cp == 0x2018)
            out += '\'';
        else if (cp == 0x00B7 || cp == 0x2022)
            out += '*';
        else
            out += '?';
    }
    return out;
}

} // namespace

std::size_t displayWidth(const std::string &s)
{
    return std::size_t(std::ranges::count_if(s, [](char c) { return !isContinuation(static_cast<unsigned char>(c)); }));
}

std::string renderText(const Document &doc, int width)
{
    std::string out;
    for (const Document::Line &l : doc.lines) {
        if (l.kind == Document::Kind::Image)   // pictures are for thermal printers
            continue;
        // Double-width text takes two columns per character.
        const std::size_t w = std::size_t(std::max(8, l.big ? width / 2 : width));
        for (const std::string &row : layoutLine(l, w))
            out += row + "\n";
    }
    if (doc.kickDrawer)
        out += "[drawer opened]\n";
    if (doc.cut)
        out += std::string(std::size_t(width), '=') + "\n";
    return out;
}

std::string renderEscPos(const Document &doc, int width)
{
    const std::string ESC = "\x1b", GS = "\x1d";
    std::string out = ESC + "@";                     // initialize
    for (const Document::Line &l : doc.lines) {
        if (l.kind == Document::Kind::Image) {
            // GS v 0: a raster picture, centered; in bands so small printer
            // buffers keep up.
            const Raster &r = *l.raster;
            const int bytes = r.rowBytes();
            out += ESC + "a" + std::string(1, '\x01');
            constexpr int kBand = 128;
            for (int y = 0; y < r.height; y += kBand) {
                const int rows = std::min(kBand, r.height - y);
                out += GS + "v0" + std::string(1, '\0');
                out += char(bytes & 0xff);
                out += char((bytes >> 8) & 0xff);
                out += char(rows & 0xff);
                out += char((rows >> 8) & 0xff);
                out.append(reinterpret_cast<const char *>(r.bits.data()) + std::size_t(y) * std::size_t(bytes),
                           std::size_t(rows) * std::size_t(bytes));
            }
            out += ESC + "a" + std::string(1, '\0');
            continue;
        }
        const std::size_t w = std::size_t(std::max(8, l.big ? width / 2 : width));
        out += GS + "!" + (l.big ? std::string("\x11") : std::string(1, '\0'));   // character size
        out += ESC + "E" + (l.bold ? std::string("\x01") : std::string(1, '\0')); // emphasis
        // Layout is done here (padding), so the printer stays left-aligned.
        out += ESC + "a" + std::string(1, '\0');
        for (const std::string &row : layoutLine(l, w))
            out += toAscii(row) + "\n";
    }
    out += GS + "!" + std::string(1, '\0') + ESC + "E" + std::string(1, '\0');
    if (doc.kickDrawer)
        out += ESC + "p" + std::string(1, '\0') + "\x19\xfa";   // pulse pin 2
    if (doc.cut)
        out += "\n\n\n" + GS + "V" + "\x42" + std::string(1, '\0');   // feed and partial cut
    return out;
}

} // namespace vt::print
