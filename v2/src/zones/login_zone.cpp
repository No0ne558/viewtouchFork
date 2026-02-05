/**
 * @file login_zone.cpp
 */

#include "zones/login_zone.hpp"
#include <QPainter>

namespace vt2 {

LoginZone::LoginZone(QWidget* parent)
    : Zone(ZoneType::Login, parent) {
}

LoginZone::~LoginZone() = default;

void LoginZone::drawContent(QPainter& painter) {
    painter.setPen(fgColor_);
    painter.drawText(rect(), Qt::AlignCenter, "Login Zone\n(TODO)");
}

} // namespace vt2
