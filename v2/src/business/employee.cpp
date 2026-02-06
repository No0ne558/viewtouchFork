// ViewTouch V2 - Employee System Implementation
// Modern C++/Qt6 reimplementation

#include "employee.hpp"
#include <QJsonArray>
#include <QJsonDocument>
#include <QFile>

namespace vt2 {

QString jobTypeToString(JobType type) {
    switch (type) {
        case JobType::None:       return "None";
        case JobType::Dishwasher: return "Dishwasher";
        case JobType::Busperson:  return "Busperson";
        case JobType::Cook:       return "Cook";
        case JobType::Cook2:      return "Cook II";
        case JobType::Cook3:      return "Cook III";
        case JobType::Cashier:    return "Cashier";
        case JobType::Server:     return "Server";
        case JobType::Server2:    return "Server/Cashier";
        case JobType::Host:       return "Host";
        case JobType::Bookkeeper: return "Bookkeeper";
        case JobType::Manager:    return "Shift Supervisor";
        case JobType::Manager2:   return "Assistant Manager";
        case JobType::Manager3:   return "Manager";
        case JobType::Bartender:  return "Bartender";
        case JobType::Developer:  return "Developer";
        case JobType::Superuser:  return "Superuser";
        default:                  return "Unknown";
    }
}

//=============================================================================
// JobInfo Implementation
//=============================================================================

JobInfo::JobInfo(QObject* parent)
    : QObject(parent)
{
}

JobInfo::JobInfo(JobType job, int payAmount, PayRate rate, QObject* parent)
    : QObject(parent)
    , m_job(job)
    , m_payRate(rate)
    , m_payAmount(payAmount)
{
}

void JobInfo::setJob(JobType job) {
    if (m_job != job) {
        m_job = job;
        emit jobChanged();
    }
}

void JobInfo::setPayAmount(int amount) {
    if (m_payAmount != amount) {
        m_payAmount = amount;
        emit payAmountChanged();
    }
}

QString JobInfo::title() const {
    return jobTypeToString(m_job);
}

QJsonObject JobInfo::toJson() const {
    QJsonObject json;
    json["job"] = static_cast<int>(m_job);
    json["payRate"] = static_cast<int>(m_payRate);
    json["payAmount"] = m_payAmount;
    json["startingPage"] = m_startingPage;
    json["deptCode"] = m_deptCode;
    return json;
}

JobInfo* JobInfo::fromJson(const QJsonObject& json, QObject* parent) {
    auto* info = new JobInfo(parent);
    info->m_job = static_cast<JobType>(json["job"].toInt());
    info->m_payRate = static_cast<PayRate>(json["payRate"].toInt(1));
    info->m_payAmount = json["payAmount"].toInt();
    info->m_startingPage = json["startingPage"].toInt();
    info->m_deptCode = json["deptCode"].toInt();
    return info;
}

//=============================================================================
// Employee Implementation
//=============================================================================

Employee::Employee(QObject* parent)
    : QObject(parent)
{
}

void Employee::setSystemName(const QString& name) {
    if (m_systemName != name) {
        m_systemName = name;
        emit nameChanged();
    }
}

void Employee::setFirstName(const QString& name) {
    if (m_firstName != name) {
        m_firstName = name;
        emit nameChanged();
    }
}

void Employee::setLastName(const QString& name) {
    if (m_lastName != name) {
        m_lastName = name;
        emit nameChanged();
    }
}

void Employee::setActive(bool active) {
    if (m_active != active) {
        m_active = active;
        emit activeChanged();
    }
}

void Employee::addJob(JobInfo* job) {
    if (job && !m_jobs.contains(job)) {
        job->setParent(this);
        m_jobs.append(job);
        emit jobsChanged();
    }
}

void Employee::removeJob(JobInfo* job) {
    if (m_jobs.removeOne(job)) {
        emit jobsChanged();
    }
}

JobInfo* Employee::findJobByType(JobType type) {
    for (auto* job : m_jobs) {
        if (job->job() == type) return job;
    }
    return nullptr;
}

JobInfo* Employee::currentJobInfo() const {
    for (auto* job : m_jobs) {
        if (job->job() == m_currentJob) return job;
    }
    return m_jobs.isEmpty() ? nullptr : m_jobs.first();
}

QString Employee::jobTitle() const {
    auto* info = currentJobInfo();
    return info ? info->title() : jobTypeToString(m_currentJob);
}

void Employee::login(JobType job) {
    m_lastJob = m_currentJob;
    m_currentJob = job;
    emit loginStateChanged();
}

void Employee::logout() {
    m_lastJob = m_currentJob;
    m_currentJob = JobType::None;
    emit loginStateChanged();
}

QJsonObject Employee::toJson() const {
    QJsonObject json;
    json["id"] = m_id;
    json["key"] = m_key;
    json["employeeNo"] = m_employeeNo;
    json["accessCode"] = m_accessCode;
    json["systemName"] = m_systemName;
    json["firstName"] = m_firstName;
    json["lastName"] = m_lastName;
    json["address"] = m_address;
    json["city"] = m_city;
    json["state"] = m_state;
    json["phone"] = m_phone;
    json["ssn"] = m_ssn;
    json["active"] = m_active;
    json["training"] = m_training;
    json["drawer"] = m_drawer;
    json["password"] = m_password;
    json["securityFlags"] = m_securityFlags;
    
    QJsonArray jobArray;
    for (const auto* job : m_jobs) {
        jobArray.append(job->toJson());
    }
    json["jobs"] = jobArray;
    
    return json;
}

Employee* Employee::fromJson(const QJsonObject& json, QObject* parent) {
    auto* emp = new Employee(parent);
    emp->m_id = json["id"].toInt();
    emp->m_key = json["key"].toInt();
    emp->m_employeeNo = json["employeeNo"].toInt();
    emp->m_accessCode = json["accessCode"].toInt();
    emp->m_systemName = json["systemName"].toString();
    emp->m_firstName = json["firstName"].toString();
    emp->m_lastName = json["lastName"].toString();
    emp->m_address = json["address"].toString();
    emp->m_city = json["city"].toString();
    emp->m_state = json["state"].toString();
    emp->m_phone = json["phone"].toString();
    emp->m_ssn = json["ssn"].toString();
    emp->m_active = json["active"].toBool(true);
    emp->m_training = json["training"].toBool(false);
    emp->m_drawer = json["drawer"].toInt();
    emp->m_password = json["password"].toString();
    emp->m_securityFlags = json["securityFlags"].toInt();
    
    QJsonArray jobArray = json["jobs"].toArray();
    for (const auto& jobRef : jobArray) {
        auto* job = JobInfo::fromJson(jobRef.toObject(), emp);
        emp->m_jobs.append(job);
    }
    
    return emp;
}

//=============================================================================
// EmployeeManager Implementation
//=============================================================================

EmployeeManager* EmployeeManager::s_instance = nullptr;

EmployeeManager::EmployeeManager(QObject* parent)
    : QObject(parent)
{
}

EmployeeManager* EmployeeManager::instance() {
    if (!s_instance) {
        s_instance = new EmployeeManager();
    }
    return s_instance;
}

Employee* EmployeeManager::createEmployee() {
    auto* emp = new Employee(this);
    emp->setId(m_nextId++);
    emp->setKey(emp->id());
    m_employees.append(emp);
    emit employeeCreated(emp);
    emit employeesChanged();
    return emp;
}

Employee* EmployeeManager::findById(int id) {
    for (auto* emp : m_employees) {
        if (emp->id() == id) return emp;
    }
    return nullptr;
}

Employee* EmployeeManager::findByKey(int key) {
    for (auto* emp : m_employees) {
        if (emp->key() == key) return emp;
    }
    return nullptr;
}

Employee* EmployeeManager::findByAccessCode(int code) {
    for (auto* emp : m_employees) {
        if (emp->accessCode() == code) return emp;
    }
    return nullptr;
}

Employee* EmployeeManager::findByName(const QString& name) {
    for (auto* emp : m_employees) {
        if (emp->systemName() == name || emp->fullName() == name) {
            return emp;
        }
    }
    return nullptr;
}

QList<Employee*> EmployeeManager::activeEmployees() const {
    QList<Employee*> active;
    for (auto* emp : m_employees) {
        if (emp->isActive()) active.append(emp);
    }
    return active;
}

QList<Employee*> EmployeeManager::loggedInEmployees() const {
    QList<Employee*> loggedIn;
    for (auto* emp : m_employees) {
        if (emp->isLoggedIn()) loggedIn.append(emp);
    }
    return loggedIn;
}

void EmployeeManager::deleteEmployee(Employee* emp) {
    if (emp && m_employees.removeOne(emp)) {
        emit employeeDeleted(emp);
        emit employeesChanged();
        if (m_currentUser == emp) {
            m_currentUser = nullptr;
            emit userLoggedOut();
        }
        delete emp;
    }
}

bool EmployeeManager::login(int accessCode, JobType job) {
    auto* emp = findByAccessCode(accessCode);
    if (!emp || !emp->isActive()) return false;
    
    // Determine job to log in as
    JobType loginJob = job;
    if (loginJob == JobType::None && !emp->jobs().isEmpty()) {
        loginJob = emp->jobs().first()->job();
    }
    
    emp->login(loginJob);
    m_currentUser = emp;
    emit userLoggedIn(emp);
    return true;
}

void EmployeeManager::logout() {
    if (m_currentUser) {
        m_currentUser->logout();
        m_currentUser = nullptr;
        emit userLoggedOut();
    }
}

bool EmployeeManager::saveToFile(const QString& path) {
    QJsonObject root;
    root["nextId"] = m_nextId;
    
    QJsonArray empArray;
    for (const auto* emp : m_employees) {
        empArray.append(emp->toJson());
    }
    root["employees"] = empArray;
    
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        return false;
    }
    
    QJsonDocument doc(root);
    file.write(doc.toJson());
    return true;
}

bool EmployeeManager::loadFromFile(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return false;
    }
    
    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    QJsonObject root = doc.object();
    
    m_nextId = root["nextId"].toInt(1);
    
    qDeleteAll(m_employees);
    m_employees.clear();
    m_currentUser = nullptr;
    
    QJsonArray empArray = root["employees"].toArray();
    for (const auto& empRef : empArray) {
        auto* emp = Employee::fromJson(empRef.toObject(), this);
        m_employees.append(emp);
    }
    
    emit employeesChanged();
    return true;
}

} // namespace vt2
