/**
 * @file table_zone.hpp
 * @brief Table selection/display zone
 */

#pragma once

#include "ui/zone.hpp"

namespace vt2 {

class TableZone : public Zone {
    Q_OBJECT

public:
    explicit TableZone(QWidget* parent = nullptr);
    ~TableZone() override;

protected:
    void drawContent(QPainter& painter) override;
};

} // namespace vt2
