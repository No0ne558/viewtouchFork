// ViewTouch V2 - Reports System Implementation
// Modern C++/Qt6 reimplementation

#include "reports.hpp"
#include <QJsonArray>
#include <QJsonDocument>
#include <QFile>
#include <QTextStream>
#include <QLocale>

namespace vt2 {

//=============================================================================
// ReportRow Implementation
//=============================================================================

QString ReportRow::formattedValue(const QString& columnId, const QString& dataType) const {
    QVariant val = value(columnId);
    
    if (dataType == "currency") {
        return QLocale().toCurrencyString(val.toDouble() / 100.0);
    } else if (dataType == "percent") {
        return QString::number(val.toDouble(), 'f', 1) + "%";
    } else if (dataType == "date") {
        return val.toDate().toString("MM/dd/yyyy");
    } else if (dataType == "int") {
        return QString::number(val.toInt());
    }
    return val.toString();
}

//=============================================================================
// ReportData Implementation
//=============================================================================

ReportData::ReportData(QObject* parent)
    : QObject(parent)
    , m_generatedAt(QDateTime::currentDateTime())
{
}

ReportData::~ReportData() {
    qDeleteAll(m_rows);
}

ReportColumn* ReportData::column(const QString& id) {
    for (auto& col : m_columns) {
        if (col.id == id) return &col;
    }
    return nullptr;
}

ReportRow* ReportData::addRow() {
    auto* row = new ReportRow();
    m_rows.append(row);
    return row;
}

ReportRow* ReportData::addSubtotal(const QString& label) {
    auto* row = new ReportRow();
    row->setSubtotal(true);
    row->setValue("label", label);
    m_rows.append(row);
    return row;
}

ReportRow* ReportData::addTotal(const QString& label) {
    auto* row = new ReportRow();
    row->setTotal(true);
    row->setValue("label", label);
    m_rows.append(row);
    return row;
}

void ReportData::clear() {
    qDeleteAll(m_rows);
    m_rows.clear();
    m_columns.clear();
    m_summaryValues.clear();
    m_notes.clear();
}

QJsonObject ReportData::toJson() const {
    QJsonObject json;
    json["title"] = m_title;
    json["subtitle"] = m_subtitle;
    json["type"] = static_cast<int>(m_type);
    json["generatedAt"] = m_generatedAt.toString(Qt::ISODate);
    json["startDate"] = m_startDate.toString(Qt::ISODate);
    json["endDate"] = m_endDate.toString(Qt::ISODate);
    
    // Columns
    QJsonArray colArray;
    for (const auto& col : m_columns) {
        QJsonObject colObj;
        colObj["id"] = col.id;
        colObj["header"] = col.header;
        colObj["dataType"] = col.dataType;
        colObj["width"] = col.width;
        colObj["visible"] = col.visible;
        colObj["alignment"] = col.alignment;
        colArray.append(colObj);
    }
    json["columns"] = colArray;
    
    // Rows
    QJsonArray rowArray;
    for (const auto* row : m_rows) {
        QJsonObject rowObj;
        rowObj["isSubtotal"] = row->isSubtotal();
        rowObj["isTotal"] = row->isTotal();
        rowObj["groupKey"] = row->groupKey();
        
        QJsonObject valuesObj;
        for (auto it = row->values().constBegin(); it != row->values().constEnd(); ++it) {
            valuesObj[it.key()] = QJsonValue::fromVariant(it.value());
        }
        rowObj["values"] = valuesObj;
        rowArray.append(rowObj);
    }
    json["rows"] = rowArray;
    
    // Summary
    QJsonObject summaryObj;
    for (auto it = m_summaryValues.constBegin(); it != m_summaryValues.constEnd(); ++it) {
        summaryObj[it.key()] = QJsonValue::fromVariant(it.value());
    }
    json["summary"] = summaryObj;
    
    // Notes
    QJsonArray notesArray;
    for (const auto& note : m_notes) {
        notesArray.append(note);
    }
    json["notes"] = notesArray;
    
    return json;
}

ReportData* ReportData::fromJson(const QJsonObject& json, QObject* parent) {
    auto* report = new ReportData(parent);
    report->m_title = json["title"].toString();
    report->m_subtitle = json["subtitle"].toString();
    report->m_type = static_cast<ReportType>(json["type"].toInt());
    report->m_generatedAt = QDateTime::fromString(json["generatedAt"].toString(), Qt::ISODate);
    report->m_startDate = QDate::fromString(json["startDate"].toString(), Qt::ISODate);
    report->m_endDate = QDate::fromString(json["endDate"].toString(), Qt::ISODate);
    
    // Columns
    QJsonArray colArray = json["columns"].toArray();
    for (const auto& ref : colArray) {
        QJsonObject colObj = ref.toObject();
        ReportColumn col;
        col.id = colObj["id"].toString();
        col.header = colObj["header"].toString();
        col.dataType = colObj["dataType"].toString();
        col.width = colObj["width"].toInt(100);
        col.visible = colObj["visible"].toBool(true);
        col.alignment = colObj["alignment"].toString("left");
        report->m_columns.append(col);
    }
    
    // Rows
    QJsonArray rowArray = json["rows"].toArray();
    for (const auto& ref : rowArray) {
        QJsonObject rowObj = ref.toObject();
        auto* row = new ReportRow();
        row->setSubtotal(rowObj["isSubtotal"].toBool());
        row->setTotal(rowObj["isTotal"].toBool());
        row->setGroupKey(rowObj["groupKey"].toString());
        
        QJsonObject valuesObj = rowObj["values"].toObject();
        for (auto it = valuesObj.constBegin(); it != valuesObj.constEnd(); ++it) {
            row->setValue(it.key(), it.value().toVariant());
        }
        report->m_rows.append(row);
    }
    
    // Summary
    QJsonObject summaryObj = json["summary"].toObject();
    for (auto it = summaryObj.constBegin(); it != summaryObj.constEnd(); ++it) {
        report->m_summaryValues[it.key()] = it.value().toVariant();
    }
    
    // Notes
    QJsonArray notesArray = json["notes"].toArray();
    for (const auto& ref : notesArray) {
        report->m_notes.append(ref.toString());
    }
    
    return report;
}

//=============================================================================
// ReportGenerator Implementation
//=============================================================================

ReportGenerator::ReportGenerator(QObject* parent)
    : QObject(parent)
{
}

ReportData* ReportGenerator::generateDailySalesReport(const QDate& date) {
    auto* report = new ReportData(this);
    report->setTitle("Daily Sales Report");
    report->setSubtitle(date.toString("dddd, MMMM d, yyyy"));
    report->setReportType(ReportType::DailySales);
    report->setStartDate(date);
    report->setEndDate(date);
    
    // Define columns
    report->addColumn({"category", "Category", "string", 150, true, "left"});
    report->addColumn({"quantity", "Qty", "int", 60, true, "right"});
    report->addColumn({"gross", "Gross Sales", "currency", 100, true, "right"});
    report->addColumn({"discounts", "Discounts", "currency", 100, true, "right"});
    report->addColumn({"net", "Net Sales", "currency", 100, true, "right"});
    report->addColumn({"tax", "Tax", "currency", 80, true, "right"});
    
    // In a real implementation, this would pull from SalesManager
    // For now, create placeholder structure
    
    // Total row
    auto* total = report->addTotal("TOTAL");
    total->setValue("quantity", 0);
    total->setValue("gross", 0);
    total->setValue("discounts", 0);
    total->setValue("net", 0);
    total->setValue("tax", 0);
    
    // Summary values
    report->setSummaryValue("totalChecks", 0);
    report->setSummaryValue("avgCheck", 0);
    report->setSummaryValue("guestCount", 0);
    
    return report;
}

ReportData* ReportGenerator::generateDailyLaborReport(const QDate& date) {
    auto* report = new ReportData(this);
    report->setTitle("Daily Labor Report");
    report->setSubtitle(date.toString("dddd, MMMM d, yyyy"));
    report->setReportType(ReportType::DailyLabor);
    report->setStartDate(date);
    report->setEndDate(date);
    
    report->addColumn({"employee", "Employee", "string", 150, true, "left"});
    report->addColumn({"job", "Job", "string", 100, true, "left"});
    report->addColumn({"clockIn", "Clock In", "string", 80, true, "center"});
    report->addColumn({"clockOut", "Clock Out", "string", 80, true, "center"});
    report->addColumn({"hours", "Hours", "string", 60, true, "right"});
    report->addColumn({"rate", "Rate", "currency", 80, true, "right"});
    report->addColumn({"wages", "Wages", "currency", 100, true, "right"});
    
    return report;
}

ReportData* ReportGenerator::generateSalesByHour(const QDate& start, const QDate& end) {
    auto* report = new ReportData(this);
    report->setTitle("Sales by Hour");
    report->setReportType(ReportType::SalesByHour);
    report->setStartDate(start);
    report->setEndDate(end);
    
    report->addColumn({"hour", "Hour", "string", 100, true, "left"});
    report->addColumn({"checks", "Checks", "int", 80, true, "right"});
    report->addColumn({"guests", "Guests", "int", 80, true, "right"});
    report->addColumn({"sales", "Sales", "currency", 100, true, "right"});
    report->addColumn({"avgCheck", "Avg Check", "currency", 100, true, "right"});
    report->addColumn({"percent", "% of Day", "percent", 80, true, "right"});
    
    return report;
}

ReportData* ReportGenerator::generateSalesByItem(const QDate& start, const QDate& end) {
    auto* report = new ReportData(this);
    report->setTitle("Sales by Item");
    report->setReportType(ReportType::SalesByItem);
    report->setStartDate(start);
    report->setEndDate(end);
    
    report->addColumn({"item", "Item Name", "string", 200, true, "left"});
    report->addColumn({"category", "Category", "string", 120, true, "left"});
    report->addColumn({"quantity", "Qty Sold", "int", 80, true, "right"});
    report->addColumn({"gross", "Gross Sales", "currency", 100, true, "right"});
    report->addColumn({"percent", "% of Sales", "percent", 80, true, "right"});
    
    return report;
}

ReportData* ReportGenerator::generateSalesByCategory(const QDate& start, const QDate& end) {
    auto* report = new ReportData(this);
    report->setTitle("Sales by Category");
    report->setReportType(ReportType::SalesByCategory);
    report->setStartDate(start);
    report->setEndDate(end);
    
    report->addColumn({"category", "Category", "string", 150, true, "left"});
    report->addColumn({"items", "Items Sold", "int", 80, true, "right"});
    report->addColumn({"gross", "Gross Sales", "currency", 120, true, "right"});
    report->addColumn({"percent", "% of Total", "percent", 80, true, "right"});
    
    return report;
}

ReportData* ReportGenerator::generateSalesByEmployee(const QDate& start, const QDate& end) {
    auto* report = new ReportData(this);
    report->setTitle("Sales by Employee");
    report->setReportType(ReportType::SalesByEmployee);
    report->setStartDate(start);
    report->setEndDate(end);
    
    report->addColumn({"employee", "Employee", "string", 150, true, "left"});
    report->addColumn({"checks", "Checks", "int", 80, true, "right"});
    report->addColumn({"guests", "Guests", "int", 80, true, "right"});
    report->addColumn({"sales", "Sales", "currency", 100, true, "right"});
    report->addColumn({"avgCheck", "Avg Check", "currency", 100, true, "right"});
    report->addColumn({"tips", "Tips", "currency", 100, true, "right"});
    
    return report;
}

ReportData* ReportGenerator::generateEmployeeShifts(int employeeId, const QDate& start, const QDate& end) {
    auto* report = new ReportData(this);
    report->setTitle("Employee Shift Report");
    report->setReportType(ReportType::EmployeeShifts);
    report->setStartDate(start);
    report->setEndDate(end);
    
    report->addColumn({"date", "Date", "date", 100, true, "left"});
    report->addColumn({"job", "Job", "string", 100, true, "left"});
    report->addColumn({"clockIn", "In", "string", 80, true, "center"});
    report->addColumn({"clockOut", "Out", "string", 80, true, "center"});
    report->addColumn({"break", "Break", "string", 60, true, "center"});
    report->addColumn({"total", "Total", "string", 60, true, "right"});
    report->addColumn({"overtime", "OT", "string", 60, true, "right"});
    
    Q_UNUSED(employeeId);
    return report;
}

ReportData* ReportGenerator::generateEmployeeTips(int employeeId, const QDate& start, const QDate& end) {
    auto* report = new ReportData(this);
    report->setTitle("Employee Tips Report");
    report->setReportType(ReportType::EmployeeTips);
    report->setStartDate(start);
    report->setEndDate(end);
    
    report->addColumn({"date", "Date", "date", 100, true, "left"});
    report->addColumn({"sales", "Sales", "currency", 100, true, "right"});
    report->addColumn({"cash", "Cash Tips", "currency", 100, true, "right"});
    report->addColumn({"credit", "CC Tips", "currency", 100, true, "right"});
    report->addColumn({"pooled", "Pooled", "currency", 100, true, "right"});
    report->addColumn({"total", "Total Tips", "currency", 100, true, "right"});
    report->addColumn({"percent", "Tip %", "percent", 80, true, "right"});
    
    Q_UNUSED(employeeId);
    return report;
}

ReportData* ReportGenerator::generateTaxReport(const QDate& start, const QDate& end) {
    auto* report = new ReportData(this);
    report->setTitle("Tax Report");
    report->setReportType(ReportType::TaxReport);
    report->setStartDate(start);
    report->setEndDate(end);
    
    report->addColumn({"taxType", "Tax Type", "string", 150, true, "left"});
    report->addColumn({"rate", "Rate", "percent", 80, true, "right"});
    report->addColumn({"taxableSales", "Taxable Sales", "currency", 120, true, "right"});
    report->addColumn({"taxCollected", "Tax Collected", "currency", 120, true, "right"});
    
    return report;
}

ReportData* ReportGenerator::generateDiscountReport(const QDate& start, const QDate& end) {
    auto* report = new ReportData(this);
    report->setTitle("Discount Report");
    report->setReportType(ReportType::DiscountReport);
    report->setStartDate(start);
    report->setEndDate(end);
    
    report->addColumn({"discount", "Discount Type", "string", 150, true, "left"});
    report->addColumn({"count", "Times Applied", "int", 100, true, "right"});
    report->addColumn({"amount", "Total Amount", "currency", 120, true, "right"});
    report->addColumn({"approvedBy", "Approved By", "string", 120, true, "left"});
    
    return report;
}

ReportData* ReportGenerator::generateVoidReport(const QDate& start, const QDate& end) {
    auto* report = new ReportData(this);
    report->setTitle("Void Report");
    report->setReportType(ReportType::VoidReport);
    report->setStartDate(start);
    report->setEndDate(end);
    
    report->addColumn({"date", "Date/Time", "string", 120, true, "left"});
    report->addColumn({"check", "Check #", "int", 80, true, "right"});
    report->addColumn({"item", "Item", "string", 150, true, "left"});
    report->addColumn({"amount", "Amount", "currency", 100, true, "right"});
    report->addColumn({"employee", "Employee", "string", 120, true, "left"});
    report->addColumn({"reason", "Reason", "string", 150, true, "left"});
    
    return report;
}

ReportData* ReportGenerator::generateInventoryUsage(const QDate& start, const QDate& end) {
    auto* report = new ReportData(this);
    report->setTitle("Inventory Usage Report");
    report->setReportType(ReportType::InventoryUsage);
    report->setStartDate(start);
    report->setEndDate(end);
    
    report->addColumn({"item", "Item", "string", 200, true, "left"});
    report->addColumn({"startQty", "Start Qty", "string", 80, true, "right"});
    report->addColumn({"received", "Received", "string", 80, true, "right"});
    report->addColumn({"used", "Used", "string", 80, true, "right"});
    report->addColumn({"waste", "Waste", "string", 80, true, "right"});
    report->addColumn({"endQty", "End Qty", "string", 80, true, "right"});
    
    return report;
}

ReportData* ReportGenerator::generateInventoryReorder() {
    auto* report = new ReportData(this);
    report->setTitle("Reorder Report");
    report->setReportType(ReportType::InventoryReorder);
    report->setStartDate(QDate::currentDate());
    report->setEndDate(QDate::currentDate());
    
    report->addColumn({"item", "Item", "string", 200, true, "left"});
    report->addColumn({"current", "Current Qty", "string", 100, true, "right"});
    report->addColumn({"reorderPoint", "Reorder Point", "string", 100, true, "right"});
    report->addColumn({"reorderQty", "Order Qty", "string", 100, true, "right"});
    report->addColumn({"vendor", "Vendor", "string", 150, true, "left"});
    
    return report;
}

ReportData* ReportGenerator::generateAuditTrail(const QDate& start, const QDate& end) {
    auto* report = new ReportData(this);
    report->setTitle("Audit Trail");
    report->setReportType(ReportType::AuditTrail);
    report->setStartDate(start);
    report->setEndDate(end);
    
    report->addColumn({"timestamp", "Date/Time", "string", 140, true, "left"});
    report->addColumn({"action", "Action", "string", 120, true, "left"});
    report->addColumn({"user", "User", "string", 100, true, "left"});
    report->addColumn({"target", "Target", "string", 150, true, "left"});
    report->addColumn({"details", "Details", "string", 200, true, "left"});
    
    return report;
}

ReportData* ReportGenerator::generateTimeClockAudit(const QDate& start, const QDate& end) {
    auto* report = new ReportData(this);
    report->setTitle("Time Clock Audit");
    report->setReportType(ReportType::TimeClockAudit);
    report->setStartDate(start);
    report->setEndDate(end);
    
    report->addColumn({"timestamp", "Date/Time", "string", 140, true, "left"});
    report->addColumn({"employee", "Employee", "string", 120, true, "left"});
    report->addColumn({"action", "Action", "string", 100, true, "left"});
    report->addColumn({"original", "Original Time", "string", 100, true, "center"});
    report->addColumn({"modified", "Modified Time", "string", 100, true, "center"});
    report->addColumn({"modifiedBy", "Modified By", "string", 120, true, "left"});
    
    return report;
}

//=============================================================================
// ReportExporter Implementation
//=============================================================================

ReportExporter::ReportExporter(QObject* parent)
    : QObject(parent)
{
}

QString ReportExporter::exportToCsv(const ReportData* report) {
    QString csv;
    QTextStream out(&csv);
    
    // Header
    bool first = true;
    for (const auto& col : report->columns()) {
        if (!col.visible) continue;
        if (!first) out << ",";
        out << "\"" << col.header << "\"";
        first = false;
    }
    out << "\n";
    
    // Rows
    for (const auto* row : report->rows()) {
        first = true;
        for (const auto& col : report->columns()) {
            if (!col.visible) continue;
            if (!first) out << ",";
            QString val = row->formattedValue(col.id, col.dataType);
            out << "\"" << val.replace("\"", "\"\"") << "\"";
            first = false;
        }
        out << "\n";
    }
    
    return csv;
}

QString ReportExporter::exportToHtml(const ReportData* report) {
    QString html;
    QTextStream out(&html);
    
    out << "<!DOCTYPE html>\n<html>\n<head>\n";
    out << "<title>" << report->title() << "</title>\n";
    out << "<style>\n";
    out << "body { font-family: Arial, sans-serif; margin: 20px; }\n";
    out << "h1 { margin-bottom: 5px; }\n";
    out << "h2 { color: #666; margin-top: 0; font-weight: normal; }\n";
    out << "table { border-collapse: collapse; width: 100%; margin-top: 20px; }\n";
    out << "th, td { border: 1px solid #ddd; padding: 8px; }\n";
    out << "th { background-color: #f4f4f4; }\n";
    out << ".subtotal { background-color: #f9f9f9; font-weight: bold; }\n";
    out << ".total { background-color: #e9e9e9; font-weight: bold; }\n";
    out << ".right { text-align: right; }\n";
    out << ".center { text-align: center; }\n";
    out << "</style>\n</head>\n<body>\n";
    
    out << "<h1>" << report->title() << "</h1>\n";
    if (!report->subtitle().isEmpty()) {
        out << "<h2>" << report->subtitle() << "</h2>\n";
    }
    
    out << "<table>\n<thead>\n<tr>\n";
    for (const auto& col : report->columns()) {
        if (!col.visible) continue;
        out << "<th>" << col.header << "</th>\n";
    }
    out << "</tr>\n</thead>\n<tbody>\n";
    
    for (const auto* row : report->rows()) {
        QString rowClass;
        if (row->isTotal()) rowClass = "total";
        else if (row->isSubtotal()) rowClass = "subtotal";
        
        out << "<tr class=\"" << rowClass << "\">\n";
        for (const auto& col : report->columns()) {
            if (!col.visible) continue;
            QString tdClass;
            if (col.alignment == "right") tdClass = "right";
            else if (col.alignment == "center") tdClass = "center";
            
            out << "<td class=\"" << tdClass << "\">";
            out << row->formattedValue(col.id, col.dataType);
            out << "</td>\n";
        }
        out << "</tr>\n";
    }
    
    out << "</tbody>\n</table>\n";
    
    // Notes
    if (!report->notes().isEmpty()) {
        out << "<div class=\"notes\">\n";
        for (const auto& note : report->notes()) {
            out << "<p>" << note << "</p>\n";
        }
        out << "</div>\n";
    }
    
    out << "<p style=\"color: #999; font-size: 12px;\">Generated: ";
    out << report->generatedAt().toString("MM/dd/yyyy hh:mm AP") << "</p>\n";
    
    out << "</body>\n</html>\n";
    
    return html;
}

QString ReportExporter::exportToText(const ReportData* report) {
    QString text;
    QTextStream out(&text);
    
    out << report->title() << "\n";
    out << QString("=").repeated(report->title().length()) << "\n";
    if (!report->subtitle().isEmpty()) {
        out << report->subtitle() << "\n";
    }
    out << "\n";
    
    // Calculate column widths
    QList<int> widths;
    for (const auto& col : report->columns()) {
        if (!col.visible) continue;
        widths.append(col.width / 8);  // Approximate character width
    }
    
    // Header
    int colIdx = 0;
    for (const auto& col : report->columns()) {
        if (!col.visible) continue;
        out << col.header.leftJustified(widths[colIdx]) << " ";
        colIdx++;
    }
    out << "\n";
    
    // Separator
    colIdx = 0;
    for (const auto& col : report->columns()) {
        if (!col.visible) continue;
        out << QString("-").repeated(widths[colIdx]) << " ";
        colIdx++;
    }
    out << "\n";
    
    // Data
    for (const auto* row : report->rows()) {
        if (row->isSubtotal() || row->isTotal()) {
            out << "\n";
        }
        
        colIdx = 0;
        for (const auto& col : report->columns()) {
            if (!col.visible) continue;
            QString val = row->formattedValue(col.id, col.dataType);
            if (col.alignment == "right") {
                out << val.rightJustified(widths[colIdx]) << " ";
            } else {
                out << val.leftJustified(widths[colIdx]) << " ";
            }
            colIdx++;
        }
        out << "\n";
    }
    
    out << "\nGenerated: " << report->generatedAt().toString("MM/dd/yyyy hh:mm AP") << "\n";
    
    return text;
}

QByteArray ReportExporter::exportToPdf(const ReportData* report) {
    // PDF generation would require a library like QPdfWriter or an external tool
    // For now, return HTML converted to bytes (could use wkhtmltopdf externally)
    Q_UNUSED(report);
    return QByteArray();
}

bool ReportExporter::saveToFile(const ReportData* report, const QString& path, const QString& format) {
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        return false;
    }
    
    QString content;
    if (format == "csv") {
        content = exportToCsv(report);
    } else if (format == "html") {
        content = exportToHtml(report);
    } else {
        content = exportToText(report);
    }
    
    QTextStream out(&file);
    out << content;
    
    return true;
}

bool ReportExporter::printReport(const ReportData* report) {
    // Would use QPrinter and render HTML
    Q_UNUSED(report);
    return false;
}

//=============================================================================
// ReportsManager Implementation
//=============================================================================

ReportsManager* ReportsManager::s_instance = nullptr;

ReportsManager::ReportsManager(QObject* parent)
    : QObject(parent)
    , m_generator(new ReportGenerator(this))
    , m_exporter(new ReportExporter(this))
{
}

ReportsManager* ReportsManager::instance() {
    if (!s_instance) {
        s_instance = new ReportsManager();
    }
    return s_instance;
}

void ReportsManager::saveReportTemplate(const QString& name, ReportType type,
                                        const QDate& defaultStart, const QDate& defaultEnd) {
    ReportTemplate tmpl;
    tmpl.name = name;
    tmpl.type = type;
    tmpl.defaultStart = defaultStart;
    tmpl.defaultEnd = defaultEnd;
    m_templates[name] = tmpl;
}

void ReportsManager::addToHistory(ReportData* report) {
    m_recentReports.prepend(report);
    report->setParent(this);
    
    while (m_recentReports.size() > m_maxHistory) {
        delete m_recentReports.takeLast();
    }
    
    emit reportGenerated(report);
}

void ReportsManager::clearHistory() {
    qDeleteAll(m_recentReports);
    m_recentReports.clear();
}

} // namespace vt2
