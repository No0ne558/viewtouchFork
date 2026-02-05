/**
 * @file types.hpp
 * @brief Core type definitions for ViewTouch V2
 * 
 * This file contains fundamental types, enums, and type aliases used
 * throughout the ViewTouch V2 application.
 */

#pragma once

#include <cstdint>
#include <string>
#include <optional>
#include <variant>
#include <expected>
#include <chrono>
#include <QColor>
#include <QString>

namespace vt2 {

// ============================================================================
// Result Type - Modern error handling
// ============================================================================

template<typename T, typename E = std::string>
using Result = std::expected<T, E>;

// ============================================================================
// ID Types - Strong typing for different entity IDs
// ============================================================================

struct ZoneId { std::uint32_t value; auto operator<=>(const ZoneId&) const = default; };
struct PageId { std::uint32_t value; auto operator<=>(const PageId&) const = default; };
struct CheckId { std::uint32_t value; auto operator<=>(const CheckId&) const = default; };
struct OrderId { std::uint32_t value; auto operator<=>(const OrderId&) const = default; };
struct EmployeeId { std::uint32_t value; auto operator<=>(const EmployeeId&) const = default; };
struct MenuItemId { std::uint32_t value; auto operator<=>(const MenuItemId&) const = default; };
struct TableId { std::uint32_t value; auto operator<=>(const TableId&) const = default; };

// ============================================================================
// Time Aliases
// ============================================================================

using Clock = std::chrono::system_clock;
using TimePoint = std::chrono::time_point<Clock>;
using Duration = std::chrono::milliseconds;

// ============================================================================
// Money Type - Avoid floating point for currency
// ============================================================================

class Money {
public:
    constexpr Money() : cents_(0) {}
    constexpr explicit Money(std::int64_t cents) : cents_(cents) {}
    
    static constexpr Money fromCents(std::int64_t cents) { return Money(cents); }
    static constexpr Money fromDollars(double dollars) { 
        return Money(static_cast<std::int64_t>(dollars * 100.0 + 0.5)); 
    }
    
    [[nodiscard]] constexpr std::int64_t cents() const { return cents_; }
    [[nodiscard]] constexpr double dollars() const { return static_cast<double>(cents_) / 100.0; }
    
    [[nodiscard]] QString toString() const {
        return QString("$%1.%2")
            .arg(cents_ / 100)
            .arg(qAbs(cents_ % 100), 2, 10, QChar('0'));
    }
    
    constexpr Money operator+(Money other) const { return Money(cents_ + other.cents_); }
    constexpr Money operator-(Money other) const { return Money(cents_ - other.cents_); }
    constexpr Money operator*(int factor) const { return Money(cents_ * factor); }
    constexpr auto operator<=>(const Money&) const = default;
    
private:
    std::int64_t cents_;
};

// ============================================================================
// Zone Types - Maps to original ViewTouch zone types
// ============================================================================

enum class ZoneType {
    // Basic zones
    Button,
    Toggle,
    Label,
    
    // Input zones
    TextEntry,
    NumberEntry,
    KeyboardEntry,
    
    // Display zones  
    Order,
    CheckList,
    Table,
    Report,
    Chart,
    
    // Transaction zones
    Payment,
    Drawer,
    
    // Navigation
    PageJump,
    
    // System
    Login,
    Settings,
    Hardware,
    
    // Custom
    Custom
};

// ============================================================================
// Zone Behaviors - How zones respond to input
// ============================================================================

enum class ZoneBehavior {
    Standard,       // Normal click behavior
    Toggle,         // Toggle on/off state
    Radio,          // Part of radio group (only one active)
    Touch,          // Respond to touch only
    Keyboard,       // Respond to keyboard only
    Both            // Respond to both touch and keyboard
};

// ============================================================================
// Page Types
// ============================================================================

enum class PageType {
    Index,          // Main menu/index page
    Table,          // Table selection page
    Order,          // Order entry page
    Payment,        // Payment processing page
    Report,         // Report display page
    Settings,       // Settings/configuration page
    Manager,        // Manager functions page
    Custom          // User-defined page
};

// ============================================================================
// Colors - Named colors matching ViewTouch original
// ============================================================================

namespace colors {
    inline constexpr QColor Black{0, 0, 0};
    inline constexpr QColor White{255, 255, 255};
    inline constexpr QColor Red{220, 53, 69};
    inline constexpr QColor Green{40, 167, 69};
    inline constexpr QColor Blue{0, 123, 255};
    inline constexpr QColor Yellow{255, 193, 7};
    inline constexpr QColor Orange{253, 126, 20};
    inline constexpr QColor Purple{111, 66, 193};
    inline constexpr QColor Teal{32, 201, 151};
    inline constexpr QColor Gray{108, 117, 125};
    inline constexpr QColor DarkGray{52, 58, 64};
    inline constexpr QColor LightGray{206, 212, 218};
    
    // ViewTouch classic colors
    inline constexpr QColor VTBlue{51, 102, 153};
    inline constexpr QColor VTGreen{76, 153, 76};
    inline constexpr QColor VTRed{178, 51, 51};
    inline constexpr QColor VTYellow{204, 178, 51};
    inline constexpr QColor VTBackground{45, 45, 45};
}

// ============================================================================
// Font Definitions
// ============================================================================

enum class FontSize {
    Tiny = 8,
    Small = 10,
    Normal = 12,
    Medium = 14,
    Large = 18,
    XLarge = 24,
    Huge = 32,
    Giant = 48
};

enum class FontWeight {
    Light,
    Normal,
    Medium,
    Bold,
    Heavy
};

// ============================================================================
// Alignment
// ============================================================================

enum class HAlign {
    Left,
    Center,
    Right
};

enum class VAlign {
    Top,
    Center,
    Bottom
};

// ============================================================================
// Touch/Input Events
// ============================================================================

enum class TouchType {
    Press,
    Release,
    Move,
    LongPress,
    Swipe
};

enum class SwipeDirection {
    Left,
    Right,
    Up,
    Down
};

// ============================================================================
// Employee/Permission Types
// ============================================================================

enum class EmployeeRole {
    None,
    Server,
    Bartender,
    Cashier,
    Host,
    Manager,
    Admin
};

enum class Permission {
    VoidItem,
    VoidCheck,
    Discount,
    Comps,
    OpenDrawer,
    CloseDay,
    EditMenu,
    EditEmployees,
    ViewReports,
    SystemSettings
};

// ============================================================================
// Payment Types
// ============================================================================

enum class PaymentType {
    Cash,
    CreditCard,
    DebitCard,
    GiftCard,
    Check,
    Tab,
    HouseAccount,
    Split
};

enum class PaymentStatus {
    Pending,
    Processing,
    Approved,
    Declined,
    Voided,
    Refunded
};

} // namespace vt2
