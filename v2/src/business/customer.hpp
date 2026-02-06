// ViewTouch V2 - Customer System
// Modern C++/Qt6 reimplementation

#ifndef VT2_CUSTOMER_HPP
#define VT2_CUSTOMER_HPP

#include <QObject>
#include <QString>
#include <QDateTime>
#include <QList>
#include <QJsonObject>

namespace vt2 {

//=============================================================================
// Customer - Customer information
//=============================================================================
class Customer : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString name READ name WRITE setName NOTIFY nameChanged)
    Q_PROPERTY(QString phone READ phone WRITE setPhone NOTIFY contactChanged)
    Q_PROPERTY(QString email READ email WRITE setEmail NOTIFY contactChanged)

public:
    explicit Customer(QObject* parent = nullptr);
    ~Customer() override = default;

    int id() const { return m_id; }
    void setId(int id) { m_id = id; }
    
    // Name
    QString name() const { return m_name; }
    void setName(const QString& name);
    
    QString firstName() const { return m_firstName; }
    void setFirstName(const QString& name) { m_firstName = name; }
    
    QString lastName() const { return m_lastName; }
    void setLastName(const QString& name) { m_lastName = name; }
    
    // Contact
    QString phone() const { return m_phone; }
    void setPhone(const QString& phone);
    
    QString phone2() const { return m_phone2; }
    void setPhone2(const QString& phone) { m_phone2 = phone; }
    
    QString email() const { return m_email; }
    void setEmail(const QString& email);
    
    // Address
    QString address() const { return m_address; }
    void setAddress(const QString& addr) { m_address = addr; }
    
    QString address2() const { return m_address2; }
    void setAddress2(const QString& addr) { m_address2 = addr; }
    
    QString city() const { return m_city; }
    void setCity(const QString& city) { m_city = city; }
    
    QString state() const { return m_state; }
    void setState(const QString& state) { m_state = state; }
    
    QString zip() const { return m_zip; }
    void setZip(const QString& zip) { m_zip = zip; }
    
    // Loyalty
    int loyaltyPoints() const { return m_loyaltyPoints; }
    void setLoyaltyPoints(int points) { m_loyaltyPoints = points; }
    void addLoyaltyPoints(int points) { m_loyaltyPoints += points; }
    
    QString loyaltyNumber() const { return m_loyaltyNumber; }
    void setLoyaltyNumber(const QString& num) { m_loyaltyNumber = num; }
    
    // Account
    int accountBalance() const { return m_accountBalance; }  // In cents
    void setAccountBalance(int balance) { m_accountBalance = balance; }
    void addToBalance(int amount) { m_accountBalance += amount; }
    
    int creditLimit() const { return m_creditLimit; }
    void setCreditLimit(int limit) { m_creditLimit = limit; }
    
    // Stats
    int visitCount() const { return m_visitCount; }
    void setVisitCount(int count) { m_visitCount = count; }
    void incrementVisits() { ++m_visitCount; }
    
    int totalSpent() const { return m_totalSpent; }  // In cents
    void setTotalSpent(int amount) { m_totalSpent = amount; }
    void addSpending(int amount) { m_totalSpent += amount; }
    
    QDateTime lastVisit() const { return m_lastVisit; }
    void setLastVisit(const QDateTime& dt) { m_lastVisit = dt; }
    
    QDateTime createdDate() const { return m_createdDate; }
    void setCreatedDate(const QDateTime& dt) { m_createdDate = dt; }
    
    // Notes
    QString notes() const { return m_notes; }
    void setNotes(const QString& notes) { m_notes = notes; }
    
    // Serialization
    QJsonObject toJson() const;
    static Customer* fromJson(const QJsonObject& json, QObject* parent = nullptr);

signals:
    void nameChanged();
    void contactChanged();

private:
    int m_id = 0;
    QString m_name;
    QString m_firstName;
    QString m_lastName;
    
    QString m_phone;
    QString m_phone2;
    QString m_email;
    
    QString m_address;
    QString m_address2;
    QString m_city;
    QString m_state;
    QString m_zip;
    
    int m_loyaltyPoints = 0;
    QString m_loyaltyNumber;
    
    int m_accountBalance = 0;
    int m_creditLimit = 0;
    
    int m_visitCount = 0;
    int m_totalSpent = 0;
    QDateTime m_lastVisit;
    QDateTime m_createdDate;
    
    QString m_notes;
};

//=============================================================================
// CustomerManager - Manages all customers
//=============================================================================
class CustomerManager : public QObject {
    Q_OBJECT

public:
    static CustomerManager* instance();
    
    // Customer management
    Customer* createCustomer(const QString& name = QString());
    Customer* findById(int id);
    Customer* findByPhone(const QString& phone);
    Customer* findByEmail(const QString& email);
    Customer* findByLoyaltyNumber(const QString& num);
    QList<Customer*> searchByName(const QString& name);
    QList<Customer*> allCustomers() const { return m_customers; }
    
    void deleteCustomer(Customer* customer);
    
    // Stats
    int customerCount() const { return m_customers.size(); }
    int totalLoyaltyPoints() const;
    
    // Persistence
    bool saveToFile(const QString& path);
    bool loadFromFile(const QString& path);

signals:
    void customerCreated(Customer* customer);
    void customerDeleted(Customer* customer);
    void customersChanged();

private:
    explicit CustomerManager(QObject* parent = nullptr);
    static CustomerManager* s_instance;
    
    QList<Customer*> m_customers;
    int m_nextId = 1;
};

} // namespace vt2

#endif // VT2_CUSTOMER_HPP
