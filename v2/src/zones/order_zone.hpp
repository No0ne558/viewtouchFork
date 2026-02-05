/**
 * @file order_zone.hpp
 * @brief Order display zone - shows current order items
 */

#pragma once

#include "ui/zone.hpp"
#include "data/order.hpp"
#include <vector>

namespace vt2 {

/**
 * @brief Zone for displaying order items
 */
class OrderZone : public Zone {
    Q_OBJECT

public:
    explicit OrderZone(QWidget* parent = nullptr);
    ~OrderZone() override;

protected:
    void drawContent(QPainter& painter) override;
};

} // namespace vt2
