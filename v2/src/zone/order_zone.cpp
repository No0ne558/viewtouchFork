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
    
    uint8_t fontId = static_cast<uint8_t>(font());
    if (fontId == 0) fontId = static_cast<uint8_t>(FontId::Times14);
    uint8_t boldFontId = static_cast<uint8_t>(FontId::Times14B);
    
    int textColor = static_cast<int>(effectiveColor());
    int lineHeight = 22;
    int yPos = y() + 10;
    int leftMargin = x() + 10;
    int rightEdge = x() + w() - 10;
    
    // Header line
    renderer.drawText(QString("Qty  Item"), leftMargin, yPos, boldFontId, textColor);
    renderer.drawText(QString("Price"), rightEdge - 50, yPos, boldFontId, textColor);
    yPos += lineHeight;
    
    // Draw separator line
    renderer.drawLine(leftMargin, yPos - 5, rightEdge, yPos - 5, textColor, 1);
    
    // Draw order items
    for (int i = startIdx; i < endIdx; ++i) {
        const auto& item = filtered[i];
        
        // Highlight selected item
        bool selected = (i == m_selectedIndex);
        if (selected) {
            QRect selRect(leftMargin - 2, yPos - 2, w() - 16, lineHeight);
            renderer.fillRect(selRect, static_cast<uint8_t>(TextureId::LitSand));
        }
        
        // Draw quantity
        QString qty = QString("%1").arg(item.quantity);
        renderer.drawText(qty, leftMargin, yPos, fontId, textColor);
        
        // Draw name (with void/comp indicators)
        QString itemName = item.name;
        if (item.isVoid) itemName = "[VOID] " + itemName;
        else if (item.isComp) itemName = "[COMP] " + itemName;
        renderer.drawText(itemName, leftMargin + 30, yPos, fontId, textColor);
        
        // Draw price
        QString price = QString("$%1.%2")
            .arg(item.price / 100)
            .arg(item.price % 100, 2, 10, QChar('0'));
        renderer.drawText(price, rightEdge - 50, yPos, fontId, textColor);
        
        yPos += lineHeight;
        
        // Draw modifiers indented
        for (const auto& mod : item.modifiers) {
            renderer.drawText(QString("  - ") + mod, leftMargin + 40, yPos, fontId, textColor);
            yPos += lineHeight - 4;
        }
    }
    
    // Draw totals at bottom
    int totalY = y() + h() - 55;
    renderer.drawLine(leftMargin, totalY - 5, rightEdge, totalY - 5, textColor, 1);
    
    QString subtotalStr = QString("Subtotal:");
    renderer.drawText(subtotalStr, leftMargin, totalY, fontId, textColor);
    QString subtotalAmt = QString("$%1.%2")
        .arg(subtotal() / 100)
        .arg(subtotal() % 100, 2, 10, QChar('0'));
    renderer.drawText(subtotalAmt, rightEdge - 60, totalY, fontId, textColor);
    
    if (m_taxTotal > 0) {
        totalY += lineHeight;
        QString taxStr = QString("Tax:");
        renderer.drawText(taxStr, leftMargin, totalY, fontId, textColor);
        QString taxAmt = QString("$%1.%2")
            .arg(m_taxTotal / 100)
            .arg(m_taxTotal % 100, 2, 10, QChar('0'));
        renderer.drawText(taxAmt, rightEdge - 60, totalY, fontId, textColor);
    }
    
    // Grand total in bold
    totalY += lineHeight;
    renderer.drawText(QString("TOTAL:"), leftMargin, totalY, boldFontId, textColor);
    QString grandTotalAmt = QString("$%1.%2")
        .arg(grandTotal() / 100)
        .arg(grandTotal() % 100, 2, 10, QChar('0'));
    renderer.drawText(grandTotalAmt, rightEdge - 60, totalY, boldFontId, textColor);
    
    // Page indicator if needed
    if (pageCount() > 1) {
        QString pageStr = QString("Page %1 of %2").arg(m_currentPage + 1).arg(pageCount());
        renderer.drawTextCentered(pageStr, x() + w() / 2, y() + h() - 10, fontId, textColor);
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
    
    int cx = x() + w() / 2;
    int textColor = static_cast<int>(effectiveColor());
    uint8_t fontId = static_cast<uint8_t>(font());
    if (fontId == 0) fontId = static_cast<uint8_t>(FontId::Times_20);
    uint8_t smallFontId = static_cast<uint8_t>(FontId::Times14);
    
    QString label = (m_direction == NavDirection::Prior) ? "< Prev Seat" : "Next Seat >";
    renderer.drawTextCentered(label, cx, y() + h() / 3, fontId, textColor);
    
    QString seatStr = QString("Seat %1").arg(m_currentSeat);
    renderer.drawTextCentered(seatStr, cx, y() + h() * 2 / 3, smallFontId, textColor);
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
    
    int cx = x() + w() / 2;
    int textColor = static_cast<int>(effectiveColor());
    uint8_t fontId = static_cast<uint8_t>(font());
    if (fontId == 0) fontId = static_cast<uint8_t>(FontId::Times_20);
    
    QString label = (m_direction == NavDirection::Prior) ? "< Prev Check" : "Next Check >";
    renderer.drawTextCentered(label, cx, y() + h() / 2, fontId, textColor);
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
    
    int cx = x() + w() / 2;
    int textColor = static_cast<int>(effectiveColor());
    uint8_t bigFontId = static_cast<uint8_t>(FontId::Times34B);
    
    QString label = (m_modType == ModType::Increase) ? "+" : "−";  // Using proper minus sign
    renderer.drawTextCentered(label, cx, y() + h() / 2, bigFontId, textColor);
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
