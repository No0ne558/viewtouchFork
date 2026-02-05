/**
 * @file order.hpp
 * @brief Order data structure
 */

#pragma once

#include "core/types.hpp"
#include "data/menu_item.hpp"
#include <vector>

namespace vt2 {

/**
 * @brief An order line item
 */
struct OrderItem {
    MenuItemId menuItemId;
    QString name;
    int quantity = 1;
    Money unitPrice;
    QString modifiers;
    
    Money total() const { return unitPrice * quantity; }
};

/**
 * @brief An order represents items ordered at one time
 */
class Order {
public:
    Order() = default;
    
    OrderId id() const { return id_; }
    void setId(OrderId id) { id_ = id; }
    
    const std::vector<OrderItem>& items() const { return items_; }
    void addItem(const OrderItem& item);
    void removeItem(size_t index);
    
    Money total() const;
    
    TimePoint createdAt() const { return createdAt_; }
    
private:
    OrderId id_{0};
    std::vector<OrderItem> items_;
    TimePoint createdAt_ = Clock::now();
};

} // namespace vt2
