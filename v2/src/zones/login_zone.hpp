/**
 * @file login_zone.hpp
 * @brief Employee login zone
 */

#pragma once

#include "ui/zone.hpp"

namespace vt2 {

class LoginZone : public Zone {
    Q_OBJECT

public:
    explicit LoginZone(QWidget* parent = nullptr);
    ~LoginZone() override;

protected:
    void drawContent(QPainter& painter) override;
};

} // namespace vt2
