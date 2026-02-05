/**
 * @file check.hpp
 * @brief Check (bill/tab) data structure
 */

#pragma once

#include "core/types.hpp"
#include "data/order.hpp"
#include <vector>

namespace vt2 {

/**
 * @brief A check represents a customer's bill
 */
class Check {
public:
    Check() = default;
    
    CheckId id() const { return id_; }
    void setId(CheckId id) { id_ = id; }
    
    std::optional<TableId> tableId() const { return tableId_; }
    void setTableId(TableId id) { tableId_ = id; }
    
    const std::vector<Order>& orders() const { return orders_; }
    void addOrder(const Order& order);
    void removeOrder(OrderId id);
    
    Money subtotal() const;
    Money tax() const;
    Money total() const;
    
    bool isPaid() const { return paid_; }
    void setPaid(bool paid) { paid_ = paid; }
    
private:
    CheckId id_{0};
    std::optional<TableId> tableId_;
    std::vector<Order> orders_;
    bool paid_ = false;
    double taxRate_ = 0.08;  // 8% default
};

} // namespace vt2
