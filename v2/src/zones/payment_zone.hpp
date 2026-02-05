/**
 * @file payment_zone.hpp
 * @brief Payment processing zone
 */

#pragma once

#include "ui/zone.hpp"

namespace vt2 {

class PaymentZone : public Zone {
    Q_OBJECT

public:
    explicit PaymentZone(QWidget* parent = nullptr);
    ~PaymentZone() override;

protected:
    void drawContent(QPainter& painter) override;
};

} // namespace vt2
