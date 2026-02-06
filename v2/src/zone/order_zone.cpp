// ViewTouch V2 - Order Zone Implementation

#include "zone/order_zone.hpp"
#include "render/renderer.hpp"
#include "terminal/terminal.hpp"

namespace vt {

//=============================================================================
// OrderZone Implementation
//=============================================================================

OrderZone::OrderZone()
    : Zone()
{
    setZoneType(ZoneType::OrderEntry);
    setName("Order");
}

void OrderZone::setDisplayMode(OrderDisplayMode mode) {
    if (m_displayMode != mode) {
        m_displayMode = mode;
        m_currentPage = 0;
        m_selectedIndex = -1;
        setNeedsUpdate(true);
    }
}

void OrderZone::setSeatFilter(int seat) {
    if (m_seatFilter != seat) {
        m_seatFilter = seat;
        m_currentPage = 0;
        setNeedsUpdate(true);
    }
}

void OrderZone::setCourseFilter(int course) {
    if (m_courseFilter != course) {
        m_courseFilter = course;
        m_currentPage = 0;
        setNeedsUpdate(true);
    }
}

void OrderZone::setPage(int page) {
    int maxPage = pageCount() - 1;
    if (maxPage < 0) maxPage = 0;
    
    if (page < 0) page = 0;
    if (page > maxPage) page = maxPage;
    
    if (m_currentPage != page) {
        m_currentPage = page;
        setNeedsUpdate(true);
        emit pageChanged(page);
    }
}

int OrderZone::pageCount() const {
    auto filtered = filteredItems();
    if (filtered.isEmpty() || m_itemsPerPage <= 0) return 1;
    return (filtered.size() + m_itemsPerPage - 1) / m_itemsPerPage;
}

void OrderZone::nextPage() {
    setPage(m_currentPage + 1);
}

void OrderZone::prevPage() {
    setPage(m_currentPage - 1);
}

void OrderZone::setSelectedIndex(int idx) {
    auto filtered = filteredItems();
    if (idx < -1) idx = -1;
    if (idx >= filtered.size()) idx = filtered.size() - 1;
    
    if (m_selectedIndex != idx) {
        m_selectedIndex = idx;
        setNeedsUpdate(true);
        
        if (idx >= 0 && idx < filtered.size()) {
            emit itemSelected(filtered[idx].itemId, idx);
        }
    }
}

void OrderZone::selectNext() {
    setSelectedIndex(m_selectedIndex + 1);
}

void OrderZone::selectPrev() {
    setSelectedIndex(m_selectedIndex - 1);
}

void OrderZone::setItems(const QList<OrderItemDisplay>& items) {
    m_items = items;
    m_currentPage = 0;
    m_selectedIndex = -1;
    setNeedsUpdate(true);
}

void OrderZone::addItem(const OrderItemDisplay& item) {
    m_items.append(item);
    setNeedsUpdate(true);
}

void OrderZone::removeItem(int index) {
    if (index >= 0 && index < m_items.size()) {
        m_items.removeAt(index);
        if (m_selectedIndex >= m_items.size()) {
            m_selectedIndex = m_items.size() - 1;
        }
        setNeedsUpdate(true);
    }
}

void OrderZone::clearItems() {
    m_items.clear();
    m_currentPage = 0;
    m_selectedIndex = -1;
    setNeedsUpdate(true);
}

int OrderZone::subtotal() const {
    int total = 0;
    for (const auto& item : m_items) {
        if (!item.isVoid) {
            total += item.price * item.quantity;
        }
    }
    return total;
}

QList<OrderItemDisplay> OrderZone::filteredItems() const {
    QList<OrderItemDisplay> result;
    
    for (const auto& item : m_items) {
        if (m_seatFilter > 0 && item.seatNum != m_seatFilter) continue;
        if (m_courseFilter > 0 && item.courseNum != m_courseFilter) continue;
        result.append(item);
    }
    
    return result;
}

void OrderZone::renderContent(Renderer& renderer, Terminal* term) {
    Q_UNUSED(term);
    
    auto filtered = filteredItems();
    int startIdx = m_currentPage * m_itemsPerPage;
    int endIdx = qMin(startIdx + m_itemsPerPage, filtered.size());
    
    int lineHeight = 20;
    int yPos = y() + 5;
    
    for (int i = startIdx; i < endIdx; ++i) {
        const auto& item = filtered[i];
        
        // Highlight selected item
        bool selected = (i == m_selectedIndex);
        Q_UNUSED(selected);  // Used for future highlighting
        
        // Draw quantity
        QString qty = QString("%1").arg(item.quantity);
        renderer.drawText(qty, x() + 5, yPos, static_cast<uint8_t>(font()), 0);
        
        // Draw name (with void/comp indicators)
        QString itemName = item.name;
        if (item.isVoid) itemName = "VOID " + itemName;
        if (item.isComp) itemName = "COMP " + itemName;
        renderer.drawText(itemName, x() + 30, yPos, static_cast<uint8_t>(font()), 0);
        
        // Draw price
        QString price = QString("$%1.%2")
            .arg(item.price / 100)
            .arg(item.price % 100, 2, 10, QChar('0'));
        renderer.drawText(price, x() + w() - 60, yPos, static_cast<uint8_t>(font()), 0);
        
        // Draw modifiers
        for (const auto& mod : item.modifiers) {
            yPos += lineHeight - 5;
            renderer.drawText(QString("  ") + mod, x() + 40, yPos, static_cast<uint8_t>(font()), 0);
        }
        
        yPos += lineHeight;
    }
    
    // Draw totals at bottom
    int totalY = y() + h() - 40;
    
    QString subtotalStr = QString("Subtotal: $%1.%2")
        .arg(subtotal() / 100)
        .arg(subtotal() % 100, 2, 10, QChar('0'));
    renderer.drawText(subtotalStr, x() + w() - 120, totalY, static_cast<uint8_t>(font()), 0);
    
    if (m_taxTotal > 0) {
        QString taxStr = QString("Tax: $%1.%2")
            .arg(m_taxTotal / 100)
            .arg(m_taxTotal % 100, 2, 10, QChar('0'));
        renderer.drawText(taxStr, x() + w() - 120, totalY + 15, static_cast<uint8_t>(font()), 0);
    }
    
    // Page indicator
    if (pageCount() > 1) {
        QString pageStr = QString("Page %1/%2").arg(m_currentPage + 1).arg(pageCount());
        renderer.drawText(pageStr, x() + 5, y() + h() - 15, static_cast<uint8_t>(font()), 0);
    }
}

int OrderZone::touch(Terminal* term, int tx, int ty) {
    Q_UNUSED(term);
    
    // Calculate which item was touched
    int relY = ty - y() - 5;
    int lineHeight = 20;
    int touchedLine = relY / lineHeight;
    int actualIdx = m_currentPage * m_itemsPerPage + touchedLine;
    
    auto filtered = filteredItems();
    if (actualIdx >= 0 && actualIdx < filtered.size()) {
        setSelectedIndex(actualIdx);
        emit itemTouched(filtered[actualIdx].itemId);
    }
    
    return 0;
}

//=============================================================================
// SeatNavZone Implementation
//=============================================================================

SeatNavZone::SeatNavZone()
    : Zone()
{
    setZoneType(ZoneType::OrderFlow);  // Navigation within orders
    setName("Seat Nav");
}

void SeatNavZone::renderContent(Renderer& renderer, Terminal* term) {
    Q_UNUSED(term);
    
    QString label = (m_direction == NavDirection::Prior) ? "< Prev Seat" : "Next Seat >";
    renderer.drawText(label, x() + 10, y() + h()/2, static_cast<uint8_t>(font()), 0);
    
    QString seatStr = QString("Seat %1").arg(m_currentSeat);
    renderer.drawText(seatStr, x() + w()/2 - 20, y() + h() - 15, static_cast<uint8_t>(font()), 0);
}

int SeatNavZone::touch(Terminal* term, int tx, int ty) {
    Q_UNUSED(term);
    Q_UNUSED(tx);
    Q_UNUSED(ty);
    
    int newSeat = m_currentSeat;
    
    if (m_direction == NavDirection::Prior) {
        newSeat--;
        if (newSeat < 1) newSeat = m_maxSeats;
    } else {
        newSeat++;
        if (newSeat > m_maxSeats) newSeat = 1;
    }
    
    if (newSeat != m_currentSeat) {
        m_currentSeat = newSeat;
        setNeedsUpdate(true);
        emit seatChanged(newSeat);
    }
    
    return 0;
}

//=============================================================================
// CheckNavZone Implementation
//=============================================================================

CheckNavZone::CheckNavZone()
    : Zone()
{
    setZoneType(ZoneType::CheckDisplay);  // Check navigation
    setName("Check Nav");
}

void CheckNavZone::renderContent(Renderer& renderer, Terminal* term) {
    Q_UNUSED(term);
    
    QString label = (m_direction == NavDirection::Prior) ? "< Prev Check" : "Next Check >";
    renderer.drawText(label, x() + 10, y() + h()/2, static_cast<uint8_t>(font()), 0);
}

int CheckNavZone::touch(Terminal* term, int tx, int ty) {
    Q_UNUSED(term);
    Q_UNUSED(tx);
    Q_UNUSED(ty);
    
    int delta = (m_direction == NavDirection::Prior) ? -1 : 1;
    emit checkChanged(delta);
    
    return 0;
}

//=============================================================================
// ItemModZone Implementation
//=============================================================================

ItemModZone::ItemModZone()
    : Zone()
{
    setZoneType(ZoneType::OrderAdd);  // Using OrderAdd for increase functionality
    setName("Item Mod");
}

void ItemModZone::renderContent(Renderer& renderer, Terminal* term) {
    Q_UNUSED(term);
    
    QString label = (m_modType == ModType::Increase) ? "+" : "-";
    renderer.drawText(label, x() + w()/2 - 5, y() + h()/2, static_cast<uint8_t>(font()), 0);
}

int ItemModZone::touch(Terminal* term, int tx, int ty) {
    Q_UNUSED(term);
    Q_UNUSED(tx);
    Q_UNUSED(ty);
    
    int delta = (m_modType == ModType::Increase) ? 1 : -1;
    emit modifyItem(delta);
    
    return 0;
}

} // namespace vt
