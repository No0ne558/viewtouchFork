// ViewTouch V2 - Table Zone Implementation

#include "zone/table_zone.hpp"
#include "render/renderer.hpp"
#include "terminal/terminal.hpp"

namespace vt {

//=============================================================================
// TableZone Implementation
//=============================================================================

TableZone::TableZone()
    : Zone()
{
    setZoneType(ZoneType::Table);
    setName("Table");
}

void TableZone::setTableStatus(TableStatus status) {
    if (m_status != status) {
        m_status = status;
        setNeedsUpdate(true);
        
        if (status == TableStatus::Empty) {
            m_seatedTime = QDateTime();
            m_guestCount = 0;
            m_checkIds.clear();
        }
    }
}

int TableZone::elapsedMinutes() const {
    if (!m_seatedTime.isValid()) return 0;
    return m_seatedTime.secsTo(QDateTime::currentDateTime()) / 60;
}

void TableZone::addCheck(int checkId) {
    if (!m_checkIds.contains(checkId)) {
        m_checkIds.append(checkId);
        if (m_status == TableStatus::Empty) {
            setTableStatus(TableStatus::Occupied);
        }
        setNeedsUpdate(true);
    }
}

void TableZone::removeCheck(int checkId) {
    m_checkIds.removeAll(checkId);
    if (m_checkIds.isEmpty() && m_status == TableStatus::Occupied) {
        setTableStatus(TableStatus::Dirty);
    }
    setNeedsUpdate(true);
}

void TableZone::clearChecks() {
    m_checkIds.clear();
    setNeedsUpdate(true);
}

QColor TableZone::statusColor() const {
    switch (m_status) {
        case TableStatus::Empty:    return QColor(0, 128, 0);    // Green
        case TableStatus::Occupied: return QColor(255, 0, 0);    // Red
        case TableStatus::Reserved: return QColor(255, 165, 0);  // Orange
        case TableStatus::Dirty:    return QColor(128, 128, 0);  // Olive
        case TableStatus::OnHold:   return QColor(128, 0, 128);  // Purple
        case TableStatus::Blocked:  return QColor(64, 64, 64);   // Gray
        default: return QColor(0, 0, 0);
    }
}

void TableZone::renderContent(Renderer& renderer, Terminal* term) {
    Q_UNUSED(term);
    
    int cx = x() + w() / 2;
    int textColor = static_cast<int>(effectiveColor());
    uint8_t fontId = static_cast<uint8_t>(font());
    if (fontId == 0) fontId = static_cast<uint8_t>(FontId::Times_20);
    
    // Draw table name/number centered at top
    QString display = m_tableName.isEmpty() ? QString("Table %1").arg(m_tableId) : m_tableName;
    renderer.drawTextCentered(display, cx, y() + 20, fontId, textColor);
    
    // Draw status-specific info
    if (m_status == TableStatus::Occupied) {
        // Show guest count
        QString guests = QString("%1 Guests").arg(m_guestCount);
        uint8_t smallFont = static_cast<uint8_t>(FontId::Times14);
        renderer.drawTextCentered(guests, cx, y() + h() / 2, smallFont, textColor);
        
        // Show elapsed time at bottom
        int mins = elapsedMinutes();
        QString time;
        if (mins >= 60) {
            time = QString("%1:%2").arg(mins / 60).arg(mins % 60, 2, 10, QChar('0'));
        } else {
            time = QString("%1 min").arg(mins);
        }
        renderer.drawTextCentered(time, cx, y() + h() - 20, smallFont, textColor);
        
        // Show check count if stacked
        if (m_checkIds.size() > 1) {
            QString checks = QString("[%1 checks]").arg(m_checkIds.size());
            renderer.drawTextCentered(checks, cx, y() + h() - 35, smallFont, textColor);
        }
    } else if (m_status == TableStatus::Reserved) {
        renderer.drawTextCentered(QString("RESERVED"), cx, y() + h() / 2, fontId, textColor);
    } else if (m_status == TableStatus::Dirty) {
        renderer.drawTextCentered(QString("DIRTY"), cx, y() + h() / 2, fontId, textColor);
    } else if (m_status == TableStatus::Empty) {
        uint8_t smallFont = static_cast<uint8_t>(FontId::Times14);
        renderer.drawTextCentered(QString("Available"), cx, y() + h() / 2, smallFont, textColor);
    }
    
    // Draw server name at bottom if set
    if (!m_serverName.isEmpty() && m_status == TableStatus::Occupied) {
        uint8_t smallFont = static_cast<uint8_t>(FontId::Times14);
        renderer.drawTextCentered(m_serverName, cx, y() + h() - 5, smallFont, textColor);
    }
}

int TableZone::touch(Terminal* term, int tx, int ty) {
    Q_UNUSED(term);
    Q_UNUSED(tx);
    Q_UNUSED(ty);
    
    emit tableSelected(m_tableId);
    emit tableTouched(m_tableId, m_status);
    
    return 0;
}

//=============================================================================
// GuestCountZone Implementation
//=============================================================================

GuestCountZone::GuestCountZone()
    : Zone()
{
    setZoneType(ZoneType::GuestCount);
    setName("Guest Count");
}

void GuestCountZone::setGuestCount(int count) {
    if (count >= m_minGuests && count <= m_maxGuests) {
        m_guestCount = count;
        setNeedsUpdate(true);
        emit guestCountChanged(count);
    }
}

void GuestCountZone::renderContent(Renderer& renderer, Terminal* term) {
    Q_UNUSED(term);
    
    int cx = x() + w() / 2;
    int textColor = static_cast<int>(effectiveColor());
    uint8_t fontId = static_cast<uint8_t>(font());
    if (fontId == 0) fontId = static_cast<uint8_t>(FontId::Times24);
    
    // Draw title
    renderer.drawTextCentered(QString("How Many Guests?"), cx, y() + 25, fontId, textColor);
    
    // Draw current count large in center
    uint8_t bigFont = static_cast<uint8_t>(FontId::Times34B);
    QString count = QString::number(m_guestCount);
    if (m_guestCount == 0) {
        count = "_";  // Show cursor/placeholder
    }
    renderer.drawTextCentered(count, cx, y() + h() / 2 + 10, bigFont, textColor);
    
    // Draw range hint at bottom
    uint8_t smallFont = static_cast<uint8_t>(FontId::Times14);
    QString hint = QString("(%1-%2)").arg(m_minGuests).arg(m_maxGuests);
    renderer.drawTextCentered(hint, cx, y() + h() - 15, smallFont, textColor);
}

int GuestCountZone::touch(Terminal* term, int tx, int ty) {
    Q_UNUSED(term);
    Q_UNUSED(tx);
    Q_UNUSED(ty);
    
    if (m_guestCount > 0) {
        emit guestCountEntered(m_tableId, m_guestCount);
    }
    
    return 0;
}

//=============================================================================
// TransferZone Implementation
//=============================================================================

TransferZone::TransferZone()
    : Zone()
{
    setZoneType(ZoneType::TableAssign);
    setName("Transfer");
}

void TransferZone::renderContent(Renderer& renderer, Terminal* term) {
    Q_UNUSED(term);
    
    int cx = x() + w() / 2;
    int textColor = static_cast<int>(effectiveColor());
    uint8_t fontId = static_cast<uint8_t>(font());
    if (fontId == 0) fontId = static_cast<uint8_t>(FontId::Times_20);
    
    QString label;
    switch (m_transferType) {
        case TransferType::Table:
            label = "Transfer Table";
            break;
        case TransferType::Check:
            label = "Transfer Check";
            break;
        case TransferType::Server:
            label = "Transfer to Server";
            break;
    }
    
    renderer.drawTextCentered(label, cx, y() + h() / 2, fontId, textColor);
}

int TransferZone::touch(Terminal* term, int tx, int ty) {
    Q_UNUSED(term);
    Q_UNUSED(tx);
    Q_UNUSED(ty);
    
    if (m_sourceId > 0 && m_targetServerId > 0) {
        emit transferRequested(m_transferType, m_sourceId, m_targetServerId);
    }
    
    return 0;
}

} // namespace vt
