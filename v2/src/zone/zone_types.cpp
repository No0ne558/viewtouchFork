/*
 * ViewTouch V2 - Zone Type Classes Implementation
 */

#include "zone/zone_types.hpp"
#include "zone/login_zone.hpp"
#include "zone/table_zone.hpp"
#include "zone/payment_zone.hpp"
#include "render/renderer.hpp"
#include "terminal/terminal.hpp"

#include <QRect>

namespace vt {

/*************************************************************
 * ButtonZone Implementation (ZONE_SIMPLE)
 *************************************************************/
ButtonZone::ButtonZone() {
    setZoneType(ZoneType::Simple);
    setBehavior(ZoneBehavior::Blink);
}

void ButtonZone::setJumpTarget(int pageId, JumpType jt) {
    jumpPageId_ = pageId;
    jumpType_ = jt;
}

void ButtonZone::renderContent(Renderer& renderer, Terminal* term) {
    QRect r(region().x, region().y, region().w, region().h);
    
    // Get color based on state
    uint8_t colorId = state(currentState()).color;
    if (colorId == 0 || colorId == COLOR_DEFAULT) {
        colorId = static_cast<uint8_t>(TextColor::Black);
    }
    
    // Use zone's font
    uint8_t fontId = static_cast<uint8_t>(font());
    if (fontId == 0) {
        fontId = static_cast<uint8_t>(FontId::Times_20);
    }
    
    QString displayText = label_.isEmpty() ? name() : label_;
    renderer.drawText(displayText, r, fontId, colorId, TextAlign::Center);
}

int ButtonZone::touch(Terminal* term, int tx, int ty) {
    int result = Zone::touch(term, tx, ty);
    
    // Perform jump if configured
    if (jumpPageId_ != 0 && term && jumpType_ != JumpType::None) {
        term->jumpToPage(jumpPageId_, jumpType_);
    }
    
    return result;
}

/*************************************************************
 * MessageButtonZone Implementation (ZONE_STANDARD)
 *************************************************************/
MessageButtonZone::MessageButtonZone() {
    setZoneType(ZoneType::Standard);
}

void MessageButtonZone::setConfirm(bool confirm, const QString& msg) {
    confirm_ = confirm;
    confirmMsg_ = msg;
}

int MessageButtonZone::touch(Terminal* term, int tx, int ty) {
    // Send message signal
    if (!message_.isEmpty()) {
        emit messageTriggered(message_, groupId());
        
        // Let terminal process the message
        if (term) {
            term->signal(message_, groupId());
        }
    }
    
    // Then do the normal button behavior (jump, etc.)
    return ButtonZone::touch(term, tx, ty);
}

/*************************************************************
 * ToggleZone Implementation (ZONE_TOGGLE)
 *************************************************************/
ToggleZone::ToggleZone() {
    setZoneType(ZoneType::Toggle);
    setBehavior(ZoneBehavior::Toggle);
}

int ToggleZone::touch(Terminal* term, int tx, int ty) {
    // Cycle through states
    int nextState = (currentState() + 1) % maxStates_;
    setCurrentState(nextState);
    emit stateChanged(this, nextState);
    
    // Don't call parent - we handle state ourselves
    emit touched(this);
    return 1;
}

/*************************************************************
 * ConditionalZone Implementation (ZONE_CONDITIONAL)
 *************************************************************/
ConditionalZone::ConditionalZone() {
    setZoneType(ZoneType::Conditional);
}

bool ConditionalZone::evaluate(Terminal* term) const {
    // Parse and evaluate expression
    // Expression format: "keyword operator value"
    // e.g., "check > 0", "guests >= 1", "drawer = 1"
    
    if (expression_.isEmpty()) {
        return true;  // No condition = always active
    }
    
    // TODO: Implement full expression parser
    // For now, return true (always active)
    return true;
}

void ConditionalZone::render(Renderer& renderer, Terminal* term) {
    // Only render if condition is met
    if (evaluate(term)) {
        setActive(true);
        Zone::render(renderer, term);
    } else {
        setActive(false);
    }
}

/*************************************************************
 * CommentZone Implementation (ZONE_COMMENT)
 *************************************************************/
CommentZone::CommentZone() {
    setZoneType(ZoneType::Comment);
    setBehavior(ZoneBehavior::None);
}

void CommentZone::render(Renderer& renderer, Terminal* term) {
    // Only show to superusers in edit mode
    // TODO: Check user permissions
    bool isSuperuser = true;  // Placeholder
    
    if (isSuperuser && isEdit()) {
        Zone::render(renderer, term);
    }
}

/*************************************************************
 * SwitchZone Implementation (ZONE_SWITCH)
 *************************************************************/
SwitchZone::SwitchZone() {
    setZoneType(ZoneType::Switch);
    setBehavior(ZoneBehavior::Blink);
}

void SwitchZone::setCurrentOption(int idx) {
    if (idx >= 0 && idx < options_.size()) {
        currentOption_ = idx;
    }
}

QString SwitchZone::currentValue() const {
    if (currentOption_ >= 0 && currentOption_ < options_.size()) {
        return options_[currentOption_];
    }
    return QString();
}

void SwitchZone::renderContent(Renderer& renderer, Terminal* term) {
    QRect r(region().x, region().y, region().w, region().h);
    
    uint8_t colorId = state(currentState()).color;
    if (colorId == 0) colorId = static_cast<uint8_t>(TextColor::Black);
    
    uint8_t fontId = static_cast<uint8_t>(font());
    if (fontId == 0) fontId = static_cast<uint8_t>(FontId::Times_20);
    
    // Show label and current value
    QString displayText = name();
    if (!options_.isEmpty()) {
        displayText = QString("%1: %2").arg(name(), currentValue());
    }
    
    renderer.drawText(displayText, r, fontId, colorId, TextAlign::Center);
}

int SwitchZone::touch(Terminal* term, int tx, int ty) {
    Zone::touch(term, tx, ty);
    
    // Cycle to next option
    if (!options_.isEmpty()) {
        currentOption_ = (currentOption_ + 1) % options_.size();
        emit optionChanged(currentOption_, currentValue());
    }
    
    return 1;
}

/*************************************************************
 * ItemZone Implementation (ZONE_ITEM)
 *************************************************************/
ItemZone::ItemZone() {
    setZoneType(ZoneType::Item);
    setBehavior(ZoneBehavior::Blink);
}

void ItemZone::renderContent(Renderer& renderer, Terminal* term) {
    QRect r(region().x, region().y, region().w, region().h);
    
    uint8_t colorId = state(currentState()).color;
    if (colorId == 0) colorId = static_cast<uint8_t>(TextColor::Black);
    
    uint8_t fontId = static_cast<uint8_t>(font());
    if (fontId == 0) fontId = static_cast<uint8_t>(FontId::Times_20);
    
    // Draw item name
    QString displayText = label().isEmpty() ? name() : label();
    
    // If we have a price, show it
    if (!priceStr_.isEmpty()) {
        displayText += "\n" + priceStr_;
    }
    
    renderer.drawText(displayText, r, fontId, colorId, TextAlign::Center);
}

int ItemZone::touch(Terminal* term, int tx, int ty) {
    Zone::touch(term, tx, ty);
    
    // Emit signal that item was ordered
    emit itemOrdered(itemId_, name());
    
    return 1;
}

/*************************************************************
 * QualifierZone Implementation (ZONE_QUALIFIER)
 *************************************************************/
QualifierZone::QualifierZone() {
    setZoneType(ZoneType::Qualifier);
    setBehavior(ZoneBehavior::Blink);
}

int QualifierZone::touch(Terminal* term, int tx, int ty) {
    Zone::touch(term, tx, ty);
    emit qualifierSelected(qualifierType_, name());
    return 1;
}

// TenderZone, TableZone, LoginZone, LogoutZone - implementations now in separate files:
// - zone/payment_zone.cpp (TenderZone and related payment zones)  
// - zone/table_zone.cpp (TableZone and related table zones)
// - zone/login_zone.cpp (LoginZone, LogoutZone)

/*************************************************************
 * CommandZone Implementation (ZONE_COMMAND)
 *************************************************************/
CommandZone::CommandZone() {
    setZoneType(ZoneType::Command);
}

int CommandZone::touch(Terminal* term, int tx, int ty) {
    // Execute command
    if (!command_.isEmpty()) {
        // TODO: Execute system command safely
        // For now, just emit the message
        setMessage(command_);
    }
    
    return MessageButtonZone::touch(term, tx, ty);
}

/*************************************************************
 * StatusZone Implementation (ZONE_STATUS_BUTTON)
 *************************************************************/
StatusZone::StatusZone() {
    setZoneType(ZoneType::StatusButton);
    setBehavior(ZoneBehavior::None);
}

void StatusZone::showMessage(const QString& msg, int duration) {
    statusText_ = msg;
    setNeedsUpdate(true);
    // TODO: Implement timed message clear
}

void StatusZone::renderContent(Renderer& renderer, Terminal* term) {
    QRect r(region().x, region().y, region().w, region().h);
    
    uint8_t colorId = state(currentState()).color;
    if (colorId == 0) colorId = static_cast<uint8_t>(TextColor::Black);
    
    uint8_t fontId = static_cast<uint8_t>(font());
    if (fontId == 0) fontId = static_cast<uint8_t>(FontId::Times_18);
    
    QString text = statusText_.isEmpty() ? name() : statusText_;
    renderer.drawText(text, r, fontId, colorId, TextAlign::Left);
}

/*************************************************************
 * ImageButtonZone Implementation (ZONE_IMAGE_BUTTON)
 *************************************************************/
ImageButtonZone::ImageButtonZone() {
    setZoneType(ZoneType::ImageButton);
}

void ImageButtonZone::renderContent(Renderer& renderer, Terminal* term) {
    QRect r(region().x, region().y, region().w, region().h);
    
    // Try to load and render image
    if (!imagePath_.isEmpty()) {
        if (imagePixmap_.isNull()) {
            imagePixmap_.load(imagePath_);
        }
        
        if (!imagePixmap_.isNull()) {
            // Draw image scaled to fit
            renderer.drawImage(imagePixmap_, r);
        }
    }
    
    // Draw label if present
    if (!label().isEmpty()) {
        uint8_t colorId = state(currentState()).color;
        if (colorId == 0) colorId = static_cast<uint8_t>(TextColor::White);
        
        uint8_t fontId = static_cast<uint8_t>(font());
        if (fontId == 0) fontId = static_cast<uint8_t>(FontId::Times18B);
        
        renderer.drawText(label(), r, fontId, colorId, TextAlign::Center);
    }
}

/*************************************************************
 * IndexTabZone Implementation (ZONE_INDEX_TAB)
 *************************************************************/
IndexTabZone::IndexTabZone() {
    setZoneType(ZoneType::IndexTab);
    setBehavior(ZoneBehavior::Blink);
}

/*************************************************************
 * ZoneFactory Implementation
 *************************************************************/
std::unique_ptr<Zone> ZoneFactory::create(ZoneType type) {
    switch (type) {
        // Basic buttons
        case ZoneType::Simple:
            return std::make_unique<ButtonZone>();
        case ZoneType::Standard:
            return std::make_unique<MessageButtonZone>();
        case ZoneType::Toggle:
            return std::make_unique<ToggleZone>();
        case ZoneType::Conditional:
            return std::make_unique<ConditionalZone>();
        case ZoneType::Comment:
            return std::make_unique<CommentZone>();
        case ZoneType::Switch:
            return std::make_unique<SwitchZone>();
        case ZoneType::StatusButton:
            return std::make_unique<StatusZone>();
        case ZoneType::ImageButton:
            return std::make_unique<ImageButtonZone>();
        case ZoneType::IndexTab:
            return std::make_unique<IndexTabZone>();
            
        // Menu items
        case ZoneType::Item:
        case ZoneType::ItemNormal:
        case ZoneType::ItemModifier:
        case ZoneType::ItemMethod:
        case ZoneType::ItemSubstitute:
        case ZoneType::ItemPound:
        case ZoneType::ItemAdmission:
            return std::make_unique<ItemZone>();
            
        case ZoneType::Qualifier:
            return std::make_unique<QualifierZone>();
            
        // Payments
        case ZoneType::Tender:
            return std::make_unique<TenderZone>();
            
        // Tables
        case ZoneType::Table:
            return std::make_unique<TableZone>();
            
        // User management
        case ZoneType::Login:
            return std::make_unique<LoginZone>();
        case ZoneType::Logout:
            return std::make_unique<LogoutZone>();
            
        // Commands
        case ZoneType::Command:
            return std::make_unique<CommandZone>();
            
        // Default to standard button
        default:
            auto zone = std::make_unique<MessageButtonZone>();
            zone->setZoneType(type);
            return zone;
    }
}

std::unique_ptr<Zone> ZoneFactory::createFromType(int typeId) {
    return create(static_cast<ZoneType>(typeId));
}

ZoneType ZoneFactory::inferType(const Zone* zone) {
    if (!zone) return ZoneType::Undefined;
    return zone->zoneType();
}

} // namespace vt
