/**
 * @file payment_zone.cpp
 */

#include "zones/payment_zone.hpp"
#include <QPainter>

namespace vt2 {

PaymentZone::PaymentZone(QWidget* parent)
    : Zone(ZoneType::Payment, parent) {
}

PaymentZone::~PaymentZone() = default;

void PaymentZone::drawContent(QPainter& painter) {
    painter.setPen(fgColor_);
    painter.drawText(rect(), Qt::AlignCenter, "Payment Zone\n(TODO)");
}

} // namespace vt2
