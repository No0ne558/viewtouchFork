// ViewTouch V2 - Check/Order System
// Modern C++/Qt6 reimplementation

#ifndef VT2_CHECK_HPP
#define VT2_CHECK_HPP

#include <QObject>
#include <QString>
#include <QDateTime>
#include <QList>
#include <QJsonObject>
#include <memory>

namespace vt2 {

// Check Status
enum class CheckStatus {
    Open = 1,
    Closed = 2,
    Voided = 3
};

// Check Type
enum class CheckType {
    Restaurant = 1,
    Takeout = 2,
    Bar = 3,
    Merchandise = 4,
    Delivery = 5,
    Catering = 6,
    Hotel = 7,
    Retail = 8,
    FastFood = 9,
    SelfOrder = 10,
    DineIn = 11,
    ToGo = 12,
    CallIn = 13
};

// Tender Types
enum class TenderType {
    Cash = 0,
    Check = 1,
    ChargeCard = 2,
    Coupon = 3,
    Gift = 4,
    Comp = 5,
    Account = 6,
    ChargeRoom = 7,
    Discount = 8,
    CapturedTip = 9,
    EmployeeMeal = 10,
    CreditCard = 11,
    DebitCard = 12,
    ChargedTip = 13,
    PaidTip = 16,
    Overage = 17,
    Change = 18,
    Payout = 19,
    MoneyLost = 20,
    Gratuity = 21,
    ItemComp = 22,
    Expense = 23
};

// Order status flags
enum class OrderStatus {
    None = 0,
    Final = 1,
    Sent = 2,
    Made = 4,
    Served = 8,
    Comp = 16,
    Shown = 32
};

// Forward declarations
class Order;
class Payment;
class SubCheck;
class Check;
class MenuItem;

//=============================================================================
// Order - A single item on a check
//=============================================================================
class Order : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString itemName READ itemName WRITE setItemName NOTIFY itemNameChanged)
    Q_PROPERTY(int quantity READ quantity WRITE setQuantity NOTIFY quantityChanged)
    Q_PROPERTY(int unitPrice READ unitPrice WRITE setUnitPrice NOTIFY unitPriceChanged)
    Q_PROPERTY(int totalPrice READ totalPrice NOTIFY totalPriceChanged)

public:
    explicit Order(QObject* parent = nullptr);
    Order(const QString& name, int price, int qty = 1, QObject* parent = nullptr);
    ~Order() override = default;

    // Properties
    QString itemName() const { return m_itemName; }
    void setItemName(const QString& name);
    
    int quantity() const { return m_quantity; }
    void setQuantity(int qty);
    
    int unitPrice() const { return m_unitPrice; }
    void setUnitPrice(int price);
    
    int totalPrice() const { return m_unitPrice * m_quantity; }
    
    int seat() const { return m_seat; }
    void setSeat(int seat) { m_seat = seat; }
    
    int status() const { return m_status; }
    void setStatus(int status) { m_status = status; }
    
    int itemType() const { return m_itemType; }
    void setItemType(int type) { m_itemType = type; }
    
    int itemFamily() const { return m_itemFamily; }
    void setItemFamily(int family) { m_itemFamily = family; }
    
    int userId() const { return m_userId; }
    void setUserId(int id) { m_userId = id; }
    
    bool isComp() const { return m_status & static_cast<int>(OrderStatus::Comp); }
    bool isSent() const { return m_status & static_cast<int>(OrderStatus::Sent); }
    bool isFinal() const { return m_status & static_cast<int>(OrderStatus::Final); }
    
    // Modifiers
    QList<Order*> modifiers() const { return m_modifiers; }
    void addModifier(Order* mod);
    void removeModifier(Order* mod);
    int modifierTotal() const;
    int totalWithModifiers() const { return totalPrice() + modifierTotal(); }
    
    // Serialization
    QJsonObject toJson() const;
    static Order* fromJson(const QJsonObject& json, QObject* parent = nullptr);

signals:
    void itemNameChanged();
    void quantityChanged();
    void unitPriceChanged();
    void totalPriceChanged();
    void modifiersChanged();

private:
    QString m_itemName;
    int m_quantity = 1;
    int m_unitPrice = 0;      // Price in cents
    int m_seat = 0;
    int m_status = 0;
    int m_itemType = 0;
    int m_itemFamily = 0;
    int m_userId = 0;
    QList<Order*> m_modifiers;
};

//=============================================================================
// Payment - A payment on a subcheck
//=============================================================================
class Payment : public QObject {
    Q_OBJECT
    Q_PROPERTY(TenderType tenderType READ tenderType WRITE setTenderType NOTIFY tenderTypeChanged)
    Q_PROPERTY(int amount READ amount WRITE setAmount NOTIFY amountChanged)

public:
    explicit Payment(QObject* parent = nullptr);
    Payment(TenderType type, int amount, QObject* parent = nullptr);
    ~Payment() override = default;

    TenderType tenderType() const { return m_tenderType; }
    void setTenderType(TenderType type);
    
    int amount() const { return m_amount; }
    void setAmount(int amount);
    
    int userId() const { return m_userId; }
    void setUserId(int id) { m_userId = id; }
    
    int tenderId() const { return m_tenderId; }
    void setTenderId(int id) { m_tenderId = id; }
    
    int flags() const { return m_flags; }
    void setFlags(int flags) { m_flags = flags; }
    
    int drawerId() const { return m_drawerId; }
    void setDrawerId(int id) { m_drawerId = id; }
    
    bool isDiscount() const;
    bool isFinal() const;
    
    QString description() const;
    
    // Serialization
    QJsonObject toJson() const;
    static Payment* fromJson(const QJsonObject& json, QObject* parent = nullptr);

signals:
    void tenderTypeChanged();
    void amountChanged();

private:
    TenderType m_tenderType = TenderType::Cash;
    int m_amount = 0;         // Amount in cents
    int m_userId = 0;
    int m_tenderId = 0;
    int m_flags = 0;
    int m_drawerId = 0;
};

//=============================================================================
// SubCheck - A sub-check containing orders and payments
//=============================================================================
class SubCheck : public QObject {
    Q_OBJECT
    Q_PROPERTY(CheckStatus status READ status WRITE setStatus NOTIFY statusChanged)
    Q_PROPERTY(int subtotal READ subtotal NOTIFY totalsChanged)
    Q_PROPERTY(int tax READ tax NOTIFY totalsChanged)
    Q_PROPERTY(int total READ total NOTIFY totalsChanged)
    Q_PROPERTY(int balance READ balance NOTIFY totalsChanged)

public:
    explicit SubCheck(QObject* parent = nullptr);
    ~SubCheck() override = default;

    int id() const { return m_id; }
    void setId(int id) { m_id = id; }
    
    int number() const { return m_number; }
    void setNumber(int num) { m_number = num; }
    
    CheckStatus status() const { return m_status; }
    void setStatus(CheckStatus status);
    
    // Orders
    QList<Order*> orders() const { return m_orders; }
    void addOrder(Order* order);
    void removeOrder(Order* order);
    Order* findOrder(int index, int seat = -1);
    int orderCount(int seat = -1) const;
    
    // Payments
    QList<Payment*> payments() const { return m_payments; }
    void addPayment(Payment* payment);
    void removePayment(Payment* payment);
    Payment* findPayment(TenderType type, int id = -1);
    int paymentCount() const { return m_payments.size(); }
    
    // Totals (all in cents)
    int subtotal() const { return m_subtotal; }
    int tax() const { return m_totalTax; }
    int total() const { return m_totalCost; }
    int totalPayments() const { return m_totalPayments; }
    int balance() const { return m_balance; }
    
    // Tax breakdown
    int foodTax() const { return m_foodTax; }
    int alcoholTax() const { return m_alcoholTax; }
    int merchandiseTax() const { return m_merchandiseTax; }
    
    // Operations
    void calculateTotals(double taxRate = 0.0);
    void consolidateOrders();
    void finalizeOrders();
    void voidCheck();
    bool close();
    
    // Serialization
    QJsonObject toJson() const;
    static SubCheck* fromJson(const QJsonObject& json, QObject* parent = nullptr);

signals:
    void statusChanged();
    void totalsChanged();
    void ordersChanged();
    void paymentsChanged();

private:
    int m_id = 0;
    int m_number = 1;
    CheckStatus m_status = CheckStatus::Open;
    
    QList<Order*> m_orders;
    QList<Payment*> m_payments;
    
    // Calculated totals
    int m_subtotal = 0;
    int m_foodTax = 0;
    int m_alcoholTax = 0;
    int m_merchandiseTax = 0;
    int m_totalTax = 0;
    int m_totalCost = 0;
    int m_totalPayments = 0;
    int m_balance = 0;
};

//=============================================================================
// Check - The main check object containing subchecks
//=============================================================================
class Check : public QObject {
    Q_OBJECT
    Q_PROPERTY(int checkNumber READ checkNumber NOTIFY checkNumberChanged)
    Q_PROPERTY(CheckType checkType READ checkType WRITE setCheckType NOTIFY checkTypeChanged)
    Q_PROPERTY(CheckStatus status READ status NOTIFY statusChanged)
    Q_PROPERTY(int tableNumber READ tableNumber WRITE setTableNumber NOTIFY tableNumberChanged)
    Q_PROPERTY(int guestCount READ guestCount WRITE setGuestCount NOTIFY guestCountChanged)

public:
    explicit Check(QObject* parent = nullptr);
    ~Check() override = default;

    // Properties
    int checkNumber() const { return m_checkNumber; }
    void setCheckNumber(int num) { m_checkNumber = num; emit checkNumberChanged(); }
    
    CheckType checkType() const { return m_checkType; }
    void setCheckType(CheckType type);
    
    CheckStatus status() const { return m_status; }
    
    int tableNumber() const { return m_tableNumber; }
    void setTableNumber(int num);
    
    int guestCount() const { return m_guestCount; }
    void setGuestCount(int count);
    
    QString customerName() const { return m_customerName; }
    void setCustomerName(const QString& name) { m_customerName = name; }
    
    QString phoneNumber() const { return m_phoneNumber; }
    void setPhoneNumber(const QString& phone) { m_phoneNumber = phone; }
    
    int employeeId() const { return m_employeeId; }
    void setEmployeeId(int id) { m_employeeId = id; }
    
    QDateTime createdTime() const { return m_createdTime; }
    QDateTime closedTime() const { return m_closedTime; }
    
    // SubChecks
    QList<SubCheck*> subChecks() const { return m_subChecks; }
    SubCheck* currentSubCheck();
    SubCheck* addSubCheck();
    void removeSubCheck(SubCheck* sc);
    int subCheckCount() const { return m_subChecks.size(); }
    
    // Convenience methods for single subcheck use
    void addOrder(Order* order);
    void removeOrder(Order* order);
    void addPayment(Payment* payment);
    
    // Totals
    int subtotal() const;
    int tax() const;
    int total() const;
    int totalPayments() const;
    int balance() const;
    
    // Operations
    void calculateTotals(double taxRate = 0.0);
    bool isSettled() const;
    bool close();
    void voidCheck();
    void reopen();
    
    // Flags
    bool isPrinted() const { return m_flags & 1; }
    void setPrinted(bool p) { m_flags = p ? (m_flags | 1) : (m_flags & ~1); }
    
    bool isTraining() const { return m_flags & 4; }
    void setTraining(bool t) { m_flags = t ? (m_flags | 4) : (m_flags & ~4); }
    
    // Serialization
    QJsonObject toJson() const;
    static Check* fromJson(const QJsonObject& json, QObject* parent = nullptr);

signals:
    void checkNumberChanged();
    void checkTypeChanged();
    void statusChanged();
    void tableNumberChanged();
    void guestCountChanged();
    void subChecksChanged();
    void totalsChanged();

private:
    int m_checkNumber = 0;
    CheckType m_checkType = CheckType::Restaurant;
    CheckStatus m_status = CheckStatus::Open;
    int m_tableNumber = 0;
    int m_guestCount = 1;
    int m_employeeId = 0;
    int m_flags = 0;
    
    QString m_customerName;
    QString m_phoneNumber;
    
    QDateTime m_createdTime;
    QDateTime m_closedTime;
    
    QList<SubCheck*> m_subChecks;
};

//=============================================================================
// CheckManager - Manages all checks in the system
//=============================================================================
class CheckManager : public QObject {
    Q_OBJECT

public:
    static CheckManager* instance();
    
    // Check management
    Check* createCheck(CheckType type = CheckType::Restaurant);
    Check* findCheck(int checkNumber);
    Check* findCheckByTable(int tableNumber);
    QList<Check*> openChecks() const;
    QList<Check*> allChecks() const { return m_checks; }
    
    void closeCheck(Check* check);
    void voidCheck(Check* check);
    void deleteCheck(Check* check);
    
    // Statistics
    int openCheckCount() const;
    int totalSales() const;
    int nextCheckNumber() const { return m_nextCheckNumber; }
    
    // Persistence
    bool saveToFile(const QString& path);
    bool loadFromFile(const QString& path);
    
signals:
    void checkCreated(Check* check);
    void checkClosed(Check* check);
    void checkVoided(Check* check);
    void checksChanged();

private:
    explicit CheckManager(QObject* parent = nullptr);
    static CheckManager* s_instance;
    
    QList<Check*> m_checks;
    int m_nextCheckNumber = 1;
};

} // namespace vt2

#endif // VT2_CHECK_HPP
