/**
 * @file table_zone.cpp
 */

#include "zones/table_zone.hpp"
#include <QPainter>

namespace vt2 {

TableZone::TableZone(QWidget* parent)
    : Zone(ZoneType::Table, parent) {
}

TableZone::~TableZone() = default;

void TableZone::drawContent(QPainter& painter) {
    painter.setPen(fgColor_);
    painter.drawText(rect(), Qt::AlignCenter, "Table Zone\n(TODO)");
}

} // namespace vt2
