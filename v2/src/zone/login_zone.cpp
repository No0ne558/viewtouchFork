// ViewTouch V2 - Login Zone Implementation

#include "zone/login_zone.hpp"
#include "render/renderer.hpp"
#include "terminal/terminal.hpp"

namespace vt {

//=============================================================================
// LoginZone Implementation
//=============================================================================

LoginZone::LoginZone()
    : Zone()
{
    setZoneType(ZoneType::Login);
    setName("Login");
}

void LoginZone::setLoginState(LoginState state) {
    if (m_loginState != state) {
        m_loginState = state;
        setNeedsUpdate(true);
        emit stateChanged(state);
    }
}

void LoginZone::appendDigit(int digit) {
    if (digit >= 0 && digit <= 9) {
        m_inputBuffer += QString::number(digit);
        setNeedsUpdate(true);
    }
}

void LoginZone::clearInput() {
    m_inputBuffer.clear();
    setNeedsUpdate(true);
}

void LoginZone::backspace() {
    if (!m_inputBuffer.isEmpty()) {
        m_inputBuffer.chop(1);
        setNeedsUpdate(true);
    }
}

QString LoginZone::inputDisplay() const {
    if (m_loginState == LoginState::GetPassword) {
        // Show asterisks for password
        return QString(m_inputBuffer.length(), '*');
    }
    return m_inputBuffer;
}

QString LoginZone::promptText() const {
    switch (m_loginState) {
        case LoginState::GetUserId:
            return "Enter Employee ID:";
        case LoginState::GetPassword:
            return "Enter Password:";
        case LoginState::UserOnline:
            return QString("Welcome, %1").arg(m_userName);
        case LoginState::PasswordFailed:
            return "Wrong Password - Try Again";
        case LoginState::UnknownUser:
            return "Unknown Employee ID";
        case LoginState::OnAnotherTerminal:
            return "Already Logged In Elsewhere";
        case LoginState::AlreadyOnClock:
            return "Already Clocked In";
        case LoginState::NotOnClock:
            return "Not Clocked In";
        case LoginState::ClockNotUsed:
            return "Time Clock Not Used";
        case LoginState::OpenCheck:
            return "Has Open Checks";
        case LoginState::AssignedDrawer:
            return "Has Assigned Drawer";
        case LoginState::UserInactive:
            return "Account Disabled";
        case LoginState::NeedBalance:
            return "Must Balance Drawer";
        case LoginState::NotAllowedIn:
            return "Access Denied";
        default:
            return "";
    }
}

void LoginZone::processUserId() {
    bool ok;
    m_userId = m_inputBuffer.toInt(&ok);
    
    if (!ok || m_userId <= 0) {
        setLoginState(LoginState::UnknownUser);
        m_inputBuffer.clear();
        return;
    }
    
    // TODO: Look up employee in EmployeeManager
    // For now, just accept any ID
    m_userName = QString("Employee %1").arg(m_userId);
    
    if (m_requirePassword) {
        m_inputBuffer.clear();
        setLoginState(LoginState::GetPassword);
    } else {
        emit loginSucceeded(m_userId, m_userName);
        setLoginState(LoginState::UserOnline);
    }
}

void LoginZone::processPassword() {
    // TODO: Verify password against employee record
    // For now, accept "1234" as password
    if (m_inputBuffer == "1234" || !m_requirePassword) {
        m_inputBuffer.clear();
        emit loginSucceeded(m_userId, m_userName);
        setLoginState(LoginState::UserOnline);
    } else {
        m_failedAttempts++;
        m_inputBuffer.clear();
        setLoginState(LoginState::PasswordFailed);
        
        // Reset to GetUserId after 3 failures
        if (m_failedAttempts >= 3) {
            m_failedAttempts = 0;
            m_userId = 0;
            setLoginState(LoginState::GetUserId);
        }
    }
}

bool LoginZone::clockOn(int employeeId, int jobId) {
    Q_UNUSED(jobId);
    // TODO: Integrate with LaborManager
    emit clockOnRequested(employeeId);
    return true;
}

bool LoginZone::clockOff(int employeeId) {
    // TODO: Integrate with LaborManager
    emit clockOffRequested(employeeId);
    return true;
}

void LoginZone::renderContent(Renderer& renderer, Terminal* term) {
    Q_UNUSED(term);
    
    int textColor = static_cast<int>(effectiveColor());
    int leftMargin = 10;
    int baselineOffset = 20;  // Approximate font ascent for baseline positioning
    int lineHeight = 30;
    int topMargin = 20;
    
    // Title at top
    QString title = "Welcome";
    renderer.drawText(title, x() + leftMargin, y() + topMargin + baselineOffset, static_cast<uint8_t>(font()), textColor);
    
    // Prompt text in middle
    QString prompt = promptText();
    renderer.drawText(prompt, x() + leftMargin, y() + h() / 3 + baselineOffset, static_cast<uint8_t>(font()), textColor);
    
    // Input display area - show X's for digits, underscore for cursor
    if (m_loginState == LoginState::GetUserId || 
        m_loginState == LoginState::GetPassword) {
        QString display = inputDisplay();
        QString inputLine = display + "_";  // Add cursor
        renderer.drawText(inputLine, x() + leftMargin, y() + h() / 2 + lineHeight + baselineOffset, static_cast<uint8_t>(font()), textColor);
    } else if (m_loginState == LoginState::UserOnline) {
        // Show user name when logged in
        QString hello = QString("Hello, %1").arg(m_userName);
        renderer.drawText(hello, x() + leftMargin, y() + h() / 2 + baselineOffset, static_cast<uint8_t>(font()), textColor);
    }
}

int LoginZone::touch(Terminal* term, int tx, int ty) {
    Q_UNUSED(term);
    Q_UNUSED(tx);
    Q_UNUSED(ty);
    
    // Process input on touch
    if (m_loginState == LoginState::GetUserId) {
        processUserId();
    } else if (m_loginState == LoginState::GetPassword) {
        processPassword();
    } else if (m_loginState == LoginState::PasswordFailed ||
               m_loginState == LoginState::UnknownUser) {
        // Clear error and return to input
        m_inputBuffer.clear();
        setLoginState(LoginState::GetUserId);
    }
    
    return 0;
}

//=============================================================================
// LogoutZone Implementation
//=============================================================================

LogoutZone::LogoutZone()
    : Zone()
{
    setZoneType(ZoneType::Logout);
    setName("Logout");
}

void LogoutZone::renderContent(Renderer& renderer, Terminal* term) {
    Q_UNUSED(term);
    
    QString text = m_awaitingConfirmation ? "Confirm Logout?" : "Logout";
    renderer.drawText(text, x() + 10, y() + h() / 2, static_cast<uint8_t>(font()), 0);
}

int LogoutZone::touch(Terminal* term, int tx, int ty) {
    Q_UNUSED(tx);
    Q_UNUSED(ty);
    
    if (m_confirmLogout && !m_awaitingConfirmation) {
        m_awaitingConfirmation = true;
        setNeedsUpdate(true);
        return 0;
    }
    
    // Get current user from terminal
    int employeeId = term ? term->userId() : 0;
    
    if (m_autoClockOff) {
        // Clock off before logout
        // TODO: Call LaborManager
    }
    
    m_awaitingConfirmation = false;
    emit logoutConfirmed(employeeId);
    setNeedsUpdate(true);
    
    return 0;
}

} // namespace vt
