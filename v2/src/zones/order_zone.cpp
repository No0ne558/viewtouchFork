/**
 * @file order_zone.cpp
 * @brief Order zone implementation (stub)
 */

#include "zones/order_zone.hpp"
#include <QPainter>

namespace vt2 {

OrderZone::OrderZone(QWidget* parent)
    : Zone(ZoneType::Order, parent) {
}

OrderZone::~OrderZone() = default;

void OrderZone::drawContent(QPainter& painter) {
    painter.setPen(fgColor_);
    painter.drawText(rect(), Qt::AlignCenter, "Order Zone\n(TODO)");
}

} // namespace vt2
