/**
 * @file login_zone.hpp
 * @brief Employee login zone with PIN keypad
 */

#pragma once

#include "ui/zone.hpp"
#include <QLineEdit>
#include <QLabel>
#include <QGridLayout>
#include <QPushButton>
#include <vector>

namespace vt2 {

class LoginZone : public Zone {
    Q_OBJECT
    Q_PROPERTY(QString enteredPin READ enteredPin NOTIFY pinChanged)

public:
    explicit LoginZone(QWidget* parent = nullptr);
    ~LoginZone() override;

    void clearPin();
    void setErrorMessage(const QString& message);
    void clearError();
    
    [[nodiscard]] QString enteredPin() const { return m_enteredPin; }

signals:
    void pinEntered(const QString& action);
    void pinChanged();
    void loginSuccessful(int employeeId);
    void loginFailed();

private slots:
    void onDigitPressed(int digit);
    void onClearPressed();
    void onBackspacePressed();

protected:
    void drawContent(QPainter& painter) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    void setupKeypad();
    void updatePinDisplay();
    void updateSizes();
    QPushButton* createKeypadButton(const QString& text);

    QLineEdit* m_pinDisplay{nullptr};
    QLabel* m_promptLabel{nullptr};
    QLabel* m_errorLabel{nullptr};
    QWidget* m_keypadWidget{nullptr};
    QGridLayout* m_keypadLayout{nullptr};
    QString m_enteredPin;
    std::vector<QPushButton*> m_allButtons;
    QPushButton* m_clearBtn{nullptr};
    QPushButton* m_backBtn{nullptr};
    
    static constexpr int MAX_PIN_LENGTH = 5;  // PINs are 1-5 digits
};

} // namespace vt2
