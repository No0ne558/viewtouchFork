/**
 * @file check.cpp
 */

#include "data/check.hpp"

namespace vt2 {

void Check::addOrder(const Order& order) {
    orders_.push_back(order);
}

void Check::removeOrder(OrderId id) {
    orders_.erase(
        std::remove_if(orders_.begin(), orders_.end(),
            [id](const Order& o) { return o.id() == id; }),
        orders_.end()
    );
}

Money Check::subtotal() const {
    Money total = Money::fromCents(0);
    for (const auto& order : orders_) {
        total = total + order.total();
    }
    return total;
}

Money Check::tax() const {
    return Money::fromCents(static_cast<int64_t>(subtotal().cents() * taxRate_));
}

Money Check::total() const {
    return subtotal() + tax();
}

} // namespace vt2
