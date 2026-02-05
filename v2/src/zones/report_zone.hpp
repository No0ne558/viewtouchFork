/**
 * @file report_zone.hpp
 * @brief Report display zone
 */

#pragma once

#include "ui/zone.hpp"

namespace vt2 {

class ReportZone : public Zone {
    Q_OBJECT

public:
    explicit ReportZone(QWidget* parent = nullptr);
    ~ReportZone() override;

protected:
    void drawContent(QPainter& painter) override;
};

} // namespace vt2
