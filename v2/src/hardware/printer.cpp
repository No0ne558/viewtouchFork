// ViewTouch V2 - Printer System Implementation
// Modern C++/Qt6 reimplementation

#include "printer.hpp"
#include <QJsonArray>
#include <QJsonDocument>
#include <QFile>
#include <QTcpSocket>

namespace vt2 {

//=============================================================================
// PrintJob Implementation
//=============================================================================

PrintJob::PrintJob(QObject* parent)
    : QObject(parent)
    , m_queuedAt(QDateTime::currentDateTime())
{
}

QJsonObject PrintJob::toJson() const {
    QJsonObject json;
    json["id"] = m_id;
    json["printerId"] = m_printerId;
    json["content"] = m_content;
    json["copies"] = m_copies;
    json["status"] = static_cast<int>(m_status);
    json["queuedAt"] = m_queuedAt.toString(Qt::ISODate);
    if (m_printedAt.isValid()) {
        json["printedAt"] = m_printedAt.toString(Qt::ISODate);
    }
    json["errorMessage"] = m_errorMessage;
    json["priority"] = m_priority;
    json["checkId"] = m_checkId;
    return json;
}

PrintJob* PrintJob::fromJson(const QJsonObject& json, QObject* parent) {
    auto* job = new PrintJob(parent);
    job->m_id = json["id"].toInt();
    job->m_printerId = json["printerId"].toInt();
    job->m_content = json["content"].toString();
    job->m_copies = json["copies"].toInt(1);
    job->m_status = static_cast<JobStatus>(json["status"].toInt());
    job->m_queuedAt = QDateTime::fromString(json["queuedAt"].toString(), Qt::ISODate);
    if (json.contains("printedAt")) {
        job->m_printedAt = QDateTime::fromString(json["printedAt"].toString(), Qt::ISODate);
    }
    job->m_errorMessage = json["errorMessage"].toString();
    job->m_priority = json["priority"].toInt();
    job->m_checkId = json["checkId"].toInt();
    return job;
}

//=============================================================================
// PrinterConfig Implementation
//=============================================================================

PrinterConfig::PrinterConfig(QObject* parent)
    : QObject(parent)
{
}

QJsonObject PrinterConfig::toJson() const {
    QJsonObject json;
    json["id"] = m_id;
    json["name"] = m_name;
    json["printerType"] = static_cast<int>(m_printerType);
    json["enabled"] = m_enabled;
    json["connectionType"] = static_cast<int>(m_connectionType);
    json["devicePath"] = m_devicePath;
    json["ipAddress"] = m_ipAddress;
    json["port"] = m_port;
    json["paperWidth"] = m_paperWidth;
    json["fontName"] = m_fontName;
    json["fontSize"] = m_fontSize;
    json["cutPaper"] = m_cutPaper;
    json["openDrawer"] = m_openDrawer;
    json["drawerKickPulse"] = m_drawerKickPulse;
    
    QJsonArray catArray;
    for (int cat : m_menuCategories) {
        catArray.append(cat);
    }
    json["menuCategories"] = catArray;
    
    QJsonArray headerArray;
    for (const auto& line : m_headerLines) {
        headerArray.append(line);
    }
    json["headerLines"] = headerArray;
    
    QJsonArray footerArray;
    for (const auto& line : m_footerLines) {
        footerArray.append(line);
    }
    json["footerLines"] = footerArray;
    
    json["logoPath"] = m_logoPath;
    json["printLogo"] = m_printLogo;
    
    return json;
}

PrinterConfig* PrinterConfig::fromJson(const QJsonObject& json, QObject* parent) {
    auto* printer = new PrinterConfig(parent);
    printer->m_id = json["id"].toInt();
    printer->m_name = json["name"].toString();
    printer->m_printerType = static_cast<PrinterType>(json["printerType"].toInt());
    printer->m_enabled = json["enabled"].toBool(true);
    printer->m_connectionType = static_cast<PrinterConnection>(json["connectionType"].toInt());
    printer->m_devicePath = json["devicePath"].toString();
    printer->m_ipAddress = json["ipAddress"].toString();
    printer->m_port = json["port"].toInt(9100);
    printer->m_paperWidth = json["paperWidth"].toInt(42);
    printer->m_fontName = json["fontName"].toString("Courier");
    printer->m_fontSize = json["fontSize"].toInt(10);
    printer->m_cutPaper = json["cutPaper"].toBool(true);
    printer->m_openDrawer = json["openDrawer"].toBool();
    printer->m_drawerKickPulse = json["drawerKickPulse"].toInt(100);
    
    QJsonArray catArray = json["menuCategories"].toArray();
    for (const auto& ref : catArray) {
        printer->m_menuCategories.append(ref.toInt());
    }
    
    QJsonArray headerArray = json["headerLines"].toArray();
    for (const auto& ref : headerArray) {
        printer->m_headerLines.append(ref.toString());
    }
    
    QJsonArray footerArray = json["footerLines"].toArray();
    for (const auto& ref : footerArray) {
        printer->m_footerLines.append(ref.toString());
    }
    
    printer->m_logoPath = json["logoPath"].toString();
    printer->m_printLogo = json["printLogo"].toBool();
    
    return printer;
}

//=============================================================================
// ReceiptBuilder Implementation
//=============================================================================

ReceiptBuilder::ReceiptBuilder(int lineWidth)
    : m_lineWidth(lineWidth)
{
}

void ReceiptBuilder::addLine(const QString& text) {
    m_lines.append(formatLine(text));
}

void ReceiptBuilder::addCenteredLine(const QString& text) {
    m_lines.append(pad(text, m_lineWidth, ' ', Qt::AlignCenter));
}

void ReceiptBuilder::addRightAligned(const QString& text) {
    m_lines.append(pad(text, m_lineWidth, ' ', Qt::AlignRight));
}

void ReceiptBuilder::addTwoColumn(const QString& left, const QString& right) {
    int rightWidth = right.length();
    int leftWidth = m_lineWidth - rightWidth - 1;
    QString line = pad(left, leftWidth, ' ', Qt::AlignLeft) + " " + right;
    m_lines.append(line);
}

void ReceiptBuilder::addThreeColumn(const QString& left, const QString& center, const QString& right) {
    int leftWidth = m_lineWidth / 3;
    int rightWidth = m_lineWidth / 3;
    int centerWidth = m_lineWidth - leftWidth - rightWidth;
    
    QString line = pad(left, leftWidth, ' ', Qt::AlignLeft) +
                   pad(center, centerWidth, ' ', Qt::AlignCenter) +
                   pad(right, rightWidth, ' ', Qt::AlignRight);
    m_lines.append(line);
}

void ReceiptBuilder::addDivider(char ch) {
    m_lines.append(QString(m_lineWidth, ch));
}

void ReceiptBuilder::addBlankLine() {
    m_lines.append("");
}

void ReceiptBuilder::addDoubleLine() {
    m_lines.append(QString(m_lineWidth, '='));
}

void ReceiptBuilder::addBold(const QString& text) {
    // ESC E 1 for bold, ESC E 0 to reset
    m_lines.append("\x1B" "E" "\x01" + text + "\x1B" "E" "\x00");
}

void ReceiptBuilder::addLarge(const QString& text) {
    // ESC ! for text size mode
    m_lines.append("\x1B" "!" "\x30" + text + "\x1B" "!" "\x00");
}

void ReceiptBuilder::addSmall(const QString& text) {
    // Use smaller font
    m_lines.append("\x1B" "!" "\x01" + text + "\x1B" "!" "\x00");
}

void ReceiptBuilder::addInverse(const QString& text) {
    // GS B 1 for inverse, GS B 0 to reset
    m_lines.append("\x1D" "B" "\x01" + text + "\x1D" "B" "\x00");
}

void ReceiptBuilder::addMoneyLine(const QString& label, int cents) {
    QString amount = QString("$%1.%2")
        .arg(cents / 100)
        .arg(cents % 100, 2, 10, QChar('0'));
    addTwoColumn(label, amount);
}

void ReceiptBuilder::addItemLine(const QString& item, int qty, int priceEach) {
    QString qtyStr = QString::number(qty);
    int total = qty * priceEach;
    QString totalStr = QString("$%1.%2")
        .arg(total / 100)
        .arg(total % 100, 2, 10, QChar('0'));
    
    // Format: qty item name ........... $total
    int nameWidth = m_lineWidth - qtyStr.length() - totalStr.length() - 2;
    QString itemPadded = item.left(nameWidth).leftJustified(nameWidth, '.');
    
    m_lines.append(qtyStr + " " + itemPadded + " " + totalStr);
}

void ReceiptBuilder::addBarcode(const QString& data) {
    // ESC/POS barcode command for Code 128
    QByteArray cmd;
    cmd.append("\x1D" "k" "\x49");  // GS k CODE128
    cmd.append(static_cast<char>(data.length()));
    cmd.append(data.toLatin1());
    m_lines.append(QString::fromLatin1(cmd));
}

void ReceiptBuilder::addQRCode(const QString& data) {
    // ESC/POS QR code commands
    // This is a simplified implementation
    QByteArray cmd;
    // Set QR code size
    cmd.append("\x1D" "(k" "\x03\x00" "\x31" "\x43" "\x06");
    // Set error correction
    cmd.append("\x1D" "(k" "\x03\x00" "\x31" "\x45" "\x31");
    // Store data
    int len = data.length() + 3;
    cmd.append("\x1D" "(k");
    cmd.append(static_cast<char>(len % 256));
    cmd.append(static_cast<char>(len / 256));
    cmd.append("\x31" "\x50" "\x30");
    cmd.append(data.toLatin1());
    // Print QR code
    cmd.append("\x1D" "(k" "\x03\x00" "\x31" "\x51" "\x30");
    
    m_lines.append(QString::fromLatin1(cmd));
}

void ReceiptBuilder::cutPaper() {
    m_cutAtEnd = true;
}

void ReceiptBuilder::openDrawer() {
    m_openDrawerAtEnd = true;
}

void ReceiptBuilder::feedLines(int count) {
    m_feedAtEnd = count;
}

QString ReceiptBuilder::build() const {
    QString result = m_lines.join("\n");
    
    if (m_feedAtEnd > 0) {
        result += QString("\n").repeated(m_feedAtEnd);
    }
    
    return result;
}

QByteArray ReceiptBuilder::buildRaw() const {
    QByteArray result;
    
    // Initialize printer
    result.append("\x1B" "@");  // ESC @ - Initialize
    
    for (const auto& line : m_lines) {
        result.append(line.toLatin1());
        result.append("\n");
    }
    
    // Feed lines
    if (m_feedAtEnd > 0) {
        result.append("\x1B" "d");  // ESC d - Feed n lines
        result.append(static_cast<char>(m_feedAtEnd));
    }
    
    // Open drawer (pulse to pin 2)
    if (m_openDrawerAtEnd) {
        result.append("\x1B" "p" "\x00" "\x19" "\xFA");
    }
    
    // Cut paper
    if (m_cutAtEnd) {
        result.append("\x1D" "V" "\x00");  // GS V - Cut
    }
    
    return result;
}

void ReceiptBuilder::clear() {
    m_lines.clear();
    m_cutAtEnd = false;
    m_openDrawerAtEnd = false;
    m_feedAtEnd = 0;
}

QString ReceiptBuilder::formatLine(const QString& text) const {
    if (text.length() <= m_lineWidth) {
        return text;
    }
    return text.left(m_lineWidth);
}

QString ReceiptBuilder::pad(const QString& text, int width, char ch, Qt::Alignment align) const {
    if (text.length() >= width) {
        return text.left(width);
    }
    
    int padding = width - text.length();
    
    if (align == Qt::AlignCenter) {
        int leftPad = padding / 2;
        int rightPad = padding - leftPad;
        return QString(leftPad, ch) + text + QString(rightPad, ch);
    } else if (align == Qt::AlignRight) {
        return QString(padding, ch) + text;
    } else {
        return text + QString(padding, ch);
    }
}

//=============================================================================
// PrintManager Implementation
//=============================================================================

PrintManager* PrintManager::s_instance = nullptr;

PrintManager::PrintManager(QObject* parent)
    : QObject(parent)
{
}

PrintManager* PrintManager::instance() {
    if (!s_instance) {
        s_instance = new PrintManager();
    }
    return s_instance;
}

void PrintManager::addPrinter(PrinterConfig* printer) {
    if (printer->id() == 0) {
        printer->setId(m_nextPrinterId++);
    }
    printer->setParent(this);
    m_printers.append(printer);
    m_printerStatus[printer->id()] = PrinterStatus::Unknown;
    
    emit printerAdded(printer);
}

void PrintManager::removePrinter(int printerId) {
    for (int i = 0; i < m_printers.size(); ++i) {
        if (m_printers[i]->id() == printerId) {
            delete m_printers.takeAt(i);
            m_printerStatus.remove(printerId);
            emit printerRemoved(printerId);
            return;
        }
    }
}

PrinterConfig* PrintManager::findPrinter(int id) {
    for (auto* printer : m_printers) {
        if (printer->id() == id) return printer;
    }
    return nullptr;
}

PrinterConfig* PrintManager::findPrinterByName(const QString& name) {
    for (auto* printer : m_printers) {
        if (printer->name() == name) return printer;
    }
    return nullptr;
}

QList<PrinterConfig*> PrintManager::printersByType(PrinterType type) const {
    QList<PrinterConfig*> result;
    for (auto* printer : m_printers) {
        if (printer->printerType() == type && printer->isEnabled()) {
            result.append(printer);
        }
    }
    return result;
}

PrinterStatus PrintManager::checkStatus(int printerId) {
    auto* printer = findPrinter(printerId);
    if (!printer) return PrinterStatus::Unknown;
    
    PrinterStatus status = PrinterStatus::Unknown;
    
    if (printer->connectionType() == PrinterConnection::Network) {
        // Try to connect to network printer
        QTcpSocket socket;
        socket.connectToHost(printer->ipAddress(), printer->port());
        if (socket.waitForConnected(1000)) {
            status = PrinterStatus::Ready;
            socket.disconnectFromHost();
        } else {
            status = PrinterStatus::Offline;
        }
    } else if (printer->connectionType() == PrinterConnection::USB ||
               printer->connectionType() == PrinterConnection::Serial) {
        // Check if device exists
        QFile device(printer->devicePath());
        if (device.exists()) {
            status = PrinterStatus::Ready;
        } else {
            status = PrinterStatus::Offline;
        }
    }
    
    if (m_printerStatus[printerId] != status) {
        m_printerStatus[printerId] = status;
        emit printerStatusChanged(printerId, status);
    }
    
    return status;
}

void PrintManager::testPrinter(int printerId) {
    auto* printer = findPrinter(printerId);
    if (!printer) return;
    
    ReceiptBuilder builder(printer->paperWidth());
    builder.addCenteredLine("*** PRINTER TEST ***");
    builder.addBlankLine();
    builder.addLine("Printer: " + printer->name());
    builder.addLine("Time: " + QDateTime::currentDateTime().toString("MM/dd/yyyy hh:mm:ss"));
    builder.addBlankLine();
    builder.addDivider();
    builder.addLine("0123456789012345678901234567890123456789");
    builder.addDivider();
    builder.addTwoColumn("Left text", "Right text");
    builder.addBlankLine();
    builder.addCenteredLine("*** END TEST ***");
    builder.feedLines(3);
    builder.cutPaper();
    
    print(printerId, builder.build());
}

PrintJob* PrintManager::print(int printerId, const QString& content, int copies) {
    auto* job = new PrintJob(this);
    job->setId(m_nextJobId++);
    job->setPrinterId(printerId);
    job->setContent(content);
    job->setCopies(copies);
    job->setStatus(PrintJob::JobStatus::Queued);
    
    m_jobQueue.append(job);
    emit jobQueued(job);
    
    processQueue();
    
    return job;
}

PrintJob* PrintManager::printReceipt(int checkId) {
    // Would build receipt from check data
    // For now, placeholder
    if (m_defaultReceiptPrinter == 0) return nullptr;
    
    auto* job = print(m_defaultReceiptPrinter, "");
    if (job) {
        job->setCheckId(checkId);
    }
    return job;
}

PrintJob* PrintManager::printKitchenOrder(int checkId) {
    if (m_defaultKitchenPrinter == 0) return nullptr;
    
    auto* job = print(m_defaultKitchenPrinter, "");
    if (job) {
        job->setCheckId(checkId);
    }
    return job;
}

PrintJob* PrintManager::printLabel(const QString& content) {
    auto printers = printersByType(PrinterType::Label);
    if (printers.isEmpty()) return nullptr;
    
    return print(printers.first()->id(), content);
}

PrintJob* PrintManager::printReport(const QString& reportHtml) {
    if (m_defaultReportPrinter == 0) return nullptr;
    return print(m_defaultReportPrinter, reportHtml);
}

QList<PrintJob*> PrintManager::pendingJobs() const {
    QList<PrintJob*> result;
    for (auto* job : m_jobQueue) {
        if (job->status() == PrintJob::JobStatus::Queued ||
            job->status() == PrintJob::JobStatus::Printing) {
            result.append(job);
        }
    }
    return result;
}

QList<PrintJob*> PrintManager::jobsForPrinter(int printerId) const {
    QList<PrintJob*> result;
    for (auto* job : m_jobQueue) {
        if (job->printerId() == printerId) {
            result.append(job);
        }
    }
    return result;
}

bool PrintManager::cancelJob(int jobId) {
    for (auto* job : m_jobQueue) {
        if (job->id() == jobId && job->status() == PrintJob::JobStatus::Queued) {
            job->setStatus(PrintJob::JobStatus::Cancelled);
            return true;
        }
    }
    return false;
}

bool PrintManager::retryJob(int jobId) {
    for (auto* job : m_jobQueue) {
        if (job->id() == jobId && job->status() == PrintJob::JobStatus::Failed) {
            job->setStatus(PrintJob::JobStatus::Queued);
            job->setErrorMessage("");
            processQueue();
            return true;
        }
    }
    return false;
}

void PrintManager::clearCompletedJobs() {
    for (int i = m_jobQueue.size() - 1; i >= 0; --i) {
        auto* job = m_jobQueue[i];
        if (job->status() == PrintJob::JobStatus::Completed ||
            job->status() == PrintJob::JobStatus::Cancelled) {
            delete m_jobQueue.takeAt(i);
        }
    }
}

void PrintManager::processQueue() {
    for (auto* job : m_jobQueue) {
        if (job->status() != PrintJob::JobStatus::Queued) continue;
        
        auto* printer = findPrinter(job->printerId());
        if (!printer || !printer->isEnabled()) {
            job->setStatus(PrintJob::JobStatus::Failed);
            job->setErrorMessage("Printer not found or disabled");
            emit jobFailed(job, job->errorMessage());
            continue;
        }
        
        job->setStatus(PrintJob::JobStatus::Printing);
        emit jobStarted(job);
        
        bool success = false;
        for (int i = 0; i < job->copies(); ++i) {
            success = sendToPrinter(printer, job->content());
            if (!success) break;
        }
        
        if (success) {
            job->setStatus(PrintJob::JobStatus::Completed);
            job->setPrintedAt(QDateTime::currentDateTime());
            emit jobCompleted(job);
        } else {
            job->setStatus(PrintJob::JobStatus::Failed);
            job->setErrorMessage("Failed to send data to printer");
            emit jobFailed(job, job->errorMessage());
        }
    }
}

bool PrintManager::sendToPrinter(PrinterConfig* printer, const QString& content) {
    if (printer->connectionType() == PrinterConnection::Network) {
        QTcpSocket socket;
        socket.connectToHost(printer->ipAddress(), printer->port());
        if (!socket.waitForConnected(5000)) {
            return false;
        }
        
        ReceiptBuilder builder(printer->paperWidth());
        // Content is already formatted, just add control codes
        
        QByteArray data = content.toLatin1();
        if (printer->cutPaper()) {
            data.append("\x1D" "V" "\x00");
        }
        
        socket.write(data);
        socket.waitForBytesWritten(5000);
        socket.disconnectFromHost();
        return true;
        
    } else if (printer->connectionType() == PrinterConnection::USB ||
               printer->connectionType() == PrinterConnection::Serial) {
        QFile device(printer->devicePath());
        if (!device.open(QIODevice::WriteOnly)) {
            return false;
        }
        
        QByteArray data = content.toLatin1();
        if (printer->cutPaper()) {
            data.append("\x1D" "V" "\x00");
        }
        
        qint64 written = device.write(data);
        device.close();
        return written == data.size();
    }
    
    return false;
}

bool PrintManager::saveToFile(const QString& path) {
    QJsonObject root;
    root["nextPrinterId"] = m_nextPrinterId;
    root["nextJobId"] = m_nextJobId;
    root["defaultReceiptPrinter"] = m_defaultReceiptPrinter;
    root["defaultKitchenPrinter"] = m_defaultKitchenPrinter;
    root["defaultReportPrinter"] = m_defaultReportPrinter;
    
    QJsonArray printerArray;
    for (const auto* printer : m_printers) {
        printerArray.append(printer->toJson());
    }
    root["printers"] = printerArray;
    
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        return false;
    }
    
    QJsonDocument doc(root);
    file.write(doc.toJson());
    return true;
}

bool PrintManager::loadFromFile(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return false;
    }
    
    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    QJsonObject root = doc.object();
    
    m_nextPrinterId = root["nextPrinterId"].toInt(1);
    m_nextJobId = root["nextJobId"].toInt(1);
    m_defaultReceiptPrinter = root["defaultReceiptPrinter"].toInt();
    m_defaultKitchenPrinter = root["defaultKitchenPrinter"].toInt();
    m_defaultReportPrinter = root["defaultReportPrinter"].toInt();
    
    qDeleteAll(m_printers);
    m_printers.clear();
    
    QJsonArray printerArray = root["printers"].toArray();
    for (const auto& ref : printerArray) {
        auto* printer = PrinterConfig::fromJson(ref.toObject(), this);
        m_printers.append(printer);
        m_printerStatus[printer->id()] = PrinterStatus::Unknown;
    }
    
    return true;
}

} // namespace vt2
