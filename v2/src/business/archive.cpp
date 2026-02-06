// ViewTouch V2 - Archive System Implementation

#include "archive.hpp"
#include <QJsonArray>
#include <QJsonDocument>
#include <QFile>
#include <QDir>
#include <QCryptographicHash>

namespace vt2 {

//=============================================================================
// ArchiveRecord Implementation
//=============================================================================

ArchiveRecord::ArchiveRecord(QObject* parent)
    : QObject(parent)
    , m_createdAt(QDateTime::currentDateTime())
{
}

QJsonObject ArchiveRecord::toJson() const {
    QJsonObject json;
    json["id"] = m_id;
    json["type"] = static_cast<int>(m_type);
    json["dateFrom"] = m_dateFrom.toString(Qt::ISODate);
    json["dateTo"] = m_dateTo.toString(Qt::ISODate);
    json["createdAt"] = m_createdAt.toString(Qt::ISODate);
    json["createdBy"] = m_createdBy;
    json["filePath"] = m_filePath;
    json["fileSize"] = m_fileSize;
    json["checksum"] = m_checksum;
    json["checkCount"] = m_checkCount;
    json["totalSales"] = m_totalSales;
    json["totalTax"] = m_totalTax;
    json["compressed"] = m_compressed;
    json["verified"] = m_verified;
    json["notes"] = m_notes;
    return json;
}

ArchiveRecord* ArchiveRecord::fromJson(const QJsonObject& json, QObject* parent) {
    auto* record = new ArchiveRecord(parent);
    record->m_id = json["id"].toInt();
    record->m_type = static_cast<ArchiveType>(json["type"].toInt());
    record->m_dateFrom = QDate::fromString(json["dateFrom"].toString(), Qt::ISODate);
    record->m_dateTo = QDate::fromString(json["dateTo"].toString(), Qt::ISODate);
    record->m_createdAt = QDateTime::fromString(json["createdAt"].toString(), Qt::ISODate);
    record->m_createdBy = json["createdBy"].toInt();
    record->m_filePath = json["filePath"].toString();
    record->m_fileSize = json["fileSize"].toVariant().toLongLong();
    record->m_checksum = json["checksum"].toString();
    record->m_checkCount = json["checkCount"].toInt();
    record->m_totalSales = json["totalSales"].toInt();
    record->m_totalTax = json["totalTax"].toInt();
    record->m_compressed = json["compressed"].toBool();
    record->m_verified = json["verified"].toBool();
    record->m_notes = json["notes"].toString();
    return record;
}

//=============================================================================
// ArchiveManager Implementation
//=============================================================================

ArchiveManager* ArchiveManager::s_instance = nullptr;

ArchiveManager::ArchiveManager(QObject* parent)
    : QObject(parent)
{
    m_archiveDir = "./archives";
}

ArchiveManager* ArchiveManager::instance() {
    if (!s_instance) {
        s_instance = new ArchiveManager();
    }
    return s_instance;
}

void ArchiveManager::setArchiveDirectory(const QString& dir) {
    m_archiveDir = dir;
    QDir().mkpath(dir);
}

ArchiveRecord* ArchiveManager::createDailyArchive(const QDate& date, int employeeId) {
    emit archiveProgress(0, "Starting daily archive...");

    // Check if already archived
    if (isDateArchived(date)) {
        emit archiveError(QString("Date %1 is already archived").arg(date.toString()));
        return nullptr;
    }

    // Collect data
    emit archiveProgress(10, "Collecting data...");
    ArchiveData data = collectDataForDate(date);

    // Generate path
    QString archivePath = generateArchivePath(date, ArchiveType::Daily);
    emit archiveProgress(30, "Writing archive file...");

    // Write archive
    if (!writeArchiveFile(archivePath, data)) {
        emit archiveError("Failed to write archive file");
        return nullptr;
    }

    // Create record
    auto* record = new ArchiveRecord(this);
    record->setId(m_nextId++);
    record->setType(ArchiveType::Daily);
    record->setDateFrom(date);
    record->setDateTo(date);
    record->setCreatedByEmployee(employeeId);
    record->setFilePath(archivePath);

    // Get file info
    QFileInfo fi(archivePath);
    record->setFileSize(fi.size());

    emit archiveProgress(70, "Calculating checksum...");
    record->setChecksum(calculateChecksum(archivePath));

    // Summary data
    record->setCheckCount(data.closedChecks.size());

    int totalSales = 0;
    int totalTax = 0;
    if (!data.dailySummary.isEmpty()) {
        totalSales = data.dailySummary["grossSales"].toInt();
        totalTax = data.dailySummary["totalTax"].toInt();
    }
    record->setTotalSales(totalSales);
    record->setTotalTax(totalTax);

    // Compress if enabled
    if (m_compressionEnabled) {
        emit archiveProgress(80, "Compressing archive...");
        QString compressedPath = archivePath + ".gz";
        if (compressArchive(archivePath, compressedPath)) {
            QFile::remove(archivePath);
            record->setFilePath(compressedPath);
            record->setCompressed(true);
            QFileInfo cfi(compressedPath);
            record->setFileSize(cfi.size());
        }
    }

    // Verify
    emit archiveProgress(90, "Verifying archive...");
    record->setVerified(verifyArchive(record->id()));

    m_archives.append(record);
    saveIndex();

    emit archiveProgress(100, "Archive complete");
    emit archiveCreated(record);

    return record;
}

ArchiveRecord* ArchiveManager::createBackup(const QString& notes) {
    QDate today = QDate::currentDate();

    auto* record = new ArchiveRecord(this);
    record->setId(m_nextId++);
    record->setType(ArchiveType::Backup);
    record->setDateFrom(today);
    record->setDateTo(today);
    record->setNotes(notes);

    QString backupPath = m_archiveDir + QString("/backup_%1.json")
        .arg(QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss"));
    record->setFilePath(backupPath);

    // TODO: Actually backup current system data

    m_archives.append(record);
    saveIndex();

    emit archiveCreated(record);
    return record;
}

QList<ArchiveRecord*> ArchiveManager::archivesForDate(const QDate& date) {
    QList<ArchiveRecord*> result;
    for (auto* record : m_archives) {
        if (record->dateFrom() <= date && record->dateTo() >= date) {
            result.append(record);
        }
    }
    return result;
}

QList<ArchiveRecord*> ArchiveManager::archivesInRange(const QDate& from, const QDate& to) {
    QList<ArchiveRecord*> result;
    for (auto* record : m_archives) {
        if (record->dateFrom() <= to && record->dateTo() >= from) {
            result.append(record);
        }
    }
    return result;
}

ArchiveRecord* ArchiveManager::findArchive(int id) {
    for (auto* record : m_archives) {
        if (record->id() == id) return record;
    }
    return nullptr;
}

ArchiveData ArchiveManager::loadArchive(int archiveId) {
    auto* record = findArchive(archiveId);
    if (!record) {
        return ArchiveData();
    }

    QString path = record->filePath();

    // Decompress if needed
    if (record->isCompressed()) {
        QString tempPath = path;
        tempPath.replace(".gz", "_temp.json");
        if (!decompressArchive(path, tempPath)) {
            return ArchiveData();
        }
        ArchiveData data = readArchiveFile(tempPath);
        QFile::remove(tempPath);
        return data;
    }

    return readArchiveFile(path);
}

ArchiveData ArchiveManager::loadArchiveByDate(const QDate& date) {
    auto archives = archivesForDate(date);
    if (archives.isEmpty()) {
        return ArchiveData();
    }

    // Prefer daily archive
    for (auto* record : archives) {
        if (record->type() == ArchiveType::Daily) {
            return loadArchive(record->id());
        }
    }

    return loadArchive(archives.first()->id());
}

bool ArchiveManager::restoreArchive(int archiveId, bool overwrite) {
    Q_UNUSED(overwrite)

    auto* record = findArchive(archiveId);
    if (!record) {
        emit archiveError("Archive not found");
        return false;
    }

    ArchiveData data = loadArchive(archiveId);
    if (data.closedChecks.isEmpty() && data.payments.isEmpty()) {
        emit archiveError("Archive is empty or corrupted");
        return false;
    }

    // TODO: Actually restore data to system

    emit archiveRestored(archiveId);
    return true;
}

bool ArchiveManager::verifyArchive(int archiveId) {
    auto* record = findArchive(archiveId);
    if (!record) return false;

    QString currentChecksum = calculateChecksum(record->filePath());
    if (!record->checksum().isEmpty() && record->checksum() != currentChecksum) {
        return false;
    }

    // Try to load and parse
    ArchiveData data = loadArchive(archiveId);
    return !data.closedChecks.isEmpty() || !data.payments.isEmpty();
}

int ArchiveManager::deleteArchivesOlderThan(const QDate& cutoff) {
    int deleted = 0;

    for (int i = m_archives.size() - 1; i >= 0; --i) {
        auto* record = m_archives[i];
        if (record->dateTo() < cutoff) {
            QFile::remove(record->filePath());
            emit archiveDeleted(record->id());
            delete m_archives.takeAt(i);
            deleted++;
        }
    }

    if (deleted > 0) {
        saveIndex();
    }

    return deleted;
}

bool ArchiveManager::isDateArchived(const QDate& date) const {
    for (const auto* record : m_archives) {
        if (record->type() == ArchiveType::Daily &&
            record->dateFrom() <= date && record->dateTo() >= date) {
            return true;
        }
    }
    return false;
}

QString ArchiveManager::generateArchivePath(const QDate& date, ArchiveType type) {
    QString typeStr;
    switch (type) {
    case ArchiveType::Daily: typeStr = "daily"; break;
    case ArchiveType::Weekly: typeStr = "weekly"; break;
    case ArchiveType::Monthly: typeStr = "monthly"; break;
    case ArchiveType::Yearly: typeStr = "yearly"; break;
    case ArchiveType::Backup: typeStr = "backup"; break;
    case ArchiveType::Emergency: typeStr = "emergency"; break;
    }

    return m_archiveDir + QString("/%1_%2.json")
        .arg(typeStr)
        .arg(date.toString("yyyyMMdd"));
}

QString ArchiveManager::calculateChecksum(const QString& filePath) {
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        return QString();
    }

    QCryptographicHash hash(QCryptographicHash::Sha256);
    hash.addData(&file);
    return hash.result().toHex();
}

bool ArchiveManager::compressArchive(const QString& sourcePath, const QString& destPath) {
    // Simple copy for now - real implementation would use zlib
    return QFile::copy(sourcePath, destPath);
}

bool ArchiveManager::decompressArchive(const QString& sourcePath, const QString& destPath) {
    // Simple copy for now - real implementation would use zlib
    return QFile::copy(sourcePath, destPath);
}

ArchiveData ArchiveManager::collectDataForDate(const QDate& date) {
    ArchiveData data;
    data.archiveDate = date;

    // TODO: Collect actual data from CheckManager, PaymentManager, etc.
    // For now, return empty data structure

    return data;
}

bool ArchiveManager::writeArchiveFile(const QString& path, const ArchiveData& data) {
    QJsonObject root;
    root["archiveDate"] = data.archiveDate.toString(Qt::ISODate);
    root["version"] = "2.0";
    root["createdAt"] = QDateTime::currentDateTime().toString(Qt::ISODate);

    root["closedChecks"] = QJsonArray::fromVariantList(
        QVariantList() // data.closedChecks as variant
    );
    root["dailySummary"] = data.dailySummary;

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        return false;
    }

    QJsonDocument doc(root);
    file.write(doc.toJson());
    return true;
}

ArchiveData ArchiveManager::readArchiveFile(const QString& path) {
    ArchiveData data;

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return data;
    }

    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    QJsonObject root = doc.object();

    data.archiveDate = QDate::fromString(root["archiveDate"].toString(), Qt::ISODate);
    data.dailySummary = root["dailySummary"].toObject();

    QJsonArray checksArray = root["closedChecks"].toArray();
    for (const auto& ref : checksArray) {
        data.closedChecks.append(ref.toObject());
    }

    return data;
}

bool ArchiveManager::saveIndex() {
    QJsonObject root;
    root["nextId"] = m_nextId;

    QJsonArray archivesArray;
    for (const auto* record : m_archives) {
        archivesArray.append(record->toJson());
    }
    root["archives"] = archivesArray;

    QString indexPath = m_archiveDir + "/archive_index.json";
    QFile file(indexPath);
    if (!file.open(QIODevice::WriteOnly)) {
        return false;
    }

    QJsonDocument doc(root);
    file.write(doc.toJson());
    return true;
}

bool ArchiveManager::loadIndex() {
    QString indexPath = m_archiveDir + "/archive_index.json";
    QFile file(indexPath);
    if (!file.open(QIODevice::ReadOnly)) {
        return false;
    }

    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    QJsonObject root = doc.object();

    m_nextId = root["nextId"].toInt(1);

    qDeleteAll(m_archives);
    m_archives.clear();

    QJsonArray archivesArray = root["archives"].toArray();
    for (const auto& ref : archivesArray) {
        auto* record = ArchiveRecord::fromJson(ref.toObject(), this);
        m_archives.append(record);
    }

    return true;
}

//=============================================================================
// DailySummary Implementation
//=============================================================================

QJsonObject ArchiveDailySummary::toJson() const {
    QJsonObject json;
    json["date"] = date.toString(Qt::ISODate);
    json["totalChecks"] = totalChecks;
    json["openChecks"] = openChecks;
    json["closedChecks"] = closedChecks;
    json["voidedChecks"] = voidedChecks;
    json["grossSales"] = grossSales;
    json["netSales"] = netSales;
    json["discounts"] = discounts;
    json["comps"] = comps;
    json["voids"] = voids;
    json["totalTax"] = totalTax;
    json["totalCash"] = totalCash;
    json["totalCredit"] = totalCredit;
    json["totalDebit"] = totalDebit;
    json["totalChecks_payment"] = totalChecks_payment;
    json["totalGiftCert"] = totalGiftCert;
    json["totalHouseAccount"] = totalHouseAccount;
    json["totalOther"] = totalOther;
    json["totalLaborHours"] = totalLaborHours;
    json["totalLaborCost"] = totalLaborCost;
    json["cashTips"] = cashTips;
    json["creditTips"] = creditTips;
    json["chargedTips"] = chargedTips;
    json["guestCount"] = guestCount;
    json["averageCheck"] = averageCheck;

    QJsonObject taxByTypeObj;
    for (auto it = taxByType.begin(); it != taxByType.end(); ++it) {
        taxByTypeObj[QString::number(it.key())] = it.value();
    }
    json["taxByType"] = taxByTypeObj;

    return json;
}

ArchiveDailySummary ArchiveDailySummary::fromJson(const QJsonObject& json) {
    ArchiveDailySummary summary;
    summary.date = QDate::fromString(json["date"].toString(), Qt::ISODate);
    summary.totalChecks = json["totalChecks"].toInt();
    summary.openChecks = json["openChecks"].toInt();
    summary.closedChecks = json["closedChecks"].toInt();
    summary.voidedChecks = json["voidedChecks"].toInt();
    summary.grossSales = json["grossSales"].toInt();
    summary.netSales = json["netSales"].toInt();
    summary.discounts = json["discounts"].toInt();
    summary.comps = json["comps"].toInt();
    summary.voids = json["voids"].toInt();
    summary.totalTax = json["totalTax"].toInt();
    summary.totalCash = json["totalCash"].toInt();
    summary.totalCredit = json["totalCredit"].toInt();
    summary.totalDebit = json["totalDebit"].toInt();
    summary.totalChecks_payment = json["totalChecks_payment"].toInt();
    summary.totalGiftCert = json["totalGiftCert"].toInt();
    summary.totalHouseAccount = json["totalHouseAccount"].toInt();
    summary.totalOther = json["totalOther"].toInt();
    summary.totalLaborHours = json["totalLaborHours"].toInt();
    summary.totalLaborCost = json["totalLaborCost"].toInt();
    summary.cashTips = json["cashTips"].toInt();
    summary.creditTips = json["creditTips"].toInt();
    summary.chargedTips = json["chargedTips"].toInt();
    summary.guestCount = json["guestCount"].toInt();
    summary.averageCheck = json["averageCheck"].toInt();

    QJsonObject taxByTypeObj = json["taxByType"].toObject();
    for (auto it = taxByTypeObj.begin(); it != taxByTypeObj.end(); ++it) {
        summary.taxByType[it.key().toInt()] = it.value().toInt();
    }

    return summary;
}

} // namespace vt2
