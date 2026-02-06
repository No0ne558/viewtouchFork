/*
 * ViewTouch V2 - Zone Type Classes
 * Implementations for all zone types from original ViewTouch
 */

#pragma once

#include "zone/zone.hpp"
#include "core/types.hpp"

#include <QString>
#include <QPixmap>
#include <functional>

namespace vt {

// Forward declarations
class Terminal;
class Renderer;

/*************************************************************
 * ButtonZone - Base class for button-style zones (ZONE_SIMPLE)
 * Just performs a jump when touched, no message
 *************************************************************/
class ButtonZone : public Zone {
    Q_OBJECT

public:
    ButtonZone();
    
    const char* typeName() const override { return "ButtonZone"; }
    
    // Label (displayed text)
    void setLabel(const QString& label) { label_ = label; setName(label); }
    QString label() const { return label_; }
    
    // Jump target
    void setJumpTarget(int pageId, JumpType jt = JumpType::Normal);
    int jumpPageId() const { return jumpPageId_; }
    JumpType jumpType() const { return jumpType_; }
    
protected:
    void renderContent(Renderer& renderer, Terminal* term) override;
    int touch(Terminal* term, int tx, int ty) override;
    
private:
    QString label_;
    int jumpPageId_ = 0;
    JumpType jumpType_ = JumpType::None;
};

/*************************************************************
 * MessageButtonZone - Button with message signal (ZONE_STANDARD)
 * Sends a signal/message when touched, then optionally jumps
 *************************************************************/
class MessageButtonZone : public ButtonZone {
    Q_OBJECT

public:
    MessageButtonZone();
    
    const char* typeName() const override { return "MessageButtonZone"; }
    
    // Message to send when touched
    void setMessage(const QString& msg) { message_ = msg; }
    QString message() const { return message_; }
    
    // Confirmation dialog
    void setConfirm(bool confirm, const QString& msg = QString());
    bool needsConfirm() const { return confirm_; }
    QString confirmMessage() const { return confirmMsg_; }

signals:
    void messageTriggered(const QString& message, int groupId);

protected:
    int touch(Terminal* term, int tx, int ty) override;
    
private:
    QString message_;
    bool confirm_ = false;
    QString confirmMsg_;
};

/*************************************************************
 * ToggleZone - Button that cycles through states (ZONE_TOGGLE)
 * Each touch advances to next state, wrapping around
 *************************************************************/
class ToggleZone : public ButtonZone {
    Q_OBJECT

public:
    ToggleZone();
    
    const char* typeName() const override { return "ToggleZone"; }
    
    // Number of toggle states (default 2: on/off)
    void setMaxStates(int max) { maxStates_ = max; }
    int maxStates() const { return maxStates_; }

protected:
    int touch(Terminal* term, int tx, int ty) override;
    
private:
    int maxStates_ = 2;
};

/*************************************************************
 * ConditionalZone - Button that shows/hides based on condition (ZONE_CONDITIONAL)
 * Evaluates an expression to determine visibility
 *************************************************************/
class ConditionalZone : public MessageButtonZone {
    Q_OBJECT

public:
    ConditionalZone();
    
    const char* typeName() const override { return "ConditionalZone"; }
    
    // Condition expression
    void setExpression(const QString& expr) { expression_ = expr; }
    QString expression() const { return expression_; }
    
    // Evaluate condition
    bool evaluate(Terminal* term) const;
    
    void render(Renderer& renderer, Terminal* term) override;

private:
    QString expression_;
};

/*************************************************************
 * CommentZone - Hidden zone visible only to superusers (ZONE_COMMENT)
 *************************************************************/
class CommentZone : public Zone {
    Q_OBJECT

public:
    CommentZone();
    
    const char* typeName() const override { return "CommentZone"; }
    
    void render(Renderer& renderer, Terminal* term) override;

protected:
    int touch(Terminal* term, int tx, int ty) override { return 0; }
};

/*************************************************************
 * SwitchZone - Settings selection button (ZONE_SWITCH)
 * Shows current value and cycles through options
 *************************************************************/
class SwitchZone : public ButtonZone {
    Q_OBJECT

public:
    SwitchZone();
    
    const char* typeName() const override { return "SwitchZone"; }
    
    // Options to cycle through
    void setOptions(const QStringList& opts) { options_ = opts; }
    QStringList options() const { return options_; }
    
    int currentOption() const { return currentOption_; }
    void setCurrentOption(int idx);
    QString currentValue() const;

signals:
    void optionChanged(int index, const QString& value);

protected:
    void renderContent(Renderer& renderer, Terminal* term) override;
    int touch(Terminal* term, int tx, int ty) override;
    
private:
    QStringList options_;
    int currentOption_ = 0;
};

/*************************************************************
 * ItemZone - Menu item ordering button (ZONE_ITEM)
 * Adds an item to the current order
 *************************************************************/
class ItemZone : public ButtonZone {
    Q_OBJECT

public:
    ItemZone();
    
    const char* typeName() const override { return "ItemZone"; }
    
    // Item details
    void setItemId(int id) { itemId_ = id; }
    int itemId() const { return itemId_; }
    
    void setPrice(int cents) { price_ = cents; }
    int price() const { return price_; }
    
    void setPriceString(const QString& str) { priceStr_ = str; }
    QString priceString() const { return priceStr_; }

signals:
    void itemOrdered(int itemId, const QString& name);

protected:
    void renderContent(Renderer& renderer, Terminal* term) override;
    int touch(Terminal* term, int tx, int ty) override;
    
private:
    int itemId_ = 0;
    int price_ = 0;
    QString priceStr_;
};

/*************************************************************
 * QualifierZone - Modifier button (ZONE_QUALIFIER)
 * Adds qualifiers like "no", "extra", "lite" to items
 *************************************************************/
class QualifierZone : public ButtonZone {
    Q_OBJECT

public:
    QualifierZone();
    
    const char* typeName() const override { return "QualifierZone"; }
    
    void setQualifierType(int type) { qualifierType_ = type; }
    int qualifierType() const { return qualifierType_; }

signals:
    void qualifierSelected(int type, const QString& name);

protected:
    int touch(Terminal* term, int tx, int ty) override;
    
private:
    int qualifierType_ = 0;
};

/*************************************************************
 * TenderZone - Payment tender button (ZONE_TENDER)
 * Handles payment processing
 *************************************************************/
class TenderZone : public ButtonZone {
    Q_OBJECT

public:
    TenderZone();
    
    const char* typeName() const override { return "TenderZone"; }
    
    void setTenderType(int type) { tenderType_ = type; }
    int tenderType() const { return tenderType_; }

signals:
    void tenderSelected(int type);

protected:
    int touch(Terminal* term, int tx, int ty) override;
    
private:
    int tenderType_ = 0;
};

/*************************************************************
 * TableZone - Table selection/status button (ZONE_TABLE)
 * Shows table status and handles table selection
 *************************************************************/
class TableZone : public ButtonZone {
    Q_OBJECT

public:
    TableZone();
    
    const char* typeName() const override { return "TableZone"; }
    
    void setTableId(int id) { tableId_ = id; }
    int tableId() const { return tableId_; }
    
    // Table status
    enum class Status { Empty, Occupied, Reserved, Dirty };
    void setStatus(Status s) { status_ = s; }
    Status status() const { return status_; }

signals:
    void tableSelected(int tableId);

protected:
    void renderContent(Renderer& renderer, Terminal* term) override;
    int touch(Terminal* term, int tx, int ty) override;
    
private:
    int tableId_ = 0;
    Status status_ = Status::Empty;
};

/*************************************************************
 * LoginZone - User login button (ZONE_LOGIN)
 * Takes user ID input for authentication
 *************************************************************/
class LoginZone : public ButtonZone {
    Q_OBJECT

public:
    LoginZone();
    
    const char* typeName() const override { return "LoginZone"; }

signals:
    void loginRequested();

protected:
    int touch(Terminal* term, int tx, int ty) override;
};

/*************************************************************
 * LogoutZone - User logout button (ZONE_LOGOUT)
 *************************************************************/
class LogoutZone : public ButtonZone {
    Q_OBJECT

public:
    LogoutZone();
    
    const char* typeName() const override { return "LogoutZone"; }

signals:
    void logoutRequested();

protected:
    int touch(Terminal* term, int tx, int ty) override;
};

/*************************************************************
 * CommandZone - System command button (ZONE_COMMAND)
 * Executes system commands
 *************************************************************/
class CommandZone : public MessageButtonZone {
    Q_OBJECT

public:
    CommandZone();
    
    const char* typeName() const override { return "CommandZone"; }
    
    void setCommand(const QString& cmd) { command_ = cmd; }
    QString command() const { return command_; }

protected:
    int touch(Terminal* term, int tx, int ty) override;
    
private:
    QString command_;
};

/*************************************************************
 * StatusZone - Status display zone (ZONE_STATUS_BUTTON)
 * Shows status messages, errors, etc.
 *************************************************************/
class StatusZone : public Zone {
    Q_OBJECT

public:
    StatusZone();
    
    const char* typeName() const override { return "StatusZone"; }
    
    void setStatusText(const QString& text) { statusText_ = text; }
    QString statusText() const { return statusText_; }

public slots:
    void showMessage(const QString& msg, int duration = 0);

protected:
    void renderContent(Renderer& renderer, Terminal* term) override;
    int touch(Terminal* term, int tx, int ty) override { return 0; }
    
private:
    QString statusText_;
};

/*************************************************************
 * ImageButtonZone - Button with custom image (ZONE_IMAGE_BUTTON)
 *************************************************************/
class ImageButtonZone : public ButtonZone {
    Q_OBJECT

public:
    ImageButtonZone();
    
    const char* typeName() const override { return "ImageButtonZone"; }
    
    void setImagePath(const QString& path) { imagePath_ = path; }
    QString imagePath() const { return imagePath_; }

protected:
    void renderContent(Renderer& renderer, Terminal* term) override;
    
private:
    QString imagePath_;
    QPixmap imagePixmap_;
};

/*************************************************************
 * IndexTabZone - Navigation tab button (ZONE_INDEX_TAB)
 * Used on index pages for quick category navigation
 *************************************************************/
class IndexTabZone : public ButtonZone {
    Q_OBJECT

public:
    IndexTabZone();
    
    const char* typeName() const override { return "IndexTabZone"; }
};

/*************************************************************
 * ZoneFactory - Creates zones by type
 *************************************************************/
class ZoneFactory {
public:
    static std::unique_ptr<Zone> create(ZoneType type);
    static std::unique_ptr<Zone> createFromType(int typeId);
    
    // Get zone type from an existing zone based on its properties
    static ZoneType inferType(const Zone* zone);
};

} // namespace vt
