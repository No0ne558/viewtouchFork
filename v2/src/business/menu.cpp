// ViewTouch V2 - Menu Item System Implementation
// Modern C++/Qt6 reimplementation

#include "menu.hpp"
#include <QJsonArray>
#include <QJsonDocument>
#include <QFile>

namespace vt2 {

QString itemFamilyToString(ItemFamily family) {
    switch (family) {
        case ItemFamily::Unknown:     return "Unknown";
        case ItemFamily::Appetizer:   return "Appetizer";
        case ItemFamily::Soup:        return "Soup";
        case ItemFamily::Salad:       return "Salad";
        case ItemFamily::Entree:      return "Entree";
        case ItemFamily::Dessert:     return "Dessert";
        case ItemFamily::Beverage:    return "Beverage";
        case ItemFamily::Alcohol:     return "Alcohol";
        case ItemFamily::Beer:        return "Beer";
        case ItemFamily::Wine:        return "Wine";
        case ItemFamily::Coffee:      return "Coffee";
        case ItemFamily::Side:        return "Side";
        case ItemFamily::Bread:       return "Bread";
        case ItemFamily::Sandwich:    return "Sandwich";
        case ItemFamily::Pizza:       return "Pizza";
        case ItemFamily::Seafood:     return "Seafood";
        case ItemFamily::Steak:       return "Steak";
        case ItemFamily::Pasta:       return "Pasta";
        case ItemFamily::Chicken:     return "Chicken";
        case ItemFamily::Kids:        return "Kids";
        case ItemFamily::Combo:       return "Combo";
        case ItemFamily::Retail:      return "Retail";
        case ItemFamily::Merchandise: return "Merchandise";
        default:                      return "Unknown";
    }
}

QString salesTypeToString(SalesType type) {
    switch (type) {
        case SalesType::None:        return "None";
        case SalesType::Food:        return "Food";
        case SalesType::Beverage:    return "Beverage";
        case SalesType::Alcohol:     return "Alcohol";
        case SalesType::Beer:        return "Beer";
        case SalesType::Wine:        return "Wine";
        case SalesType::Merchandise: return "Merchandise";
        case SalesType::Room:        return "Room";
        default:                     return "Unknown";
    }
}

//=============================================================================
// MenuItem Implementation
//=============================================================================

MenuItem::MenuItem(QObject* parent)
    : QObject(parent)
{
}

MenuItem::MenuItem(const QString& name, int price, QObject* parent)
    : QObject(parent)
    , m_name(name)
    , m_price(price)
{
}

void MenuItem::setName(const QString& name) {
    if (m_name != name) {
        m_name = name;
        emit nameChanged();
    }
}

void MenuItem::setShortName(const QString& name) {
    if (m_shortName != name) {
        m_shortName = name;
        emit nameChanged();
    }
}

void MenuItem::setPrice(int price) {
    if (m_price != price) {
        m_price = price;
        emit priceChanged();
    }
}

void MenuItem::setActive(bool active) {
    if (m_active != active) {
        m_active = active;
        emit activeChanged();
    }
}

void MenuItem::addAllowedModifier(MenuItem* mod) {
    if (mod && !m_allowedModifiers.contains(mod)) {
        m_allowedModifiers.append(mod);
        emit modifiersChanged();
    }
}

void MenuItem::removeAllowedModifier(MenuItem* mod) {
    if (m_allowedModifiers.removeOne(mod)) {
        emit modifiersChanged();
    }
}

bool MenuItem::canHaveModifier(MenuItem* mod) const {
    if (m_allowedModifiers.isEmpty()) return true;  // Allow all if none specified
    return m_allowedModifiers.contains(mod);
}

QJsonObject MenuItem::toJson() const {
    QJsonObject json;
    json["id"] = m_id;
    json["name"] = m_name;
    json["shortName"] = m_shortName;
    json["zoneName"] = m_zoneName;
    json["printName"] = m_printName;
    json["price"] = m_price;
    json["cost"] = m_cost;
    json["employeePrice"] = m_employeePrice;
    json["itemType"] = static_cast<int>(m_itemType);
    json["family"] = static_cast<int>(m_family);
    json["salesType"] = static_cast<int>(m_salesType);
    json["printerId"] = m_printerId;
    json["callOrder"] = m_callOrder;
    json["active"] = m_active;
    
    // Store modifier IDs
    QJsonArray modIds;
    for (const auto* mod : m_allowedModifiers) {
        modIds.append(mod->id());
    }
    json["allowedModifierIds"] = modIds;
    
    return json;
}

MenuItem* MenuItem::fromJson(const QJsonObject& json, QObject* parent) {
    auto* item = new MenuItem(parent);
    item->m_id = json["id"].toInt();
    item->m_name = json["name"].toString();
    item->m_shortName = json["shortName"].toString();
    item->m_zoneName = json["zoneName"].toString();
    item->m_printName = json["printName"].toString();
    item->m_price = json["price"].toInt();
    item->m_cost = json["cost"].toInt();
    item->m_employeePrice = json["employeePrice"].toInt();
    item->m_itemType = static_cast<ItemType>(json["itemType"].toInt());
    item->m_family = static_cast<ItemFamily>(json["family"].toInt());
    item->m_salesType = static_cast<SalesType>(json["salesType"].toInt(1));
    item->m_printerId = json["printerId"].toInt();
    item->m_callOrder = json["callOrder"].toInt();
    item->m_active = json["active"].toBool(true);
    // Note: allowedModifiers must be linked after all items are loaded
    return item;
}

//=============================================================================
// MenuCategory Implementation
//=============================================================================

MenuCategory::MenuCategory(QObject* parent)
    : QObject(parent)
{
}

MenuCategory::MenuCategory(const QString& name, QObject* parent)
    : QObject(parent)
    , m_name(name)
{
}

void MenuCategory::setName(const QString& name) {
    if (m_name != name) {
        m_name = name;
        emit nameChanged();
    }
}

void MenuCategory::addItem(MenuItem* item) {
    if (item && !m_items.contains(item)) {
        item->setParent(this);
        m_items.append(item);
        emit itemsChanged();
    }
}

void MenuCategory::removeItem(MenuItem* item) {
    if (m_items.removeOne(item)) {
        emit itemsChanged();
    }
}

MenuItem* MenuCategory::findItem(int id) {
    for (auto* item : m_items) {
        if (item->id() == id) return item;
    }
    return nullptr;
}

MenuItem* MenuCategory::findItemByName(const QString& name) {
    for (auto* item : m_items) {
        if (item->name() == name) return item;
    }
    return nullptr;
}

QJsonObject MenuCategory::toJson() const {
    QJsonObject json;
    json["id"] = m_id;
    json["name"] = m_name;
    json["family"] = static_cast<int>(m_family);
    
    QJsonArray itemArray;
    for (const auto* item : m_items) {
        itemArray.append(item->toJson());
    }
    json["items"] = itemArray;
    
    return json;
}

MenuCategory* MenuCategory::fromJson(const QJsonObject& json, QObject* parent) {
    auto* cat = new MenuCategory(parent);
    cat->m_id = json["id"].toInt();
    cat->m_name = json["name"].toString();
    cat->m_family = static_cast<ItemFamily>(json["family"].toInt());
    
    QJsonArray itemArray = json["items"].toArray();
    for (const auto& itemRef : itemArray) {
        auto* item = MenuItem::fromJson(itemRef.toObject(), cat);
        cat->m_items.append(item);
    }
    
    return cat;
}

//=============================================================================
// MenuManager Implementation
//=============================================================================

MenuManager* MenuManager::s_instance = nullptr;

MenuManager::MenuManager(QObject* parent)
    : QObject(parent)
{
}

MenuManager* MenuManager::instance() {
    if (!s_instance) {
        s_instance = new MenuManager();
    }
    return s_instance;
}

MenuCategory* MenuManager::createCategory(const QString& name) {
    auto* cat = new MenuCategory(name, this);
    cat->setId(m_nextCategoryId++);
    m_categories.append(cat);
    emit categoryCreated(cat);
    emit menuChanged();
    return cat;
}

MenuCategory* MenuManager::findCategory(int id) {
    for (auto* cat : m_categories) {
        if (cat->id() == id) return cat;
    }
    return nullptr;
}

MenuCategory* MenuManager::findCategoryByName(const QString& name) {
    for (auto* cat : m_categories) {
        if (cat->name() == name) return cat;
    }
    return nullptr;
}

void MenuManager::deleteCategory(MenuCategory* cat) {
    if (cat && m_categories.removeOne(cat)) {
        emit categoryDeleted(cat);
        emit menuChanged();
        delete cat;
    }
}

MenuItem* MenuManager::createItem(const QString& name, int price) {
    auto* item = new MenuItem(name, price, this);
    item->setId(m_nextItemId++);
    emit itemCreated(item);
    emit menuChanged();
    return item;
}

MenuItem* MenuManager::findItem(int id) {
    for (auto* cat : m_categories) {
        auto* item = cat->findItem(id);
        if (item) return item;
    }
    return nullptr;
}

MenuItem* MenuManager::findItemByName(const QString& name) {
    for (auto* cat : m_categories) {
        auto* item = cat->findItemByName(name);
        if (item) return item;
    }
    return nullptr;
}

QList<MenuItem*> MenuManager::allItems() const {
    QList<MenuItem*> items;
    for (auto* cat : m_categories) {
        items.append(cat->items());
    }
    return items;
}

QList<MenuItem*> MenuManager::activeItems() const {
    QList<MenuItem*> items;
    for (auto* cat : m_categories) {
        for (auto* item : cat->items()) {
            if (item->isActive()) items.append(item);
        }
    }
    return items;
}

QList<MenuItem*> MenuManager::itemsByFamily(ItemFamily family) const {
    QList<MenuItem*> items;
    for (auto* cat : m_categories) {
        for (auto* item : cat->items()) {
            if (item->family() == family) items.append(item);
        }
    }
    return items;
}

void MenuManager::deleteItem(MenuItem* item) {
    if (!item) return;
    for (auto* cat : m_categories) {
        if (cat->items().contains(item)) {
            cat->removeItem(item);
            emit itemDeleted(item);
            emit menuChanged();
            delete item;
            return;
        }
    }
}

QList<MenuItem*> MenuManager::modifiers() const {
    QList<MenuItem*> mods;
    for (auto* cat : m_categories) {
        for (auto* item : cat->items()) {
            if (item->itemType() == ItemType::Modifier) {
                mods.append(item);
            }
        }
    }
    return mods;
}

int MenuManager::itemCount() const {
    int count = 0;
    for (auto* cat : m_categories) {
        count += cat->itemCount();
    }
    return count;
}

bool MenuManager::saveToFile(const QString& path) {
    QJsonObject root;
    root["nextItemId"] = m_nextItemId;
    root["nextCategoryId"] = m_nextCategoryId;
    
    QJsonArray catArray;
    for (const auto* cat : m_categories) {
        catArray.append(cat->toJson());
    }
    root["categories"] = catArray;
    
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        return false;
    }
    
    QJsonDocument doc(root);
    file.write(doc.toJson());
    return true;
}

bool MenuManager::loadFromFile(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return false;
    }
    
    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    QJsonObject root = doc.object();
    
    m_nextItemId = root["nextItemId"].toInt(1);
    m_nextCategoryId = root["nextCategoryId"].toInt(1);
    
    qDeleteAll(m_categories);
    m_categories.clear();
    
    QJsonArray catArray = root["categories"].toArray();
    for (const auto& catRef : catArray) {
        auto* cat = MenuCategory::fromJson(catRef.toObject(), this);
        m_categories.append(cat);
    }
    
    // Link modifiers (second pass)
    QMap<int, MenuItem*> itemMap;
    for (auto* cat : m_categories) {
        for (auto* item : cat->items()) {
            itemMap[item->id()] = item;
        }
    }
    
    // TODO: Link allowedModifiers using stored IDs
    
    emit menuChanged();
    return true;
}

} // namespace vt2
