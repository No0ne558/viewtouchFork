#pragma once

#include "core/menu.hh"
#include "core/money.hh"
#include "core/tax.hh"

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace vt::core {

struct Modifier {
    std::string itemId;
    std::string name;
    Money unitPrice;
    Qualifier qualifier = Qualifier::None;

    Money price() const { return qualifiedPrice(unitPrice, qualifier); }
    std::string displayName() const { return qualifierPrefix(qualifier) + name; }
    bool operator==(const Modifier &) const = default;
};

// One line on a check: an item (with its modifiers) or a free-text comment.
struct OrderLine {
    std::int64_t id = 0;
    std::string itemId;          // empty for comments
    std::string name;
    Money unitPrice;
    int quantity = 1;
    TaxClass taxClass = TaxClass::Food;
    Qualifier qualifier = Qualifier::None;
    std::vector<Modifier> modifiers;
    std::string printer;
    bool sent = false;           // sent to the kitchen; can no longer just be deleted
    bool voided = false;         // voided after sending (kept for the record)
    std::int64_t sentAt = 0;

    bool isComment() const { return itemId.empty(); }
    std::string displayName() const { return qualifierPrefix(qualifier) + name; }
    // (item + modifiers) x quantity; zero once voided.
    Money total() const;
    bool operator==(const OrderLine &) const = default;
};

enum class TenderKind { Cash, Card, Discount };

// A way to pay or reduce a check, configured in settings.
struct Tender {
    std::string id;
    std::string name;
    TenderKind kind = TenderKind::Cash;
    std::int64_t percentBp = 0;   // Discount: share of the items total (10000 = comp)

    bool operator==(const Tender &) const = default;
};

struct Payment {
    std::int64_t id = 0;
    std::string tenderId;
    std::string tenderName;
    TenderKind kind = TenderKind::Cash;
    Money amount;                 // Cash/Card
    std::int64_t percentBp = 0;   // Discount, applied to the current items total

    bool operator==(const Payment &) const = default;
};

enum class CheckType { DineIn, Takeout, Quick };
enum class CheckStatus { Open, Closed };

std::string toString(CheckType t);
CheckType checkTypeFromString(const std::string &s);
std::string toString(CheckStatus s);
CheckStatus checkStatusFromString(const std::string &s);
std::string toString(TenderKind k);
TenderKind tenderKindFromString(const std::string &s);

struct Totals {
    Money items;         // all non-voided lines
    Money discounts;     // positive amount taken off
    Money subtotal;      // items - discounts
    Money tax;
    std::map<TaxClass, Money> taxByClass;
    Money total;         // subtotal + tax
    Money paid;          // cash + card
    Money cashPaid;      // cash tendered
    Money balance;       // total - paid (negative = change owed)
    Money change;        // max(0, -balance), always given in cash
    Money cashNet() const { return cashPaid - change; }   // what stays in the drawer

    bool operator==(const Totals &) const = default;
};

struct Check {
    std::int64_t id = 0;              // serial number
    CheckType type = CheckType::DineIn;
    CheckStatus status = CheckStatus::Open;
    std::string label;                // table "T4", "Takeout 12", ...
    int guests = 1;
    std::string serverId;
    std::string serverName;
    std::int64_t openedAt = 0;        // epoch ms
    std::int64_t closedAt = 0;
    std::vector<OrderLine> lines;
    std::vector<Payment> payments;
    std::int64_t nextLineId = 1;
    std::int64_t nextPaymentId = 1;
    std::int64_t businessDay = 0;     // day the check was closed in
    std::int64_t drawerSession = 0;   // drawer that took its cash

    OrderLine *line(std::int64_t lineId);
    const OrderLine *line(std::int64_t lineId) const;
    // Last line that can take modifiers (an unsent, non-comment item).
    OrderLine *lastItemLine();

    OrderLine &addItem(const MenuItem &item, Qualifier q = Qualifier::None);
    // Attach a modifier to a line; false if the line is missing, sent, or a comment.
    bool addModifier(std::int64_t lineId, const MenuItem &modifier, Qualifier q = Qualifier::None);
    OrderLine &addComment(const std::string &text);
    // Unsent lines are removed; sent lines must be voided instead.
    bool removeLine(std::int64_t lineId);
    bool voidLine(std::int64_t lineId);
    // Marks every unsent line sent; returns how many.
    int sendAll(std::int64_t now);
    int unsentCount() const;

    // Split checks: remove a line (with its modifiers) / add one under a new id.
    std::optional<OrderLine> takeLine(std::int64_t lineId);
    OrderLine &adoptLine(OrderLine line);

    Payment &addPayment(const Tender &tender, Money amount);
    bool removePayment(std::int64_t paymentId);

    Totals totals(const TaxRates &rates) const;

    bool operator==(const Check &) const = default;
};

} // namespace vt::core
