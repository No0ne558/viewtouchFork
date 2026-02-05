/**
 * @file settings_zone.hpp
 * @brief Settings zone for system configuration (superuser only)
 */

#pragma once

#include "ui/zone.hpp"
#include <QPushButton>
#include <QLabel>
#include <QVBoxLayout>
#include <QGridLayout>
#include <vector>

namespace vt2 {

/**
 * @brief Settings zone - restricted to superuser access
 * 
 * Provides access to:
 * - Hardware settings (displays, printers)
 * - Tax configuration
 * - System clear (with 10-tap safety)
 */
class SettingsZone : public Zone {
    Q_OBJECT

public:
    explicit SettingsZone(QWidget* parent = nullptr);
    ~SettingsZone() override;

signals:
    void hardwareRequested();
    void taxRequested();
    void clearSystemRequested();
    void backRequested();

protected:
    void drawContent(QPainter& painter) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    void setupUI();
    void updateSizes();
    QPushButton* createSettingsButton(const QString& text, const QColor& color);

    QLabel* m_titleLabel{nullptr};
    QPushButton* m_hardwareBtn{nullptr};
    QPushButton* m_taxBtn{nullptr};
    QPushButton* m_clearSystemBtn{nullptr};
    QPushButton* m_backBtn{nullptr};
    QVBoxLayout* m_mainLayout{nullptr};
    QGridLayout* m_buttonLayout{nullptr};
    std::vector<QPushButton*> m_allButtons;
};

/**
 * @brief Clear System confirmation zone with 10-tap safety
 */
class ClearSystemZone : public Zone {
    Q_OBJECT

public:
    explicit ClearSystemZone(QWidget* parent = nullptr);
    ~ClearSystemZone() override;

    void resetTapCount();

signals:
    void clearConfirmed();
    void backRequested();

protected:
    void drawContent(QPainter& painter) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    void setupUI();
    void updateSizes();
    void onClearTapped();

    QLabel* m_titleLabel{nullptr};
    QLabel* m_warningLabel{nullptr};
    QLabel* m_instructionLabel{nullptr};
    QLabel* m_tapCountLabel{nullptr};
    QPushButton* m_clearBtn{nullptr};
    QPushButton* m_backBtn{nullptr};
    QVBoxLayout* m_mainLayout{nullptr};
    
    int m_tapCount{0};
    static constexpr int REQUIRED_TAPS = 10;
};

} // namespace vt2
