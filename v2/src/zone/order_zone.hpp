// ViewTouch V2 - Order Zone
// Order display with pagination, seat filtering, item management

#pragma once

#include "zone/zone.hpp"
#include <QString>
#include <QList>

namespace vt {

// Forward declarations
class Check;
class SubCheck;

//=============================================================================
// OrderDisplayMode
//=============================================================================

enum class OrderDisplayMode {
    Normal,       // Standard order view
    Kitchen,      // Kitchen display format
    KitchenVideo, // Video kitchen display
    Seat,         // Show by seat
    Course,       // Show by course
    Category,     // Show by menu category
    Settlement    // Settlement/payment view
};

//=============================================================================
// OrderItemDisplay - Item for rendering
//=============================================================================

struct OrderItemDisplay {
    int itemId = 0;
    QString name;
    int quantity = 1;
    int price = 0;      // cents
    int seatNum = 0;
    int courseNum = 1;
    bool isVoid = false;
    bool isComp = false;
    bool isSelected = false;
    QList<QString> modifiers;
};

//=============================================================================
// OrderZone - Main Order Display
//=============================================================================

class OrderZone : public Zone {
    Q_OBJECT

public:
    OrderZone();
    ~OrderZone() override = default;

    // Zone type identification
    const char* typeName() const override { return "OrderZone"; }

    // Rendering
    void renderContent(Renderer& renderer, Terminal* term) override;

    // Touch handling
    int touch(Terminal* term, int tx, int ty) override;

    // Display mode
    void setDisplayMode(OrderDisplayMode mode);
    OrderDisplayMode displayMode() const { return m_displayMode; }

    // Filtering
    void setSeatFilter(int seat);  // 0 = all seats
    int seatFilter() const { return m_seatFilter; }

    void setCourseFilter(int course);  // 0 = all courses
    int courseFilter() const { return m_courseFilter; }

    // Pagination
    void setPage(int page);
    int currentPage() const { return m_currentPage; }
    int pageCount() const;
    void nextPage();
    void prevPage();

    // Items per page
    void setItemsPerPage(int count) { m_itemsPerPage = count; }
    int itemsPerPage() const { return m_itemsPerPage; }

    // Selection
    int selectedIndex() const { return m_selectedIndex; }
    void setSelectedIndex(int idx);
    void selectNext();
    void selectPrev();

    // Data binding
    void setItems(const QList<OrderItemDisplay>& items);
    void addItem(const OrderItemDisplay& item);
    void removeItem(int index);
    void clearItems();
    
    const QList<OrderItemDisplay>& items() const { return m_items; }

    // Totals
    int subtotal() const;
    int taxTotal() const { return m_taxTotal; }
    void setTaxTotal(int tax) { m_taxTotal = tax; }
    int grandTotal() const { return subtotal() + m_taxTotal; }

signals:
    void itemSelected(int itemId, int index);
    void itemTouched(int itemId);
    void pageChanged(int page);

private:
    QList<OrderItemDisplay> filteredItems() const;

    QList<OrderItemDisplay> m_items;
    OrderDisplayMode m_displayMode = OrderDisplayMode::Normal;
    
    int m_seatFilter = 0;
    int m_courseFilter = 0;
    int m_currentPage = 0;
    int m_itemsPerPage = 10;
    int m_selectedIndex = -1;
    int m_taxTotal = 0;
};

//=============================================================================
// SeatNavZone - Navigate between seats (PriorSeat/NextSeat)
//=============================================================================

class SeatNavZone : public Zone {
    Q_OBJECT

public:
    enum class NavDirection { Prior, Next };

    SeatNavZone();
    ~SeatNavZone() override = default;

    const char* typeName() const override { return "SeatNavZone"; }

    void renderContent(Renderer& renderer, Terminal* term) override;
    int touch(Terminal* term, int tx, int ty) override;

    NavDirection direction() const { return m_direction; }
    void setDirection(NavDirection dir) { m_direction = dir; }

    int currentSeat() const { return m_currentSeat; }
    void setCurrentSeat(int seat) { m_currentSeat = seat; }

    int maxSeats() const { return m_maxSeats; }
    void setMaxSeats(int max) { m_maxSeats = max; }

signals:
    void seatChanged(int newSeat);

private:
    NavDirection m_direction = NavDirection::Next;
    int m_currentSeat = 1;
    int m_maxSeats = 10;
};

//=============================================================================
// CheckNavZone - Navigate between checks
//=============================================================================

class CheckNavZone : public Zone {
    Q_OBJECT

public:
    enum class NavDirection { Prior, Next };

    CheckNavZone();
    ~CheckNavZone() override = default;

    const char* typeName() const override { return "CheckNavZone"; }

    void renderContent(Renderer& renderer, Terminal* term) override;
    int touch(Terminal* term, int tx, int ty) override;

    NavDirection direction() const { return m_direction; }
    void setDirection(NavDirection dir) { m_direction = dir; }

signals:
    void checkChanged(int direction);  // -1 = prev, +1 = next

private:
    NavDirection m_direction = NavDirection::Next;
};

//=============================================================================
// ItemModZone - Increase/Decrease item quantity
//=============================================================================

class ItemModZone : public Zone {
    Q_OBJECT

public:
    enum class ModType { Increase, Decrease };

    ItemModZone();
    ~ItemModZone() override = default;

    const char* typeName() const override { return "ItemModZone"; }

    void renderContent(Renderer& renderer, Terminal* term) override;
    int touch(Terminal* term, int tx, int ty) override;

    ModType modType() const { return m_modType; }
    void setModType(ModType type) { m_modType = type; }

signals:
    void modifyItem(int delta);  // +1 or -1

private:
    ModType m_modType = ModType::Increase;
};

} // namespace vt
