// ViewTouch V2 - Printer System
// Modern C++/Qt6 reimplementation

#pragma once

#include <QObject>
#include <QString>
#include <QList>
#include <QMap>
#include <QDateTime>
#include <QJsonObject>
#include <QStringList>

namespace vt2 {

//=============================================================================
// Printer Types
//=============================================================================
enum class PrinterType {
    Receipt,        // Standard receipt printer
    Kitchen,        // Kitchen/prep printer
    Label,          // Label printer
    Report,         // Report/office printer
    CustomerDisplay // Customer-facing display
};

enum class PrinterConnection {
    USB,
    Serial,
    Network,
    Bluetooth,
    Parallel
};

enum class PrinterStatus {
    Ready,
    Printing,
    PaperLow,
    PaperOut,
    Offline,
    Error,
    Unknown
};

//=============================================================================
// Print Job
//=============================================================================
class PrintJob : public QObject {
    Q_OBJECT
    
public:
    enum class JobStatus {
        Queued,
        Printing,
        Completed,
        Failed,
        Cancelled
    };
    Q_ENUM(JobStatus)
    
    explicit PrintJob(QObject* parent = nullptr);
    
    int id() const { return m_id; }
    void setId(int id) { m_id = id; }
    
    int printerId() const { return m_printerId; }
    void setPrinterId(int id) { m_printerId = id; }
    
    QString content() const { return m_content; }
    void setContent(const QString& c) { m_content = c; }
    
    int copies() const { return m_copies; }
    void setCopies(int c) { m_copies = c; }
    
    JobStatus status() const { return m_status; }
    void setStatus(JobStatus s) { m_status = s; }
    
    QDateTime queuedAt() const { return m_queuedAt; }
    void setQueuedAt(const QDateTime& dt) { m_queuedAt = dt; }
    
    QDateTime printedAt() const { return m_printedAt; }
    void setPrintedAt(const QDateTime& dt) { m_printedAt = dt; }
    
    QString errorMessage() const { return m_errorMessage; }
    void setErrorMessage(const QString& msg) { m_errorMessage = msg; }
    
    int priority() const { return m_priority; }
    void setPriority(int p) { m_priority = p; }
    
    // Associated data
    int checkId() const { return m_checkId; }
    void setCheckId(int id) { m_checkId = id; }
    
    QJsonObject toJson() const;
    static PrintJob* fromJson(const QJsonObject& json, QObject* parent = nullptr);
    
private:
    int m_id = 0;
    int m_printerId = 0;
    QString m_content;
    int m_copies = 1;
    JobStatus m_status = JobStatus::Queued;
    QDateTime m_queuedAt;
    QDateTime m_printedAt;
    QString m_errorMessage;
    int m_priority = 0;  // Higher = more important
    int m_checkId = 0;
};

//=============================================================================
// Printer Configuration
//=============================================================================
class PrinterConfig : public QObject {
    Q_OBJECT
    
public:
    explicit PrinterConfig(QObject* parent = nullptr);
    
    // Basic info
    int id() const { return m_id; }
    void setId(int id) { m_id = id; }
    
    QString name() const { return m_name; }
    void setName(const QString& n) { m_name = n; }
    
    PrinterType printerType() const { return m_printerType; }
    void setPrinterType(PrinterType t) { m_printerType = t; }
    
    bool isEnabled() const { return m_enabled; }
    void setEnabled(bool e) { m_enabled = e; }
    
    // Connection
    PrinterConnection connectionType() const { return m_connectionType; }
    void setConnectionType(PrinterConnection c) { m_connectionType = c; }
    
    QString devicePath() const { return m_devicePath; }
    void setDevicePath(const QString& p) { m_devicePath = p; }
    
    QString ipAddress() const { return m_ipAddress; }
    void setIpAddress(const QString& ip) { m_ipAddress = ip; }
    
    int port() const { return m_port; }
    void setPort(int p) { m_port = p; }
    
    // Formatting
    int paperWidth() const { return m_paperWidth; }  // Characters per line
    void setPaperWidth(int w) { m_paperWidth = w; }
    
    QString fontName() const { return m_fontName; }
    void setFontName(const QString& f) { m_fontName = f; }
    
    int fontSize() const { return m_fontSize; }
    void setFontSize(int s) { m_fontSize = s; }
    
    bool cutPaper() const { return m_cutPaper; }
    void setCutPaper(bool c) { m_cutPaper = c; }
    
    bool openDrawer() const { return m_openDrawer; }
    void setOpenDrawer(bool o) { m_openDrawer = o; }
    
    int drawerKickPulse() const { return m_drawerKickPulse; }
    void setDrawerKickPulse(int p) { m_drawerKickPulse = p; }
    
    // Kitchen printer specific
    QList<int> menuCategories() const { return m_menuCategories; }
    void setMenuCategories(const QList<int>& cats) { m_menuCategories = cats; }
    void addMenuCategory(int cat) { if (!m_menuCategories.contains(cat)) m_menuCategories.append(cat); }
    void removeMenuCategory(int cat) { m_menuCategories.removeAll(cat); }
    
    // Header/footer
    QStringList headerLines() const { return m_headerLines; }
    void setHeaderLines(const QStringList& lines) { m_headerLines = lines; }
    
    QStringList footerLines() const { return m_footerLines; }
    void setFooterLines(const QStringList& lines) { m_footerLines = lines; }
    
    // Logo
    QString logoPath() const { return m_logoPath; }
    void setLogoPath(const QString& p) { m_logoPath = p; }
    bool printLogo() const { return m_printLogo; }
    void setPrintLogo(bool p) { m_printLogo = p; }
    
    QJsonObject toJson() const;
    static PrinterConfig* fromJson(const QJsonObject& json, QObject* parent = nullptr);
    
private:
    int m_id = 0;
    QString m_name;
    PrinterType m_printerType = PrinterType::Receipt;
    bool m_enabled = true;
    
    PrinterConnection m_connectionType = PrinterConnection::USB;
    QString m_devicePath;
    QString m_ipAddress;
    int m_port = 9100;
    
    int m_paperWidth = 42;
    QString m_fontName = "Courier";
    int m_fontSize = 10;
    bool m_cutPaper = true;
    bool m_openDrawer = false;
    int m_drawerKickPulse = 100;
    
    QList<int> m_menuCategories;
    QStringList m_headerLines;
    QStringList m_footerLines;
    QString m_logoPath;
    bool m_printLogo = false;
};

//=============================================================================
// Receipt Builder - Formats receipt content
//=============================================================================
class ReceiptBuilder {
public:
    ReceiptBuilder(int lineWidth = 42);
    
    // Text formatting
    void addLine(const QString& text);
    void addCenteredLine(const QString& text);
    void addRightAligned(const QString& text);
    void addTwoColumn(const QString& left, const QString& right);
    void addThreeColumn(const QString& left, const QString& center, const QString& right);
    void addDivider(char ch = '-');
    void addBlankLine();
    void addDoubleLine();
    
    // Special formatting
    void addBold(const QString& text);
    void addLarge(const QString& text);
    void addSmall(const QString& text);
    void addInverse(const QString& text);
    
    // Monetary
    void addMoneyLine(const QString& label, int cents);
    void addItemLine(const QString& item, int qty, int priceEach);
    
    // Barcode
    void addBarcode(const QString& data);
    void addQRCode(const QString& data);
    
    // Control
    void cutPaper();
    void openDrawer();
    void feedLines(int count);
    
    // Get result
    QString build() const;
    QByteArray buildRaw() const;  // With ESC/POS codes
    
    void clear();
    
private:
    QString formatLine(const QString& text) const;
    QString pad(const QString& text, int width, char ch = ' ', Qt::Alignment align = Qt::AlignLeft) const;
    
    int m_lineWidth;
    QStringList m_lines;
    bool m_cutAtEnd = false;
    bool m_openDrawerAtEnd = false;
    int m_feedAtEnd = 0;
};

//=============================================================================
// Print Manager - Singleton
//=============================================================================
class PrintManager : public QObject {
    Q_OBJECT
    
public:
    static PrintManager* instance();
    
    // Printer management
    void addPrinter(PrinterConfig* printer);
    void removePrinter(int printerId);
    PrinterConfig* findPrinter(int id);
    PrinterConfig* findPrinterByName(const QString& name);
    QList<PrinterConfig*> allPrinters() const { return m_printers; }
    QList<PrinterConfig*> printersByType(PrinterType type) const;
    
    // Status
    PrinterStatus checkStatus(int printerId);
    void testPrinter(int printerId);
    
    // Printing
    PrintJob* print(int printerId, const QString& content, int copies = 1);
    PrintJob* printReceipt(int checkId);
    PrintJob* printKitchenOrder(int checkId);
    PrintJob* printLabel(const QString& content);
    PrintJob* printReport(const QString& reportHtml);
    
    // Job management
    QList<PrintJob*> pendingJobs() const;
    QList<PrintJob*> jobsForPrinter(int printerId) const;
    bool cancelJob(int jobId);
    bool retryJob(int jobId);
    void clearCompletedJobs();
    
    // Default printers
    void setDefaultReceiptPrinter(int printerId) { m_defaultReceiptPrinter = printerId; }
    int defaultReceiptPrinter() const { return m_defaultReceiptPrinter; }
    
    void setDefaultKitchenPrinter(int printerId) { m_defaultKitchenPrinter = printerId; }
    int defaultKitchenPrinter() const { return m_defaultKitchenPrinter; }
    
    void setDefaultReportPrinter(int printerId) { m_defaultReportPrinter = printerId; }
    int defaultReportPrinter() const { return m_defaultReportPrinter; }
    
    // Persistence
    bool saveToFile(const QString& path);
    bool loadFromFile(const QString& path);
    
signals:
    void printerAdded(PrinterConfig* printer);
    void printerRemoved(int printerId);
    void printerStatusChanged(int printerId, PrinterStatus status);
    void jobQueued(PrintJob* job);
    void jobStarted(PrintJob* job);
    void jobCompleted(PrintJob* job);
    void jobFailed(PrintJob* job, const QString& error);
    
private:
    explicit PrintManager(QObject* parent = nullptr);
    static PrintManager* s_instance;
    
    void processQueue();
    bool sendToPrinter(PrinterConfig* printer, const QString& content);
    
    QList<PrinterConfig*> m_printers;
    QList<PrintJob*> m_jobQueue;
    QMap<int, PrinterStatus> m_printerStatus;
    
    int m_nextPrinterId = 1;
    int m_nextJobId = 1;
    
    int m_defaultReceiptPrinter = 0;
    int m_defaultKitchenPrinter = 0;
    int m_defaultReportPrinter = 0;
};

} // namespace vt2
