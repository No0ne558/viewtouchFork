// ViewTouch V2 - Login Zone
// Employee login/logout with clock on/off, shift management

#pragma once

#include "zone/zone.hpp"
#include <QString>
#include <QDateTime>

namespace vt {

//=============================================================================
// Login State Machine
//=============================================================================

enum class LoginState {
    GetUserId,           // Waiting for employee ID input
    GetPassword,         // Waiting for password
    UserOnline,          // User logged in, showing options
    PasswordFailed,      // Wrong password entered
    UnknownUser,         // User ID not found
    OnAnotherTerminal,   // User already logged in elsewhere
    AlreadyOnClock,      // User already clocked in
    NotOnClock,          // User not clocked in
    ClockNotUsed,        // User doesn't use time clock
    OpenCheck,           // User has open checks
    AssignedDrawer,      // User has drawer assigned
    UserInactive,        // User account disabled
    NeedBalance,         // Must balance drawer first
    NotAllowedIn         // User lacks permission
};

//=============================================================================
// LoginZone - Employee Login
//=============================================================================

class LoginZone : public Zone {
    Q_OBJECT

public:
    LoginZone();
    ~LoginZone() override = default;

    // Zone type identification
    const char* typeName() const override { return "LoginZone"; }

    // Rendering
    void renderContent(Renderer& renderer, Terminal* term) override;

    // Touch handling
    int touch(Terminal* term, int tx, int ty) override;

    // Login state
    LoginState loginState() const { return m_loginState; }
    void setLoginState(LoginState state);

    // Input handling
    void appendDigit(int digit);
    void clearInput();
    void backspace();
    QString inputDisplay() const;

    // Clock in/out
    bool clockOn(int employeeId, int jobId = -1);
    bool clockOff(int employeeId);

    // Configuration
    bool requirePassword() const { return m_requirePassword; }
    void setRequirePassword(bool req) { m_requirePassword = req; }

    bool allowClockOnOff() const { return m_allowClockOnOff; }
    void setAllowClockOnOff(bool allow) { m_allowClockOnOff = allow; }

    QString promptText() const;

signals:
    void loginSucceeded(int employeeId, const QString& name);
    void loginFailed(LoginState reason);
    void logoutRequested(int employeeId);
    void clockOnRequested(int employeeId);
    void clockOffRequested(int employeeId);
    void stateChanged(LoginState newState);

private:
    void processUserId();
    void processPassword();

    LoginState m_loginState = LoginState::GetUserId;
    QString m_inputBuffer;
    int m_userId = 0;
    QString m_userName;

    bool m_requirePassword = true;
    bool m_allowClockOnOff = true;
    int m_failedAttempts = 0;
};

//=============================================================================
// LogoutZone - User Logout
//=============================================================================

class LogoutZone : public Zone {
    Q_OBJECT

public:
    LogoutZone();
    ~LogoutZone() override = default;

    const char* typeName() const override { return "LogoutZone"; }

    void renderContent(Renderer& renderer, Terminal* term) override;
    int touch(Terminal* term, int tx, int ty) override;

    // Configuration
    bool confirmLogout() const { return m_confirmLogout; }
    void setConfirmLogout(bool confirm) { m_confirmLogout = confirm; }

    bool autoClockOff() const { return m_autoClockOff; }
    void setAutoClockOff(bool autoClock) { m_autoClockOff = autoClock; }

signals:
    void logoutRequested(int employeeId);
    void logoutConfirmed(int employeeId);
    void logoutCancelled();

private:
    bool m_confirmLogout = true;
    bool m_autoClockOff = false;
    bool m_awaitingConfirmation = false;
};

} // namespace vt
