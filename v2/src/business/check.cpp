// ViewTouch V2 - Check/Order System Implementation
// Modern C++/Qt6 reimplementation

#include "check.hpp"
#include <QJsonArray>
#include <QJsonDocument>
#include <QFile>

namespace vt2 {

//=============================================================================
// Order Implementation
//=============================================================================

Order::Order(QObject* parent)
    : QObject(parent)
{
}

Order::Order(const QString& name, int price, int qty, QObject* parent)
    : QObject(parent)
    , m_itemName(name)
    , m_quantity(qty)
    , m_unitPrice(price)
{
}

void Order::setItemName(const QString& name) {
    if (m_itemName != name) {
        m_itemName = name;
        emit itemNameChanged();
    }
}

void Order::setQuantity(int qty) {
    if (m_quantity != qty && qty > 0) {
        m_quantity = qty;
        emit quantityChanged();
        emit totalPriceChanged();
    }
}

void Order::setUnitPrice(int price) {
    if (m_unitPrice != price) {
        m_unitPrice = price;
        emit unitPriceChanged();
        emit totalPriceChanged();
    }
}

void Order::addModifier(Order* mod) {
    if (mod && !m_modifiers.contains(mod)) {
        mod->setParent(this);
        m_modifiers.append(mod);
        emit modifiersChanged();
    }
}

void Order::removeModifier(Order* mod) {
    if (m_modifiers.removeOne(mod)) {
        emit modifiersChanged();
    }
}

int Order::modifierTotal() const {
    int total = 0;
    for (const auto* mod : m_modifiers) {
        total += mod->totalPrice();
    }
    return total;
}

QJsonObject Order::toJson() const {
    QJsonObject json;
    json["itemName"] = m_itemName;
    json["quantity"] = m_quantity;
    json["unitPrice"] = m_unitPrice;
    json["seat"] = m_seat;
    json["status"] = m_status;
    json["itemType"] = m_itemType;
    json["itemFamily"] = m_itemFamily;
    json["userId"] = m_userId;
    
    QJsonArray modArray;
    for (const auto* mod : m_modifiers) {
        modArray.append(mod->toJson());
    }
    json["modifiers"] = modArray;
    
    return json;
}

Order* Order::fromJson(const QJsonObject& json, QObject* parent) {
    auto* order = new Order(parent);
    order->m_itemName = json["itemName"].toString();
    order->m_quantity = json["quantity"].toInt(1);
    order->m_unitPrice = json["unitPrice"].toInt();
    order->m_seat = json["seat"].toInt();
    order->m_status = json["status"].toInt();
    order->m_itemType = json["itemType"].toInt();
    order->m_itemFamily = json["itemFamily"].toInt();
    order->m_userId = json["userId"].toInt();
    
    QJsonArray modArray = json["modifiers"].toArray();
    for (const auto& modRef : modArray) {
        auto* mod = Order::fromJson(modRef.toObject(), order);
        order->m_modifiers.append(mod);
    }
    
    return order;
}

//=============================================================================
// Payment Implementation
//=============================================================================

Payment::Payment(QObject* parent)
    : QObject(parent)
{
}

Payment::Payment(TenderType type, int amount, QObject* parent)
    : QObject(parent)
    , m_tenderType(type)
    , m_amount(amount)
{
}

void Payment::setTenderType(TenderType type) {
    if (m_tenderType != type) {
        m_tenderType = type;
        emit tenderTypeChanged();
    }
}

void Payment::setAmount(int amount) {
    if (m_amount != amount) {
        m_amount = amount;
        emit amountChanged();
    }
}

bool Payment::isDiscount() const {
    return m_tenderType == TenderType::Discount ||
           m_tenderType == TenderType::Coupon ||
           m_tenderType == TenderType::Comp ||
           m_tenderType == TenderType::ItemComp;
}

bool Payment::isFinal() const {
    return m_flags & 128;  // TF_FINAL
}

QString Payment::description() const {
    switch (m_tenderType) {
        case TenderType::Cash:        return "Cash";
        case TenderType::Check:       return "Check";
        case TenderType::CreditCard:  return "Credit Card";
        case TenderType::DebitCard:   return "Debit Card";
        case TenderType::Gift:        return "Gift Certificate";
        case TenderType::Coupon:      return "Coupon";
        case TenderType::Discount:    return "Discount";
        case TenderType::Comp:        return "Comp";
        case TenderType::EmployeeMeal: return "Employee Meal";
        case TenderType::Gratuity:    return "Gratuity";
        case TenderType::ChargeRoom:  return "Room Charge";
        case TenderType::Account:     return "On Account";
        default:                      return "Payment";
    }
}

QJsonObject Payment::toJson() const {
    QJsonObject json;
    json["tenderType"] = static_cast<int>(m_tenderType);
    json["amount"] = m_amount;
    json["userId"] = m_userId;
    json["tenderId"] = m_tenderId;
    json["flags"] = m_flags;
    json["drawerId"] = m_drawerId;
    return json;
}

Payment* Payment::fromJson(const QJsonObject& json, QObject* parent) {
    auto* payment = new Payment(parent);
    payment->m_tenderType = static_cast<TenderType>(json["tenderType"].toInt());
    payment->m_amount = json["amount"].toInt();
    payment->m_userId = json["userId"].toInt();
    payment->m_tenderId = json["tenderId"].toInt();
    payment->m_flags = json["flags"].toInt();
    payment->m_drawerId = json["drawerId"].toInt();
    return payment;
}

//=============================================================================
// SubCheck Implementation
//=============================================================================

SubCheck::SubCheck(QObject* parent)
    : QObject(parent)
{
}

void SubCheck::setStatus(CheckStatus status) {
    if (m_status != status) {
        m_status = status;
        emit statusChanged();
    }
}

void SubCheck::addOrder(Order* order) {
    if (order && !m_orders.contains(order)) {
        order->setParent(this);
        m_orders.append(order);
        emit ordersChanged();
    }
}

void SubCheck::removeOrder(Order* order) {
    if (m_orders.removeOne(order)) {
        emit ordersChanged();
    }
}

Order* SubCheck::findOrder(int index, int seat) {
    int count = 0;
    for (auto* order : m_orders) {
        if (seat < 0 || order->seat() == seat) {
            if (count == index) return order;
            ++count;
        }
    }
    return nullptr;
}

int SubCheck::orderCount(int seat) const {
    if (seat < 0) return m_orders.size();
    int count = 0;
    for (const auto* order : m_orders) {
        if (order->seat() == seat) ++count;
    }
    return count;
}

void SubCheck::addPayment(Payment* payment) {
    if (payment && !m_payments.contains(payment)) {
        payment->setParent(this);
        m_payments.append(payment);
        emit paymentsChanged();
    }
}

void SubCheck::removePayment(Payment* payment) {
    if (m_payments.removeOne(payment)) {
        emit paymentsChanged();
    }
}

Payment* SubCheck::findPayment(TenderType type, int id) {
    for (auto* payment : m_payments) {
        if (payment->tenderType() == type) {
            if (id < 0 || payment->tenderId() == id) {
                return payment;
            }
        }
    }
    return nullptr;
}

void SubCheck::calculateTotals(double taxRate) {
    // Calculate subtotal from orders
    m_subtotal = 0;
    for (const auto* order : m_orders) {
        if (!order->isComp()) {
            m_subtotal += order->totalWithModifiers();
        }
    }
    
    // Calculate tax
    m_totalTax = static_cast<int>(m_subtotal * taxRate);
    m_foodTax = m_totalTax;  // Simplified - all food tax for now
    m_alcoholTax = 0;
    m_merchandiseTax = 0;
    
    // Calculate total cost
    m_totalCost = m_subtotal + m_totalTax;
    
    // Calculate payments
    m_totalPayments = 0;
    for (const auto* payment : m_payments) {
        if (payment->isDiscount()) {
            m_totalCost -= payment->amount();
        } else {
            m_totalPayments += payment->amount();
        }
    }
    
    // Calculate balance
    m_balance = m_totalCost - m_totalPayments;
    
    emit totalsChanged();
}

void SubCheck::consolidateOrders() {
    // Combine identical orders
    for (int i = 0; i < m_orders.size(); ++i) {
        for (int j = i + 1; j < m_orders.size(); ) {
            auto* a = m_orders[i];
            auto* b = m_orders[j];
            
            if (a->itemName() == b->itemName() &&
                a->unitPrice() == b->unitPrice() &&
                a->seat() == b->seat() &&
                a->status() == b->status() &&
                a->modifiers().isEmpty() && b->modifiers().isEmpty()) {
                
                a->setQuantity(a->quantity() + b->quantity());
                m_orders.removeAt(j);
                delete b;
            } else {
                ++j;
            }
        }
    }
    emit ordersChanged();
}

void SubCheck::finalizeOrders() {
    for (auto* order : m_orders) {
        order->setStatus(order->status() | static_cast<int>(OrderStatus::Final));
    }
}

void SubCheck::voidCheck() {
    m_status = CheckStatus::Voided;
    emit statusChanged();
}

bool SubCheck::close() {
    if (m_balance > 0) return false;  // Not fully paid
    m_status = CheckStatus::Closed;
    emit statusChanged();
    return true;
}

QJsonObject SubCheck::toJson() const {
    QJsonObject json;
    json["id"] = m_id;
    json["number"] = m_number;
    json["status"] = static_cast<int>(m_status);
    
    QJsonArray orderArray;
    for (const auto* order : m_orders) {
        orderArray.append(order->toJson());
    }
    json["orders"] = orderArray;
    
    QJsonArray paymentArray;
    for (const auto* payment : m_payments) {
        paymentArray.append(payment->toJson());
    }
    json["payments"] = paymentArray;
    
    return json;
}

SubCheck* SubCheck::fromJson(const QJsonObject& json, QObject* parent) {
    auto* sc = new SubCheck(parent);
    sc->m_id = json["id"].toInt();
    sc->m_number = json["number"].toInt(1);
    sc->m_status = static_cast<CheckStatus>(json["status"].toInt(1));
    
    QJsonArray orderArray = json["orders"].toArray();
    for (const auto& orderRef : orderArray) {
        auto* order = Order::fromJson(orderRef.toObject(), sc);
        sc->m_orders.append(order);
    }
    
    QJsonArray paymentArray = json["payments"].toArray();
    for (const auto& payRef : paymentArray) {
        auto* payment = Payment::fromJson(payRef.toObject(), sc);
        sc->m_payments.append(payment);
    }
    
    return sc;
}

//=============================================================================
// Check Implementation
//=============================================================================

Check::Check(QObject* parent)
    : QObject(parent)
    , m_createdTime(QDateTime::currentDateTime())
{
    // Create default subcheck
    addSubCheck();
}

void Check::setCheckType(CheckType type) {
    if (m_checkType != type) {
        m_checkType = type;
        emit checkTypeChanged();
    }
}

void Check::setTableNumber(int num) {
    if (m_tableNumber != num) {
        m_tableNumber = num;
        emit tableNumberChanged();
    }
}

void Check::setGuestCount(int count) {
    if (m_guestCount != count && count > 0) {
        m_guestCount = count;
        emit guestCountChanged();
    }
}

SubCheck* Check::currentSubCheck() {
    if (m_subChecks.isEmpty()) {
        return addSubCheck();
    }
    return m_subChecks.last();
}

SubCheck* Check::addSubCheck() {
    auto* sc = new SubCheck(this);
    sc->setNumber(m_subChecks.size() + 1);
    m_subChecks.append(sc);
    emit subChecksChanged();
    return sc;
}

void Check::removeSubCheck(SubCheck* sc) {
    if (m_subChecks.removeOne(sc)) {
        emit subChecksChanged();
    }
}

void Check::addOrder(Order* order) {
    currentSubCheck()->addOrder(order);
}

void Check::removeOrder(Order* order) {
    for (auto* sc : m_subChecks) {
        sc->removeOrder(order);
    }
}

void Check::addPayment(Payment* payment) {
    currentSubCheck()->addPayment(payment);
}

int Check::subtotal() const {
    int total = 0;
    for (const auto* sc : m_subChecks) {
        total += sc->subtotal();
    }
    return total;
}

int Check::tax() const {
    int total = 0;
    for (const auto* sc : m_subChecks) {
        total += sc->tax();
    }
    return total;
}

int Check::total() const {
    int t = 0;
    for (const auto* sc : m_subChecks) {
        t += sc->total();
    }
    return t;
}

int Check::totalPayments() const {
    int total = 0;
    for (const auto* sc : m_subChecks) {
        total += sc->totalPayments();
    }
    return total;
}

int Check::balance() const {
    int b = 0;
    for (const auto* sc : m_subChecks) {
        b += sc->balance();
    }
    return b;
}

void Check::calculateTotals(double taxRate) {
    for (auto* sc : m_subChecks) {
        sc->calculateTotals(taxRate);
    }
    emit totalsChanged();
}

bool Check::isSettled() const {
    for (const auto* sc : m_subChecks) {
        if (sc->status() == CheckStatus::Open) return false;
    }
    return true;
}

bool Check::close() {
    for (auto* sc : m_subChecks) {
        if (sc->status() == CheckStatus::Open) {
            if (!sc->close()) return false;
        }
    }
    m_status = CheckStatus::Closed;
    m_closedTime = QDateTime::currentDateTime();
    emit statusChanged();
    return true;
}

void Check::voidCheck() {
    for (auto* sc : m_subChecks) {
        sc->voidCheck();
    }
    m_status = CheckStatus::Voided;
    emit statusChanged();
}

void Check::reopen() {
    if (m_status != CheckStatus::Open) {
        m_status = CheckStatus::Open;
        for (auto* sc : m_subChecks) {
            sc->setStatus(CheckStatus::Open);
        }
        emit statusChanged();
    }
}

QJsonObject Check::toJson() const {
    QJsonObject json;
    json["checkNumber"] = m_checkNumber;
    json["checkType"] = static_cast<int>(m_checkType);
    json["status"] = static_cast<int>(m_status);
    json["tableNumber"] = m_tableNumber;
    json["guestCount"] = m_guestCount;
    json["employeeId"] = m_employeeId;
    json["flags"] = m_flags;
    json["customerName"] = m_customerName;
    json["phoneNumber"] = m_phoneNumber;
    json["createdTime"] = m_createdTime.toString(Qt::ISODate);
    if (m_closedTime.isValid()) {
        json["closedTime"] = m_closedTime.toString(Qt::ISODate);
    }
    
    QJsonArray scArray;
    for (const auto* sc : m_subChecks) {
        scArray.append(sc->toJson());
    }
    json["subChecks"] = scArray;
    
    return json;
}

Check* Check::fromJson(const QJsonObject& json, QObject* parent) {
    auto* check = new Check(parent);
    // Clear default subcheck
    qDeleteAll(check->m_subChecks);
    check->m_subChecks.clear();
    
    check->m_checkNumber = json["checkNumber"].toInt();
    check->m_checkType = static_cast<CheckType>(json["checkType"].toInt(1));
    check->m_status = static_cast<CheckStatus>(json["status"].toInt(1));
    check->m_tableNumber = json["tableNumber"].toInt();
    check->m_guestCount = json["guestCount"].toInt(1);
    check->m_employeeId = json["employeeId"].toInt();
    check->m_flags = json["flags"].toInt();
    check->m_customerName = json["customerName"].toString();
    check->m_phoneNumber = json["phoneNumber"].toString();
    check->m_createdTime = QDateTime::fromString(json["createdTime"].toString(), Qt::ISODate);
    if (json.contains("closedTime")) {
        check->m_closedTime = QDateTime::fromString(json["closedTime"].toString(), Qt::ISODate);
    }
    
    QJsonArray scArray = json["subChecks"].toArray();
    for (const auto& scRef : scArray) {
        auto* sc = SubCheck::fromJson(scRef.toObject(), check);
        check->m_subChecks.append(sc);
    }
    
    return check;
}

//=============================================================================
// CheckManager Implementation
//=============================================================================

CheckManager* CheckManager::s_instance = nullptr;

CheckManager::CheckManager(QObject* parent)
    : QObject(parent)
{
}

CheckManager* CheckManager::instance() {
    if (!s_instance) {
        s_instance = new CheckManager();
    }
    return s_instance;
}

Check* CheckManager::createCheck(CheckType type) {
    auto* check = new Check(this);
    check->setCheckNumber(m_nextCheckNumber++);
    check->setCheckType(type);
    m_checks.append(check);
    emit checkCreated(check);
    emit checksChanged();
    return check;
}

Check* CheckManager::findCheck(int checkNumber) {
    for (auto* check : m_checks) {
        if (check->checkNumber() == checkNumber) {
            return check;
        }
    }
    return nullptr;
}

Check* CheckManager::findCheckByTable(int tableNumber) {
    for (auto* check : m_checks) {
        if (check->tableNumber() == tableNumber && 
            check->status() == CheckStatus::Open) {
            return check;
        }
    }
    return nullptr;
}

QList<Check*> CheckManager::openChecks() const {
    QList<Check*> open;
    for (auto* check : m_checks) {
        if (check->status() == CheckStatus::Open) {
            open.append(check);
        }
    }
    return open;
}

void CheckManager::closeCheck(Check* check) {
    if (check && check->close()) {
        emit checkClosed(check);
        emit checksChanged();
    }
}

void CheckManager::voidCheck(Check* check) {
    if (check) {
        check->voidCheck();
        emit checkVoided(check);
        emit checksChanged();
    }
}

void CheckManager::deleteCheck(Check* check) {
    if (check && m_checks.removeOne(check)) {
        delete check;
        emit checksChanged();
    }
}

int CheckManager::openCheckCount() const {
    int count = 0;
    for (const auto* check : m_checks) {
        if (check->status() == CheckStatus::Open) ++count;
    }
    return count;
}

int CheckManager::totalSales() const {
    int total = 0;
    for (const auto* check : m_checks) {
        if (check->status() == CheckStatus::Closed) {
            total += check->total();
        }
    }
    return total;
}

bool CheckManager::saveToFile(const QString& path) {
    QJsonObject root;
    root["nextCheckNumber"] = m_nextCheckNumber;
    
    QJsonArray checksArray;
    for (const auto* check : m_checks) {
        checksArray.append(check->toJson());
    }
    root["checks"] = checksArray;
    
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        return false;
    }
    
    QJsonDocument doc(root);
    file.write(doc.toJson());
    return true;
}

bool CheckManager::loadFromFile(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return false;
    }
    
    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    QJsonObject root = doc.object();
    
    m_nextCheckNumber = root["nextCheckNumber"].toInt(1);
    
    qDeleteAll(m_checks);
    m_checks.clear();
    
    QJsonArray checksArray = root["checks"].toArray();
    for (const auto& checkRef : checksArray) {
        auto* check = Check::fromJson(checkRef.toObject(), this);
        m_checks.append(check);
    }
    
    emit checksChanged();
    return true;
}

} // namespace vt2
