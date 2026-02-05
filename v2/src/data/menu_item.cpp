/**
 * @file menu_item.cpp
 */

#include "data/menu_item.hpp"

namespace vt2 {

MenuItem::MenuItem(const QString& name, Money price)
    : name_(name), price_(price) {
}

} // namespace vt2
