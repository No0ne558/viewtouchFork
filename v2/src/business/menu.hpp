// ViewTouch V2 - Menu Item System
// Modern C++/Qt6 reimplementation

#ifndef VT2_MENU_HPP
#define VT2_MENU_HPP

#include <QObject>
#include <QString>
#include <QList>
#include <QJsonObject>
#include <QMap>

namespace vt2 {

// Item Types (matches original ViewTouch)
enum class ItemType {
    Normal = 0,
    Modifier = 1,
    Method = 2,
    Substitute = 3,
    IncludedModifier = 4
};

// Item Family (categories)
enum class ItemFamily {
    Unknown = 0,
    Appetizer = 1,
    Soup = 2,
    Salad = 3,
    Entree = 4,
    Dessert = 5,
    Beverage = 6,
    Alcohol = 7,
    Beer = 8,
    Wine = 9,
    Coffee = 10,
    Side = 11,
    Bread = 12,
    Sandwich = 13,
    Pizza = 14,
    Seafood = 15,
    Steak = 16,
    Pasta = 17,
    Chicken = 18,
    Kids = 19,
    Combo = 20,
    Retail = 21,
    Merchandise = 22
};

// Sales Type (for reporting)
enum class SalesType {
    None = 0,
    Food = 1,
    Beverage = 2,
    Alcohol = 3,
    Beer = 4,
    Wine = 5,
    Merchandise = 6,
    Room = 7
};

//=============================================================================
// MenuItem - A single menu item
//=============================================================================
class MenuItem : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString name READ name WRITE setName NOTIFY nameChanged)
    Q_PROPERTY(QString shortName READ shortName WRITE setShortName NOTIFY nameChanged)
    Q_PROPERTY(int price READ price WRITE setPrice NOTIFY priceChanged)
    Q_PROPERTY(bool active READ isActive WRITE setActive NOTIFY activeChanged)

public:
    explicit MenuItem(QObject* parent = nullptr);
    MenuItem(const QString& name, int price, QObject* parent = nullptr);
    ~MenuItem() override = default;

    // Identification
    int id() const { return m_id; }
    void setId(int id) { m_id = id; }
    
    // Names
    QString name() const { return m_name; }
    void setName(const QString& name);
    
    QString shortName() const { return m_shortName.isEmpty() ? m_name : m_shortName; }
    void setShortName(const QString& name);
    
    QString zoneName() const { return m_zoneName; }
    void setZoneName(const QString& name) { m_zoneName = name; }
    
    QString printName() const { return m_printName.isEmpty() ? m_name : m_printName; }
    void setPrintName(const QString& name) { m_printName = name; }
    
    // Pricing
    int price() const { return m_price; }
    void setPrice(int price);
    
    int cost() const { return m_cost; }  // Cost to make item
    void setCost(int cost) { m_cost = cost; }
    
    int employeePrice() const { return m_employeePrice; }  // Employee discount price
    void setEmployeePrice(int price) { m_employeePrice = price; }
    
    // Type/Category
    ItemType itemType() const { return m_itemType; }
    void setItemType(ItemType type) { m_itemType = type; }
    
    ItemFamily family() const { return m_family; }
    void setFamily(ItemFamily fam) { m_family = fam; }
    
    SalesType salesType() const { return m_salesType; }
    void setSalesType(SalesType type) { m_salesType = type; }
    
    // Printer
    int printerId() const { return m_printerId; }
    void setPrinterId(int id) { m_printerId = id; }
    
    int callOrder() const { return m_callOrder; }
    void setCallOrder(int order) { m_callOrder = order; }
    
    // Status
    bool isActive() const { return m_active; }
    void setActive(bool active);
    
    bool isAlcohol() const {
        return m_salesType == SalesType::Alcohol ||
               m_salesType == SalesType::Beer ||
               m_salesType == SalesType::Wine;
    }
    
    // Modifiers
    QList<MenuItem*> allowedModifiers() const { return m_allowedModifiers; }
    void addAllowedModifier(MenuItem* mod);
    void removeAllowedModifier(MenuItem* mod);
    bool canHaveModifier(MenuItem* mod) const;
    
    // Serialization
    QJsonObject toJson() const;
    static MenuItem* fromJson(const QJsonObject& json, QObject* parent = nullptr);

signals:
    void nameChanged();
    void priceChanged();
    void activeChanged();
    void modifiersChanged();

private:
    int m_id = 0;
    QString m_name;
    QString m_shortName;
    QString m_zoneName;
    QString m_printName;
    
    int m_price = 0;        // Price in cents
    int m_cost = 0;         // Cost in cents
    int m_employeePrice = 0;
    
    ItemType m_itemType = ItemType::Normal;
    ItemFamily m_family = ItemFamily::Unknown;
    SalesType m_salesType = SalesType::Food;
    
    int m_printerId = 0;
    int m_callOrder = 0;
    
    bool m_active = true;
    
    QList<MenuItem*> m_allowedModifiers;
};

//=============================================================================
// MenuCategory - A category of menu items
//=============================================================================
class MenuCategory : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString name READ name WRITE setName NOTIFY nameChanged)

public:
    explicit MenuCategory(QObject* parent = nullptr);
    MenuCategory(const QString& name, QObject* parent = nullptr);
    ~MenuCategory() override = default;

    int id() const { return m_id; }
    void setId(int id) { m_id = id; }
    
    QString name() const { return m_name; }
    void setName(const QString& name);
    
    ItemFamily family() const { return m_family; }
    void setFamily(ItemFamily fam) { m_family = fam; }
    
    QList<MenuItem*> items() const { return m_items; }
    void addItem(MenuItem* item);
    void removeItem(MenuItem* item);
    MenuItem* findItem(int id);
    MenuItem* findItemByName(const QString& name);
    int itemCount() const { return m_items.size(); }
    
    // Serialization
    QJsonObject toJson() const;
    static MenuCategory* fromJson(const QJsonObject& json, QObject* parent = nullptr);

signals:
    void nameChanged();
    void itemsChanged();

private:
    int m_id = 0;
    QString m_name;
    ItemFamily m_family = ItemFamily::Unknown;
    QList<MenuItem*> m_items;
};

//=============================================================================
// MenuManager - Manages all menu items
//=============================================================================
class MenuManager : public QObject {
    Q_OBJECT

public:
    static MenuManager* instance();
    
    // Category management
    MenuCategory* createCategory(const QString& name);
    MenuCategory* findCategory(int id);
    MenuCategory* findCategoryByName(const QString& name);
    QList<MenuCategory*> categories() const { return m_categories; }
    void deleteCategory(MenuCategory* cat);
    
    // Item management
    MenuItem* createItem(const QString& name, int price);
    MenuItem* findItem(int id);
    MenuItem* findItemByName(const QString& name);
    QList<MenuItem*> allItems() const;
    QList<MenuItem*> activeItems() const;
    QList<MenuItem*> itemsByFamily(ItemFamily family) const;
    void deleteItem(MenuItem* item);
    
    // Modifiers
    QList<MenuItem*> modifiers() const;
    
    // Persistence
    bool saveToFile(const QString& path);
    bool loadFromFile(const QString& path);
    
    // Statistics
    int itemCount() const;
    int categoryCount() const { return m_categories.size(); }

signals:
    void categoryCreated(MenuCategory* cat);
    void categoryDeleted(MenuCategory* cat);
    void itemCreated(MenuItem* item);
    void itemDeleted(MenuItem* item);
    void menuChanged();

private:
    explicit MenuManager(QObject* parent = nullptr);
    static MenuManager* s_instance;
    
    QList<MenuCategory*> m_categories;
    int m_nextItemId = 1;
    int m_nextCategoryId = 1;
};

// Helper functions
QString itemFamilyToString(ItemFamily family);
QString salesTypeToString(SalesType type);

} // namespace vt2

#endif // VT2_MENU_HPP
