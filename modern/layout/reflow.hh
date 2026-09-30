#pragma once

#include "layout/zone.hh"

#include <QList>
#include <QRect>

namespace vt::layout {

// Lays a page's zones out again for a narrow (phone) screen, inside `area`,
// in reading order (top to bottom, left to right): labels and panels across
// the full width, buttons in a grid of large touch targets. The grid uses
// 2 columns, or 3 when that is what it takes to fit; rows shrink down to a
// minimum before anything is allowed to run past the bottom.
// Returns one rect per zone, in the order given. Notes (comments) get an
// empty rect: they are not shown.
QList<QRect> reflowZones(const QList<const Zone *> &zones, const QRect &area, int gap = 16);

} // namespace vt::layout
