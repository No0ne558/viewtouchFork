// ViewTouch V2 - Customer System Implementation
// Modern C++/Qt6 reimplementation

#include "customer.hpp"
#include <QJsonArray>
#include <QJsonDocument>
#include <QFile>

namespace vt2 {

//=============================================================================
// Customer Implementation
//=============================================================================

Customer::Customer(QObject* parent)
    : QObject(parent)
    , m_createdDate(QDateTime::currentDateTime())
{
}

void Customer::setName(const QString& name) {
    if (m_name != name) {
        m_name = name;
        emit nameChanged();
    }
}

void Customer::setPhone(const QString& phone) {
    if (m_phone != phone) {
        m_phone = phone;
        emit contactChanged();
    }
}

void Customer::setEmail(const QString& email) {
    if (m_email != email) {
        m_email = email;
        emit contactChanged();
    }
}

QJsonObject Customer::toJson() const {
    QJsonObject json;
    json["id"] = m_id;
    json["name"] = m_name;
    json["firstName"] = m_firstName;
    json["lastName"] = m_lastName;
    json["phone"] = m_phone;
    json["phone2"] = m_phone2;
    json["email"] = m_email;
    json["address"] = m_address;
    json["address2"] = m_address2;
    json["city"] = m_city;
    json["state"] = m_state;
    json["zip"] = m_zip;
    json["loyaltyPoints"] = m_loyaltyPoints;
    json["loyaltyNumber"] = m_loyaltyNumber;
    json["accountBalance"] = m_accountBalance;
    json["creditLimit"] = m_creditLimit;
    json["visitCount"] = m_visitCount;
    json["totalSpent"] = m_totalSpent;
    json["lastVisit"] = m_lastVisit.toString(Qt::ISODate);
    json["createdDate"] = m_createdDate.toString(Qt::ISODate);
    json["notes"] = m_notes;
    return json;
}

Customer* Customer::fromJson(const QJsonObject& json, QObject* parent) {
    auto* customer = new Customer(parent);
    customer->m_id = json["id"].toInt();
    customer->m_name = json["name"].toString();
    customer->m_firstName = json["firstName"].toString();
    customer->m_lastName = json["lastName"].toString();
    customer->m_phone = json["phone"].toString();
    customer->m_phone2 = json["phone2"].toString();
    customer->m_email = json["email"].toString();
    customer->m_address = json["address"].toString();
    customer->m_address2 = json["address2"].toString();
    customer->m_city = json["city"].toString();
    customer->m_state = json["state"].toString();
    customer->m_zip = json["zip"].toString();
    customer->m_loyaltyPoints = json["loyaltyPoints"].toInt();
    customer->m_loyaltyNumber = json["loyaltyNumber"].toString();
    customer->m_accountBalance = json["accountBalance"].toInt();
    customer->m_creditLimit = json["creditLimit"].toInt();
    customer->m_visitCount = json["visitCount"].toInt();
    customer->m_totalSpent = json["totalSpent"].toInt();
    customer->m_lastVisit = QDateTime::fromString(json["lastVisit"].toString(), Qt::ISODate);
    customer->m_createdDate = QDateTime::fromString(json["createdDate"].toString(), Qt::ISODate);
    customer->m_notes = json["notes"].toString();
    return customer;
}

//=============================================================================
// CustomerManager Implementation
//=============================================================================

CustomerManager* CustomerManager::s_instance = nullptr;

CustomerManager::CustomerManager(QObject* parent)
    : QObject(parent)
{
}

CustomerManager* CustomerManager::instance() {
    if (!s_instance) {
        s_instance = new CustomerManager();
    }
    return s_instance;
}

Customer* CustomerManager::createCustomer(const QString& name) {
    auto* customer = new Customer(this);
    customer->setId(m_nextId++);
    if (!name.isEmpty()) {
        customer->setName(name);
    }
    m_customers.append(customer);
    emit customerCreated(customer);
    emit customersChanged();
    return customer;
}

Customer* CustomerManager::findById(int id) {
    for (auto* customer : m_customers) {
        if (customer->id() == id) return customer;
    }
    return nullptr;
}

Customer* CustomerManager::findByPhone(const QString& phone) {
    for (auto* customer : m_customers) {
        if (customer->phone() == phone || customer->phone2() == phone) {
            return customer;
        }
    }
    return nullptr;
}

Customer* CustomerManager::findByEmail(const QString& email) {
    for (auto* customer : m_customers) {
        if (customer->email().compare(email, Qt::CaseInsensitive) == 0) {
            return customer;
        }
    }
    return nullptr;
}

Customer* CustomerManager::findByLoyaltyNumber(const QString& num) {
    for (auto* customer : m_customers) {
        if (customer->loyaltyNumber() == num) return customer;
    }
    return nullptr;
}

QList<Customer*> CustomerManager::searchByName(const QString& name) {
    QList<Customer*> results;
    QString lower = name.toLower();
    for (auto* customer : m_customers) {
        if (customer->name().toLower().contains(lower) ||
            customer->firstName().toLower().contains(lower) ||
            customer->lastName().toLower().contains(lower)) {
            results.append(customer);
        }
    }
    return results;
}

void CustomerManager::deleteCustomer(Customer* customer) {
    if (customer && m_customers.removeOne(customer)) {
        emit customerDeleted(customer);
        emit customersChanged();
        delete customer;
    }
}

int CustomerManager::totalLoyaltyPoints() const {
    int total = 0;
    for (const auto* customer : m_customers) {
        total += customer->loyaltyPoints();
    }
    return total;
}

bool CustomerManager::saveToFile(const QString& path) {
    QJsonObject root;
    root["nextId"] = m_nextId;
    
    QJsonArray customerArray;
    for (const auto* customer : m_customers) {
        customerArray.append(customer->toJson());
    }
    root["customers"] = customerArray;
    
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        return false;
    }
    
    QJsonDocument doc(root);
    file.write(doc.toJson());
    return true;
}

bool CustomerManager::loadFromFile(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return false;
    }
    
    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    QJsonObject root = doc.object();
    
    m_nextId = root["nextId"].toInt(1);
    
    qDeleteAll(m_customers);
    m_customers.clear();
    
    QJsonArray customerArray = root["customers"].toArray();
    for (const auto& ref : customerArray) {
        auto* customer = Customer::fromJson(ref.toObject(), this);
        m_customers.append(customer);
    }
    
    emit customersChanged();
    return true;
}

} // namespace vt2
