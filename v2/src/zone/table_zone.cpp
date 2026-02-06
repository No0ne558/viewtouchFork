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
    
    // Draw table name
    QString display = m_tableName.isEmpty() ? QString::number(m_tableId) : m_tableName;
    renderer.drawText(display, x() + w()/2 - 10, y() + 15, static_cast<uint8_t>(font()), 0);
    
    // Draw status info
    if (m_status == TableStatus::Occupied) {
        // Show guest count
        QString guests = QString("%1G").arg(m_guestCount);
        renderer.drawText(guests, x() + 5, y() + h() - 20, static_cast<uint8_t>(font()), 0);
        
        // Show elapsed time
        int mins = elapsedMinutes();
        QString time = QString("%1m").arg(mins);
        renderer.drawText(time, x() + w() - 30, y() + h() - 20, static_cast<uint8_t>(font()), 0);
        
        // Show check count if stacked
        if (m_checkIds.size() > 1) {
            QString checks = QString("[%1]").arg(m_checkIds.size());
            renderer.drawText(checks, x() + w()/2 - 10, y() + h() - 20, static_cast<uint8_t>(font()), 0);
        }
    } else if (m_status == TableStatus::Reserved) {
        renderer.drawText(QString("RSVD"), x() + w()/2 - 15, y() + h() - 20, static_cast<uint8_t>(font()), 0);
    }
    
    // Server name
    if (!m_serverName.isEmpty()) {
        renderer.drawText(m_serverName.left(8), x() + 5, y() + h()/2, static_cast<uint8_t>(font()), 0);
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
    
    // Draw label
    renderer.drawText(QString("Guests:"), x() + 10, y() + 15, static_cast<uint8_t>(font()), 0);
    
    // Draw current count
    QString count = QString::number(m_guestCount);
    renderer.drawText(count, x() + w()/2, y() + h()/2, static_cast<uint8_t>(font()), 0);
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
    
    renderer.drawText(label, x() + 10, y() + h()/2, static_cast<uint8_t>(font()), 0);
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
