/**
 * @file report_zone.cpp
 */

#include "zones/report_zone.hpp"
#include <QPainter>

namespace vt2 {

ReportZone::ReportZone(QWidget* parent)
    : Zone(ZoneType::Report, parent) {
}

ReportZone::~ReportZone() = default;

void ReportZone::drawContent(QPainter& painter) {
    painter.setPen(fgColor_);
    painter.drawText(rect(), Qt::AlignCenter, "Report Zone\n(TODO)");
}

} // namespace vt2
