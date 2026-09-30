#include "layout/reflow.hh"

#include "layout/schema.hh"

#include <algorithm>
#include <numeric>

using namespace Qt::StringLiterals;

namespace vt::layout {

namespace {

enum class Kind { Hidden, Label, Panel, Button };

Kind kindOf(const Zone &z)
{
    if (z.kind == u"comment")
        return Kind::Hidden;
    if (z.kind == u"label")
        return Kind::Label;
    if (z.kind == u"button" || z.kind == u"image" || z.kind == u"table")
        return Kind::Button;
    return schema::isWidgetKind(z.kind) ? Kind::Panel : Kind::Button;
}

// Panels keep their shape scaled to the width, within sensible bounds.
int panelHeight(const Zone &z, const QRect &area)
{
    const double scaled = z.rect.width() > 0 ? double(z.rect.height()) * area.width() / z.rect.width() : 400.0;
    return std::clamp(int(scaled), 240, std::max(240, area.height() * 2 / 3));
}

struct Plan {
    QList<QRect> rects;
    int height = 0;
};

Plan layOut(const QList<const Zone *> &zones, const QList<int> &order, const QRect &area, int gap, int columns,
            int rowHeight, int labelHeight)
{
    Plan plan;
    plan.rects.resize(zones.size());
    const int cellW = (area.width() - gap * (columns - 1)) / columns;
    int y = area.y();
    int col = 0;   // next free column of the current button row
    auto endRow = [&] {
        if (col > 0) {
            y += rowHeight + gap;
            col = 0;
        }
    };
    for (int i : order) {
        const Zone &z = *zones[i];
        switch (kindOf(z)) {
        case Kind::Hidden:
            break;
        case Kind::Label:
            endRow();
            plan.rects[i] = QRect(area.x(), y, area.width(), labelHeight);
            y += labelHeight + gap;
            break;
        case Kind::Panel: {
            endRow();
            const int h = panelHeight(z, area);
            plan.rects[i] = QRect(area.x(), y, area.width(), h);
            y += h + gap;
            break;
        }
        case Kind::Button:
            plan.rects[i] = QRect(area.x() + col * (cellW + gap), y, cellW, rowHeight);
            if (++col == columns)
                endRow();
            break;
        }
    }
    if (col > 0)
        y += rowHeight;
    else if (y > area.y())
        y -= gap;
    plan.height = y - area.y();
    return plan;
}

} // namespace

QList<QRect> reflowZones(const QList<const Zone *> &zones, const QRect &area, int gap)
{
    // Reading order: rows first (zones whose tops are close count as one row).
    QList<int> order(zones.size());
    std::iota(order.begin(), order.end(), 0);
    std::ranges::stable_sort(order, [&](int a, int b) {
        const QRect &ra = zones[a]->rect;
        const QRect &rb = zones[b]->rect;
        if (std::abs(ra.y() - rb.y()) > 24)
            return ra.y() < rb.y();
        return ra.x() < rb.x();
    });

    Plan best;
    for (int columns : {2, 3}) {
        for (int rowHeight : {220, 190, 160, 130, 110}) {
            Plan p = layOut(zones, order, area, gap, columns, rowHeight, rowHeight >= 160 ? 96 : 72);
            if (p.height <= area.height())
                return p.rects;
            if (best.rects.isEmpty() || p.height < best.height)
                best = std::move(p);
        }
    }
    return best.rects;   // doesn't fit: the most compact one (runs past the bottom)
}

} // namespace vt::layout
