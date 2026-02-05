/**
 * @file menu_item.hpp
 * @brief Menu item data structure
 */

#pragma once

#include "core/types.hpp"
#include <QString>
#include <vector>

namespace vt2 {

/**
 * @brief A menu item that can be ordered
 */
class MenuItem {
public:
    MenuItem() = default;
    MenuItem(const QString& name, Money price);
    
    MenuItemId id() const { return id_; }
    void setId(MenuItemId id) { id_ = id; }
    
    QString name() const { return name_; }
    void setName(const QString& name) { name_ = name; }
    
    QString description() const { return description_; }
    void setDescription(const QString& desc) { description_ = desc; }
    
    Money price() const { return price_; }
    void setPrice(Money price) { price_ = price; }
    
    QString category() const { return category_; }
    void setCategory(const QString& cat) { category_ = cat; }
    
    bool available() const { return available_; }
    void setAvailable(bool avail) { available_ = avail; }
    
private:
    MenuItemId id_{0};
    QString name_;
    QString description_;
    Money price_;
    QString category_;
    bool available_ = true;
};

} // namespace vt2
