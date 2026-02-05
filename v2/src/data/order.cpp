/**
 * @file order.cpp
 */

#include "data/order.hpp"

namespace vt2 {

void Order::addItem(const OrderItem& item) {
    items_.push_back(item);
}

void Order::removeItem(size_t index) {
    if (index < items_.size()) {
        items_.erase(items_.begin() + static_cast<long>(index));
    }
}

Money Order::total() const {
    Money sum = Money::fromCents(0);
    for (const auto& item : items_) {
        sum = sum + item.total();
    }
    return sum;
}

} // namespace vt2
