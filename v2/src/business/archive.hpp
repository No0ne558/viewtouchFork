// ViewTouch V2 - Archive System
// Historical data storage, end-of-day archives, backup/restore

#pragma once

#include <QObject>
#include <QString>
#include <QDateTime>
#include <QDate>
#include <QList>
#include <QJsonObject>

namespace vt2 {

//=============================================================================
// Archive Types
//=============================================================================

enum class ArchiveType {
    Daily,          // End of day archive
    Weekly,         // Weekly summary
    Monthly,        // Monthly summary
    Yearly,         // Yearly summary
    Backup,         // Manual backup
    Emergency       // Emergency backup
};

//=============================================================================
// ArchiveRecord - Metadata about an archive
//=============================================================================

class ArchiveRecord : public QObject {
    Q_OBJECT

public:
    explicit ArchiveRecord(QObject* parent = nullptr);
    ~ArchiveRecord() override = default;

    // Identification
    int id() const { return m_id; }
    void setId(int id) { m_id = id; }

    ArchiveType type() const { return m_type; }
    void setType(ArchiveType type) { m_type = type; }

    // Date range covered
    QDate dateFrom() const { return m_dateFrom; }
    void setDateFrom(const QDate& date) { m_dateFrom = date; }

    QDate dateTo() const { return m_dateTo; }
    void setDateTo(const QDate& date) { m_dateTo = date; }

    // Creation info
    QDateTime createdAt() const { return m_createdAt; }
    void setCreatedAt(const QDateTime& dt) { m_createdAt = dt; }

    int createdByEmployee() const { return m_createdBy; }
    void setCreatedByEmployee(int empId) { m_createdBy = empId; }

    // File info
    QString filePath() const { return m_filePath; }
    void setFilePath(const QString& path) { m_filePath = path; }

    qint64 fileSize() const { return m_fileSize; }
    void setFileSize(qint64 size) { m_fileSize = size; }

    QString checksum() const { return m_checksum; }
    void setChecksum(const QString& sum) { m_checksum = sum; }

    // Summary data
    int checkCount() const { return m_checkCount; }
    void setCheckCount(int count) { m_checkCount = count; }

    int totalSales() const { return m_totalSales; }
    void setTotalSales(int cents) { m_totalSales = cents; }

    int totalTax() const { return m_totalTax; }
    void setTotalTax(int cents) { m_totalTax = cents; }

    // Status
    bool isCompressed() const { return m_compressed; }
    void setCompressed(bool c) { m_compressed = c; }

    bool isVerified() const { return m_verified; }
    void setVerified(bool v) { m_verified = v; }

    // Notes
    QString notes() const { return m_notes; }
    void setNotes(const QString& notes) { m_notes = notes; }

    // Serialization
    QJsonObject toJson() const;
    static ArchiveRecord* fromJson(const QJsonObject& json, QObject* parent = nullptr);

private:
    int m_id = 0;
    ArchiveType m_type = ArchiveType::Daily;
    QDate m_dateFrom;
    QDate m_dateTo;
    QDateTime m_createdAt;
    int m_createdBy = 0;
    QString m_filePath;
    qint64 m_fileSize = 0;
    QString m_checksum;
    int m_checkCount = 0;
    int m_totalSales = 0;
    int m_totalTax = 0;
    bool m_compressed = false;
    bool m_verified = false;
    QString m_notes;
};

//=============================================================================
// ArchiveData - Actual archived data container
//=============================================================================

struct ArchiveData {
    QDate archiveDate;

    // Checks
    QList<QJsonObject> closedChecks;
    QList<QJsonObject> voidedChecks;

    // Payments
    QList<QJsonObject> payments;
    QList<QJsonObject> creditCardBatches;

    // Labor
    QList<QJsonObject> workEntries;
    QList<QJsonObject> tipEntries;

    // Drawer sessions
    QList<QJsonObject> drawerSessions;

    // Exceptions
    QList<QJsonObject> exceptions;
    QList<QJsonObject> comps;
    QList<QJsonObject> voids;

    // Daily totals
    QJsonObject dailySummary;
};

//=============================================================================
// ArchiveManager - Singleton for archive operations
//=============================================================================

class ArchiveManager : public QObject {
    Q_OBJECT

public:
    static ArchiveManager* instance();

    // Archive directory
    void setArchiveDirectory(const QString& dir);
    QString archiveDirectory() const { return m_archiveDir; }

    // Create archives
    ArchiveRecord* createDailyArchive(const QDate& date, int employeeId = 0);
    ArchiveRecord* createBackup(const QString& notes = QString());

    // List archives
    QList<ArchiveRecord*> allArchives() const { return m_archives; }
    QList<ArchiveRecord*> archivesForDate(const QDate& date);
    QList<ArchiveRecord*> archivesInRange(const QDate& from, const QDate& to);
    ArchiveRecord* findArchive(int id);

    // Load archive data
    ArchiveData loadArchive(int archiveId);
    ArchiveData loadArchiveByDate(const QDate& date);

    // Restore from archive
    bool restoreArchive(int archiveId, bool overwrite = false);

    // Verify archive integrity
    bool verifyArchive(int archiveId);

    // Delete old archives
    int deleteArchivesOlderThan(const QDate& cutoff);

    // Compression
    void setCompressionEnabled(bool enable) { m_compressionEnabled = enable; }
    bool isCompressionEnabled() const { return m_compressionEnabled; }

    // Auto-archive settings
    void setAutoArchiveEnabled(bool enable) { m_autoArchiveEnabled = enable; }
    bool isAutoArchiveEnabled() const { return m_autoArchiveEnabled; }

    void setRetentionDays(int days) { m_retentionDays = days; }
    int retentionDays() const { return m_retentionDays; }

    // Check if date has been archived
    bool isDateArchived(const QDate& date) const;

    // Index persistence
    bool saveIndex();
    bool loadIndex();

signals:
    void archiveCreated(ArchiveRecord* record);
    void archiveDeleted(int archiveId);
    void archiveRestored(int archiveId);
    void archiveProgress(int percent, const QString& status);
    void archiveError(const QString& error);

private:
    ArchiveManager(QObject* parent = nullptr);
    static ArchiveManager* s_instance;

    QString m_archiveDir;
    QList<ArchiveRecord*> m_archives;
    int m_nextId = 1;

    bool m_compressionEnabled = true;
    bool m_autoArchiveEnabled = true;
    int m_retentionDays = 365;

    QString generateArchivePath(const QDate& date, ArchiveType type);
    QString calculateChecksum(const QString& filePath);
    bool compressArchive(const QString& sourcePath, const QString& destPath);
    bool decompressArchive(const QString& sourcePath, const QString& destPath);

    ArchiveData collectDataForDate(const QDate& date);
    bool writeArchiveFile(const QString& path, const ArchiveData& data);
    ArchiveData readArchiveFile(const QString& path);
};

//=============================================================================
// ArchiveDailySummary - Summary statistics for a day (for archives)
//=============================================================================

struct ArchiveDailySummary {
    QDate date;

    // Check counts
    int totalChecks = 0;
    int openChecks = 0;
    int closedChecks = 0;
    int voidedChecks = 0;

    // Sales
    int grossSales = 0;
    int netSales = 0;
    int discounts = 0;
    int comps = 0;
    int voids = 0;

    // Taxes
    int totalTax = 0;
    QMap<int, int> taxByType;  // taxId -> amount

    // Payments
    int totalCash = 0;
    int totalCredit = 0;
    int totalDebit = 0;
    int totalChecks_payment = 0;
    int totalGiftCert = 0;
    int totalHouseAccount = 0;
    int totalOther = 0;

    // Labor
    int totalLaborHours = 0;  // in minutes
    int totalLaborCost = 0;

    // Tips
    int cashTips = 0;
    int creditTips = 0;
    int chargedTips = 0;

    // Customer count
    int guestCount = 0;
    int averageCheck = 0;  // cents

    // Serialization
    QJsonObject toJson() const;
    static ArchiveDailySummary fromJson(const QJsonObject& json);
};

} // namespace vt2
