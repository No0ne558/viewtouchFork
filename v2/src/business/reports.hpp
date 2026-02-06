// ViewTouch V2 - Reports System
// Modern C++/Qt6 reimplementation

#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QList>
#include <QMap>
#include <QDate>
#include <QDateTime>
#include <QJsonObject>
#include <QVariant>

namespace vt2 {

//=============================================================================
// Report Types
//=============================================================================
enum class ReportType {
    // Daily Operations
    DailySales,
    DailyLabor,
    DailyInventory,
    DailyCashDrawer,
    
    // Sales Analysis
    SalesByHour,
    SalesByItem,
    SalesByCategory,
    SalesByEmployee,
    SalesByPaymentType,
    
    // Employee Reports
    EmployeeShifts,
    EmployeeTips,
    EmployeeSales,
    EmployeePerformance,
    
    // Financial
    IncomeStatement,
    TaxReport,
    DiscountReport,
    VoidReport,
    RefundReport,
    
    // Inventory
    InventoryUsage,
    InventoryWaste,
    InventoryReorder,
    
    // Customer
    CustomerActivity,
    LoyaltyReport,
    
    // Audit
    AuditTrail,
    TimeClockAudit,
    
    Custom
};

//=============================================================================
// Report Column Definition
//=============================================================================
struct ReportColumn {
    QString id;
    QString header;
    QString dataType;  // "string", "int", "currency", "date", "percent"
    int width = 100;
    bool visible = true;
    QString alignment = "left";  // "left", "center", "right"
};

//=============================================================================
// Report Row
//=============================================================================
class ReportRow {
public:
    void setValue(const QString& columnId, const QVariant& value) {
        m_values[columnId] = value;
    }
    
    QVariant value(const QString& columnId) const {
        return m_values.value(columnId);
    }
    
    QString formattedValue(const QString& columnId, const QString& dataType) const;
    
    QMap<QString, QVariant> values() const { return m_values; }
    
    bool isSubtotal() const { return m_isSubtotal; }
    void setSubtotal(bool st) { m_isSubtotal = st; }
    
    bool isTotal() const { return m_isTotal; }
    void setTotal(bool t) { m_isTotal = t; }
    
    QString groupKey() const { return m_groupKey; }
    void setGroupKey(const QString& key) { m_groupKey = key; }
    
private:
    QMap<QString, QVariant> m_values;
    bool m_isSubtotal = false;
    bool m_isTotal = false;
    QString m_groupKey;
};

//=============================================================================
// Report Data
//=============================================================================
class ReportData : public QObject {
    Q_OBJECT
    
public:
    explicit ReportData(QObject* parent = nullptr);
    ~ReportData();
    
    // Report metadata
    QString title() const { return m_title; }
    void setTitle(const QString& t) { m_title = t; }
    
    QString subtitle() const { return m_subtitle; }
    void setSubtitle(const QString& s) { m_subtitle = s; }
    
    ReportType reportType() const { return m_type; }
    void setReportType(ReportType t) { m_type = t; }
    
    QDateTime generatedAt() const { return m_generatedAt; }
    void setGeneratedAt(const QDateTime& dt) { m_generatedAt = dt; }
    
    QDate startDate() const { return m_startDate; }
    void setStartDate(const QDate& d) { m_startDate = d; }
    
    QDate endDate() const { return m_endDate; }
    void setEndDate(const QDate& d) { m_endDate = d; }
    
    // Columns
    void addColumn(const ReportColumn& col) { m_columns.append(col); }
    QList<ReportColumn> columns() const { return m_columns; }
    ReportColumn* column(const QString& id);
    
    // Rows
    ReportRow* addRow();
    ReportRow* addSubtotal(const QString& label);
    ReportRow* addTotal(const QString& label);
    QList<ReportRow*> rows() const { return m_rows; }
    int rowCount() const { return m_rows.size(); }
    
    // Summary values
    void setSummaryValue(const QString& key, const QVariant& value) {
        m_summaryValues[key] = value;
    }
    QVariant summaryValue(const QString& key) const {
        return m_summaryValues.value(key);
    }
    QMap<QString, QVariant> summaryValues() const { return m_summaryValues; }
    
    // Notes/footer
    void addNote(const QString& note) { m_notes.append(note); }
    QStringList notes() const { return m_notes; }
    
    // Clear all data
    void clear();
    
    // Serialization
    QJsonObject toJson() const;
    static ReportData* fromJson(const QJsonObject& json, QObject* parent = nullptr);
    
private:
    QString m_title;
    QString m_subtitle;
    ReportType m_type = ReportType::DailySales;
    QDateTime m_generatedAt;
    QDate m_startDate;
    QDate m_endDate;
    
    QList<ReportColumn> m_columns;
    QList<ReportRow*> m_rows;
    QMap<QString, QVariant> m_summaryValues;
    QStringList m_notes;
};

//=============================================================================
// Report Generator - Creates specific reports
//=============================================================================
class ReportGenerator : public QObject {
    Q_OBJECT
    
public:
    explicit ReportGenerator(QObject* parent = nullptr);
    
    // Daily reports
    ReportData* generateDailySalesReport(const QDate& date);
    ReportData* generateDailyLaborReport(const QDate& date);
    
    // Period reports
    ReportData* generateSalesByHour(const QDate& start, const QDate& end);
    ReportData* generateSalesByItem(const QDate& start, const QDate& end);
    ReportData* generateSalesByCategory(const QDate& start, const QDate& end);
    ReportData* generateSalesByEmployee(const QDate& start, const QDate& end);
    
    // Employee reports
    ReportData* generateEmployeeShifts(int employeeId, const QDate& start, const QDate& end);
    ReportData* generateEmployeeTips(int employeeId, const QDate& start, const QDate& end);
    
    // Financial
    ReportData* generateTaxReport(const QDate& start, const QDate& end);
    ReportData* generateDiscountReport(const QDate& start, const QDate& end);
    ReportData* generateVoidReport(const QDate& start, const QDate& end);
    
    // Inventory
    ReportData* generateInventoryUsage(const QDate& start, const QDate& end);
    ReportData* generateInventoryReorder();
    
    // Audit
    ReportData* generateAuditTrail(const QDate& start, const QDate& end);
    ReportData* generateTimeClockAudit(const QDate& start, const QDate& end);
};

//=============================================================================
// Report Exporter - Export to various formats
//=============================================================================
class ReportExporter : public QObject {
    Q_OBJECT
    
public:
    explicit ReportExporter(QObject* parent = nullptr);
    
    // Export formats
    QString exportToCsv(const ReportData* report);
    QString exportToHtml(const ReportData* report);
    QString exportToText(const ReportData* report);
    QByteArray exportToPdf(const ReportData* report);
    
    // Save to file
    bool saveToFile(const ReportData* report, const QString& path, const QString& format);
    
    // Print
    bool printReport(const ReportData* report);
};

//=============================================================================
// Reports Manager - Singleton
//=============================================================================
class ReportsManager : public QObject {
    Q_OBJECT
    
public:
    static ReportsManager* instance();
    
    // Generator access
    ReportGenerator* generator() { return m_generator; }
    
    // Exporter access
    ReportExporter* exporter() { return m_exporter; }
    
    // Saved report templates
    void saveReportTemplate(const QString& name, ReportType type, 
                           const QDate& defaultStart, const QDate& defaultEnd);
    QList<QString> savedTemplates() const { return m_templates.keys(); }
    
    // Report history
    void addToHistory(ReportData* report);
    QList<ReportData*> recentReports() const { return m_recentReports; }
    void clearHistory();
    
    // Quick reports
    ReportData* quickDailySales() { return m_generator->generateDailySalesReport(QDate::currentDate()); }
    ReportData* quickDailyLabor() { return m_generator->generateDailyLaborReport(QDate::currentDate()); }
    
signals:
    void reportGenerated(ReportData* report);
    void reportExported(const QString& path);
    
private:
    explicit ReportsManager(QObject* parent = nullptr);
    static ReportsManager* s_instance;
    
    ReportGenerator* m_generator;
    ReportExporter* m_exporter;
    
    struct ReportTemplate {
        QString name;
        ReportType type;
        QDate defaultStart;
        QDate defaultEnd;
    };
    QMap<QString, ReportTemplate> m_templates;
    QList<ReportData*> m_recentReports;
    int m_maxHistory = 20;
};

} // namespace vt2
