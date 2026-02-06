// ViewTouch V2 - Table Zone
// Interactive table status/selection with visual feedback

#pragma once

#include "zone/zone.hpp"
#include <QString>
#include <QDateTime>
#include <QColor>
#include <QList>
#include <QMap>

namespace vt {

//=============================================================================
// Table Status
//=============================================================================

enum class TableStatus {
    Empty,       // Table is available
    Occupied,    // Table has active check(s)
    Reserved,    // Table is reserved
    Dirty,       // Table needs cleaning
    OnHold,      // Table on hold
    Blocked      // Table blocked/unavailable
};

//=============================================================================
// TableInfo - Runtime table state
//=============================================================================

struct TableInfo {
    int tableId = 0;
    QString tableName;
    TableStatus status = TableStatus::Empty;

    // Occupancy
    int guestCount = 0;
    int maxCapacity = 4;
    QDateTime seatedTime;

    // Assignment
    int serverId = 0;
    QString serverName;
    int sectionId = 0;

    // Checks on this table
    QList<int> checkIds;
    int totalAmount = 0;  // cents

    // Visual state
    bool blinking = false;
    bool stacked = false;  // Multiple checks

    // Timing
    int elapsedMinutes() const {
        if (!seatedTime.isValid()) return 0;
        return seatedTime.secsTo(QDateTime::currentDateTime()) / 60;
    }
    
    QColor statusColor() const {
        switch (status) {
            case TableStatus::Empty:    return QColor(0, 128, 0);    // Green
            case TableStatus::Occupied: return QColor(255, 0, 0);    // Red
            case TableStatus::Reserved: return QColor(255, 165, 0);  // Orange
            case TableStatus::Dirty:    return QColor(128, 128, 0);  // Olive
            case TableStatus::OnHold:   return QColor(128, 0, 128);  // Purple
            case TableStatus::Blocked:  return QColor(64, 64, 64);   // Gray
            default: return QColor(0, 0, 0);
        }
    }
};

//=============================================================================
// TableZone - Table Display and Selection
//=============================================================================

class TableZone : public Zone {
    Q_OBJECT

public:
    TableZone();
    ~TableZone() override = default;

    // Zone type identification
    const char* typeName() const override { return "TableZone"; }

    // Rendering
    void renderContent(Renderer& renderer, Terminal* term) override;

    // Touch handling
    int touch(Terminal* term, int tx, int ty) override;

    // Table assignment
    void setTableId(int id) { m_tableId = id; }
    int tableId() const { return m_tableId; }

    void setTableName(const QString& name) { m_tableName = name; }
    QString tableName() const { return m_tableName; }

    // Status
    void setTableStatus(TableStatus status);
    TableStatus tableStatus() const { return m_status; }

    // Occupancy
    void setGuestCount(int count) { m_guestCount = count; }
    int guestCount() const { return m_guestCount; }

    void setMaxCapacity(int cap) { m_maxCapacity = cap; }
    int maxCapacity() const { return m_maxCapacity; }

    // Server assignment
    void setServerId(int id) { m_serverId = id; }
    int serverId() const { return m_serverId; }

    void setServerName(const QString& name) { m_serverName = name; }
    QString serverName() const { return m_serverName; }

    // Timing
    void setSeatedTime(const QDateTime& time) { m_seatedTime = time; }
    QDateTime seatedTime() const { return m_seatedTime; }
    int elapsedMinutes() const;

    // Checks
    void addCheck(int checkId);
    void removeCheck(int checkId);
    void clearChecks();
    QList<int> checkIds() const { return m_checkIds; }
    bool hasChecks() const { return !m_checkIds.isEmpty(); }

    // Visual
    void setBlinking(bool blink) { m_blinking = blink; }
    bool isBlinking() const { return m_blinking; }

    QColor statusColor() const;

signals:
    void tableSelected(int tableId);
    void tableTouched(int tableId, TableStatus status);
    void guestsSeated(int tableId, int guestCount);
    void tableCleared(int tableId);

private:
    int m_tableId = 0;
    QString m_tableName;
    TableStatus m_status = TableStatus::Empty;
    
    int m_guestCount = 0;
    int m_maxCapacity = 4;
    QDateTime m_seatedTime;
    
    int m_serverId = 0;
    QString m_serverName;
    
    QList<int> m_checkIds;
    bool m_blinking = false;
};

//=============================================================================
// GuestCountZone - Enter number of guests
//=============================================================================

class GuestCountZone : public Zone {
    Q_OBJECT

public:
    GuestCountZone();
    ~GuestCountZone() override = default;

    const char* typeName() const override { return "GuestCountZone"; }

    void renderContent(Renderer& renderer, Terminal* term) override;
    int touch(Terminal* term, int tx, int ty) override;

    // Guest count
    int guestCount() const { return m_guestCount; }
    void setGuestCount(int count);

    int minGuests() const { return m_minGuests; }
    void setMinGuests(int min) { m_minGuests = min; }

    int maxGuests() const { return m_maxGuests; }
    void setMaxGuests(int max) { m_maxGuests = max; }

signals:
    void guestCountEntered(int tableId, int count);
    void guestCountChanged(int count);

private:
    int m_guestCount = 0;
    int m_minGuests = 1;
    int m_maxGuests = 99;
    int m_tableId = 0;
};

//=============================================================================
// TransferZone - Transfer tables/checks between servers
//=============================================================================

class TransferZone : public Zone {
    Q_OBJECT

public:
    enum class TransferType {
        Table,      // Transfer entire table
        Check,      // Transfer single check
        Server      // Transfer to specific server
    };

    TransferZone();
    ~TransferZone() override = default;

    const char* typeName() const override { return "TransferZone"; }

    void renderContent(Renderer& renderer, Terminal* term) override;
    int touch(Terminal* term, int tx, int ty) override;

    // Transfer mode
    TransferType transferType() const { return m_transferType; }
    void setTransferType(TransferType type) { m_transferType = type; }

    // Source
    int sourceId() const { return m_sourceId; }
    void setSourceId(int id) { m_sourceId = id; }

    // Target
    int targetServerId() const { return m_targetServerId; }
    void setTargetServerId(int id) { m_targetServerId = id; }

signals:
    void transferRequested(TransferType type, int sourceId, int targetId);
    void transferCompleted(TransferType type, int sourceId, int targetId);

private:
    TransferType m_transferType = TransferType::Table;
    int m_sourceId = 0;
    int m_targetServerId = 0;
};

} // namespace vt
