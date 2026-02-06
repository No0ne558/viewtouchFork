// ViewTouch V2 - Employee System
// Modern C++/Qt6 reimplementation

#ifndef VT2_EMPLOYEE_HPP
#define VT2_EMPLOYEE_HPP

#include <QObject>
#include <QString>
#include <QDateTime>
#include <QList>
#include <QJsonObject>

namespace vt2 {

// Job Types
enum class JobType {
    None = 0,
    Dishwasher = 1,
    Busperson = 2,
    Cook = 3,
    Cook2 = 4,
    Cashier = 5,
    Server = 6,
    Server2 = 7,    // Server & Cashier
    Host = 8,
    Bookkeeper = 9,
    Manager = 10,   // Shift Supervisor
    Manager2 = 11,  // Assistant Manager
    Manager3 = 12,  // Manager
    Bartender = 13,
    Cook3 = 14,
    Developer = 50,
    Superuser = 51
};

// Pay Rate Types
enum class PayRate {
    Undefined = 0,
    Hour = 1,
    Day = 2,
    Week = 3,
    TwoWeeks = 4,
    FourWeeks = 5,
    HalfMonth = 6,
    Month = 7
};

// Security Flags
enum class SecurityFlag {
    None = 0,
    Tables = (1 << 0),       // Go to table page
    Order = (1 << 1),        // Place an order
    Settle = (1 << 2),       // Settle a check
    Transfer = (1 << 3),     // Move check to different table
    Rebuild = (1 << 4),      // Alter a check after finalized
    Comp = (1 << 5),         // Comp/void items on check
    Supervisor = (1 << 6),   // Supervisor page
    Manager = (1 << 7),      // Manager page
    Employees = (1 << 8),    // View/alter employee records
    Developer = (1 << 9),    // Alter application
    Expenses = (1 << 10)     // Payout from revenue
};

Q_DECLARE_FLAGS(SecurityFlags, SecurityFlag)

//=============================================================================
// JobInfo - Job information for an employee
//=============================================================================
class JobInfo : public QObject {
    Q_OBJECT
    Q_PROPERTY(JobType job READ job WRITE setJob NOTIFY jobChanged)
    Q_PROPERTY(int payAmount READ payAmount WRITE setPayAmount NOTIFY payAmountChanged)

public:
    explicit JobInfo(QObject* parent = nullptr);
    JobInfo(JobType job, int payAmount, PayRate rate = PayRate::Hour, QObject* parent = nullptr);
    ~JobInfo() override = default;

    JobType job() const { return m_job; }
    void setJob(JobType job);
    
    PayRate payRate() const { return m_payRate; }
    void setPayRate(PayRate rate) { m_payRate = rate; }
    
    int payAmount() const { return m_payAmount; }
    void setPayAmount(int amount);
    
    int startingPage() const { return m_startingPage; }
    void setStartingPage(int page) { m_startingPage = page; }
    
    int deptCode() const { return m_deptCode; }
    void setDeptCode(int code) { m_deptCode = code; }
    
    QString title() const;
    
    // Serialization
    QJsonObject toJson() const;
    static JobInfo* fromJson(const QJsonObject& json, QObject* parent = nullptr);

signals:
    void jobChanged();
    void payAmountChanged();

private:
    JobType m_job = JobType::None;
    PayRate m_payRate = PayRate::Hour;
    int m_payAmount = 0;       // Pay in cents
    int m_startingPage = 0;
    int m_deptCode = 0;
};

//=============================================================================
// Employee - Employee record
//=============================================================================
class Employee : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString systemName READ systemName WRITE setSystemName NOTIFY nameChanged)
    Q_PROPERTY(QString firstName READ firstName WRITE setFirstName NOTIFY nameChanged)
    Q_PROPERTY(QString lastName READ lastName WRITE setLastName NOTIFY nameChanged)
    Q_PROPERTY(bool active READ isActive WRITE setActive NOTIFY activeChanged)
    Q_PROPERTY(bool loggedIn READ isLoggedIn NOTIFY loginStateChanged)

public:
    explicit Employee(QObject* parent = nullptr);
    ~Employee() override = default;

    // Identification
    int id() const { return m_id; }
    void setId(int id) { m_id = id; }
    
    int key() const { return m_key; }
    void setKey(int key) { m_key = key; }
    
    int employeeNumber() const { return m_employeeNo; }
    void setEmployeeNumber(int num) { m_employeeNo = num; }
    
    int accessCode() const { return m_accessCode; }
    void setAccessCode(int code) { m_accessCode = code; }
    
    // Names
    QString systemName() const { return m_systemName; }
    void setSystemName(const QString& name);
    
    QString firstName() const { return m_firstName; }
    void setFirstName(const QString& name);
    
    QString lastName() const { return m_lastName; }
    void setLastName(const QString& name);
    
    QString fullName() const { return m_firstName + " " + m_lastName; }
    
    // Contact
    QString address() const { return m_address; }
    void setAddress(const QString& addr) { m_address = addr; }
    
    QString city() const { return m_city; }
    void setCity(const QString& city) { m_city = city; }
    
    QString state() const { return m_state; }
    void setState(const QString& state) { m_state = state; }
    
    QString phone() const { return m_phone; }
    void setPhone(const QString& phone) { m_phone = phone; }
    
    QString ssn() const { return m_ssn; }
    void setSSN(const QString& ssn) { m_ssn = ssn; }
    
    // Employment
    bool isActive() const { return m_active; }
    void setActive(bool active);
    
    bool isTraining() const { return m_training; }
    void setTraining(bool training) { m_training = training; }
    
    int drawer() const { return m_drawer; }
    void setDrawer(int drawer) { m_drawer = drawer; }
    
    QString password() const { return m_password; }
    void setPassword(const QString& pwd) { m_password = pwd; }
    
    // Security
    int securityFlags() const { return m_securityFlags; }
    void setSecurityFlags(int flags) { m_securityFlags = flags; }
    
    bool hasPermission(SecurityFlag flag) const {
        return m_securityFlags & static_cast<int>(flag);
    }
    
    void grantPermission(SecurityFlag flag) {
        m_securityFlags |= static_cast<int>(flag);
    }
    
    void revokePermission(SecurityFlag flag) {
        m_securityFlags &= ~static_cast<int>(flag);
    }
    
    // Jobs
    QList<JobInfo*> jobs() const { return m_jobs; }
    void addJob(JobInfo* job);
    void removeJob(JobInfo* job);
    JobInfo* findJobByType(JobType type);
    JobInfo* currentJobInfo() const;
    QString jobTitle() const;
    
    // Login state
    bool isLoggedIn() const { return m_currentJob != JobType::None; }
    JobType currentJob() const { return m_currentJob; }
    void login(JobType job);
    void logout();
    
    // Serialization
    QJsonObject toJson() const;
    static Employee* fromJson(const QJsonObject& json, QObject* parent = nullptr);

signals:
    void nameChanged();
    void activeChanged();
    void loginStateChanged();
    void jobsChanged();

private:
    // Identification
    int m_id = 0;
    int m_key = 0;
    int m_employeeNo = 0;
    int m_accessCode = 0;
    
    // Names
    QString m_systemName;
    QString m_firstName;
    QString m_lastName;
    
    // Contact
    QString m_address;
    QString m_city;
    QString m_state;
    QString m_phone;
    QString m_ssn;
    
    // Employment
    bool m_active = true;
    bool m_training = false;
    int m_drawer = 0;
    QString m_password;
    int m_securityFlags = 0;
    
    // Jobs
    QList<JobInfo*> m_jobs;
    JobType m_currentJob = JobType::None;
    JobType m_lastJob = JobType::None;
};

//=============================================================================
// EmployeeManager - Manages all employees
//=============================================================================
class EmployeeManager : public QObject {
    Q_OBJECT

public:
    static EmployeeManager* instance();
    
    // Employee management
    Employee* createEmployee();
    Employee* findById(int id);
    Employee* findByKey(int key);
    Employee* findByAccessCode(int code);
    Employee* findByName(const QString& name);
    QList<Employee*> allEmployees() const { return m_employees; }
    QList<Employee*> activeEmployees() const;
    QList<Employee*> loggedInEmployees() const;
    
    void deleteEmployee(Employee* emp);
    
    // Current user
    Employee* currentUser() const { return m_currentUser; }
    bool login(int accessCode, JobType job = JobType::None);
    void logout();
    bool isLoggedIn() const { return m_currentUser != nullptr; }
    
    // Persistence
    bool saveToFile(const QString& path);
    bool loadFromFile(const QString& path);
    
    // Statistics
    int employeeCount() const { return m_employees.size(); }
    int nextId() const { return m_nextId; }

signals:
    void employeeCreated(Employee* emp);
    void employeeDeleted(Employee* emp);
    void employeesChanged();
    void userLoggedIn(Employee* emp);
    void userLoggedOut();

private:
    explicit EmployeeManager(QObject* parent = nullptr);
    static EmployeeManager* s_instance;
    
    QList<Employee*> m_employees;
    Employee* m_currentUser = nullptr;
    int m_nextId = 1;
};

// Helper function to get job title string
QString jobTypeToString(JobType type);

} // namespace vt2

Q_DECLARE_OPERATORS_FOR_FLAGS(vt2::SecurityFlags)

#endif // VT2_EMPLOYEE_HPP
