/*
 * snapshot.cc - Field-by-field comparison of two store snapshots.
 */

#include "snapshot.hh"

#include "check.hh"
#include "drawer.hh"
#include "labor.hh"

#include <algorithm>
#include <map>
#include <sstream>
#include <string>

namespace vt::store {

namespace {

void AddIfDifferent(std::vector<Divergence> &out, const std::string &path,
                    int left, int right, const char *note = "")
{
    if (left == right)
        return;
    out.push_back(Divergence{path, std::to_string(left), std::to_string(right),
                             note});
}

void AddIfDifferent(std::vector<Divergence> &out, const std::string &path,
                    const std::string &left, const std::string &right,
                    const char *note = "")
{
    if (left == right)
        return;
    out.push_back(Divergence{path, left, right, note});
}

void CompareOrders(std::vector<Divergence> &out, const std::string &prefix,
                   const std::vector<OrderSnapshot> &left,
                   const std::vector<OrderSnapshot> &right)
{
    if (left.size() != right.size())
    {
        out.push_back(Divergence{prefix + ".order_count",
                                 std::to_string(left.size()),
                                 std::to_string(right.size()),
                                 "the order lists cannot be compared item by "
                                 "item until the counts agree"});
        return;
    }

    for (std::size_t i = 0; i < left.size(); ++i)
    {
        const std::string path = prefix + ".order[" + std::to_string(i) + "]";
        AddIfDifferent(out, path + ".item_name", left[i].item_name, right[i].item_name);
        AddIfDifferent(out, path + ".item_type", left[i].item_type, right[i].item_type);
        AddIfDifferent(out, path + ".item_family", left[i].item_family, right[i].item_family);
        AddIfDifferent(out, path + ".sales_type", left[i].sales_type, right[i].sales_type);
        AddIfDifferent(out, path + ".item_cost", left[i].item_cost, right[i].item_cost);
        AddIfDifferent(out, path + ".count", left[i].count, right[i].count);
        AddIfDifferent(out, path + ".seat", left[i].seat, right[i].seat);
        AddIfDifferent(out, path + ".qualifier", left[i].qualifier, right[i].qualifier);
        AddIfDifferent(out, path + ".call_order", left[i].call_order,
                       right[i].call_order,
                       "Order::Write never emitted call_order, so the legacy "
                       "side reports the constructor default no matter what "
                       "was in memory. Order::Add sorts modifiers by it, so "
                       "this is kitchen ticket order changing across a save.");
        AddIfDifferent(out, path + ".parent_index", left[i].parent_index,
                       right[i].parent_index,
                       "legacy infers the modifier tree from adjacency on "
                       "load; SQL stores parent_order_id");
    }
}

void ComparePayments(std::vector<Divergence> &out, const std::string &prefix,
                     const std::vector<PaymentSnapshot> &left,
                     const std::vector<PaymentSnapshot> &right)
{
    if (left.size() != right.size())
    {
        out.push_back(Divergence{prefix + ".payment_count",
                                 std::to_string(left.size()),
                                 std::to_string(right.size()), ""});
        return;
    }

    for (std::size_t i = 0; i < left.size(); ++i)
    {
        const std::string path = prefix + ".payment[" + std::to_string(i) + "]";
        AddIfDifferent(out, path + ".tender_type", left[i].tender_type, right[i].tender_type);
        AddIfDifferent(out, path + ".tender_id", left[i].tender_id, right[i].tender_id);
        AddIfDifferent(out, path + ".amount", left[i].amount, right[i].amount);
        AddIfDifferent(out, path + ".flags", left[i].flags, right[i].flags,
                       "Payment::Read sets TF_FINAL (128) unconditionally on "
                       "every payment it reads (check.cc:6474), so a payment "
                       "that was not final becomes final simply by surviving a "
                       "save and reload. The legacy side reports the mutated "
                       "value; SQL reports what was actually in memory.");
    }
}

void CompareCheck(std::vector<Divergence> &out, const CheckSnapshot &left,
                  const CheckSnapshot &right)
{
    const std::string prefix = "check[" + std::to_string(left.serial_number) + "]";

    AddIfDifferent(out, prefix + ".type", left.type, right.type);
    AddIfDifferent(out, prefix + ".flags", left.flags, right.flags);
    AddIfDifferent(out, prefix + ".guests", left.guests, right.guests);
    AddIfDifferent(out, prefix + ".label", left.label, right.label,
                   "the legacy writer maps ' ' and '~' onto '_' and the reader "
                   "maps '_' back to ' ', so strings are already damaged on disk");
    AddIfDifferent(out, prefix + ".comment", left.comment, right.comment);

    if (left.subchecks.size() != right.subchecks.size())
    {
        out.push_back(Divergence{prefix + ".subcheck_count",
                                 std::to_string(left.subchecks.size()),
                                 std::to_string(right.subchecks.size()), ""});
        return;
    }

    for (std::size_t i = 0; i < left.subchecks.size(); ++i)
    {
        const std::string path = prefix + ".subcheck[" + std::to_string(i) + "]";
        AddIfDifferent(out, path + ".status", left.subchecks[i].status,
                       right.subchecks[i].status);
        AddIfDifferent(out, path + ".check_type", left.subchecks[i].check_type,
                       right.subchecks[i].check_type);
        CompareOrders(out, path, left.subchecks[i].orders, right.subchecks[i].orders);
        ComparePayments(out, path, left.subchecks[i].payments,
                        right.subchecks[i].payments);
    }
}

void CompareLaborPeriod(std::vector<Divergence> &out,
                        const LaborPeriodSnapshot &left,
                        const LaborPeriodSnapshot &right)
{
    const std::string prefix = "labor[" + std::to_string(left.serial_number) + "]";

    // A period open on one side and closed on the other is a real difference:
    // LaborDB::CurrentPeriod picks the open one, so two answers means two
    // different ideas of which period a clock-in belongs to.
    AddIfDifferent(out, prefix + ".closed", left.has_end ? 1 : 0,
                   right.has_end ? 1 : 0);

    if (left.entries.size() != right.entries.size())
    {
        out.push_back(Divergence{prefix + ".entry_count",
                                 std::to_string(left.entries.size()),
                                 std::to_string(right.entries.size()), ""});
        return;
    }

    for (std::size_t i = 0; i < left.entries.size(); ++i)
    {
        const std::string path = prefix + ".entry[" + std::to_string(i) + "]";
        AddIfDifferent(out, path + ".user_id", left.entries[i].user_id,
                       right.entries[i].user_id);
        AddIfDifferent(out, path + ".job", left.entries[i].job,
                       right.entries[i].job);
        AddIfDifferent(out, path + ".pay_rate", left.entries[i].pay_rate,
                       right.entries[i].pay_rate);
        AddIfDifferent(out, path + ".pay_amount", left.entries[i].pay_amount,
                       right.entries[i].pay_amount);
        AddIfDifferent(out, path + ".tips", left.entries[i].tips,
                       right.entries[i].tips);
        AddIfDifferent(out, path + ".overtime", left.entries[i].overtime,
                       right.entries[i].overtime,
                       "WorkEntry::Write never emitted overtime, so the legacy "
                       "side always reports zero. Worse, the value SQL reports "
                       "is only non-zero if someone happened to open a labor "
                       "report first: LaborPeriod::WorkReport assigns it as a "
                       "side effect of drawing the line. Neither side is "
                       "authoritative -- see MinutesOvertime for the real "
                       "figure.");
        AddIfDifferent(out, path + ".end_shift", left.entries[i].end_shift,
                       right.entries[i].end_shift);
        AddIfDifferent(out, path + ".started", left.entries[i].has_start ? 1 : 0,
                       right.entries[i].has_start ? 1 : 0);
        // Still on the clock, or not. The one that decides whether a shift is
        // billable yet.
        AddIfDifferent(out, path + ".ended", left.entries[i].has_end ? 1 : 0,
                       right.entries[i].has_end ? 1 : 0);
    }
}

void CompareDrawer(std::vector<Divergence> &out, const DrawerSnapshot &left,
                   const DrawerSnapshot &right)
{
    const std::string prefix = "drawer[" + std::to_string(left.serial_number) + "]";

    AddIfDifferent(out, prefix + ".host", left.host, right.host);
    AddIfDifferent(out, prefix + ".position", left.position, right.position);
    AddIfDifferent(out, prefix + ".number", left.number, right.number);
    AddIfDifferent(out, prefix + ".owner_id", left.owner_id, right.owner_id);
    AddIfDifferent(out, prefix + ".puller_id", left.puller_id, right.puller_id);
    AddIfDifferent(out, prefix + ".media_balanced", left.media_balanced,
                   right.media_balanced);

    // Set-or-not rather than the values, because these three are what
    // GetStatus() reads. A drawer that is open on one side and pulled on the
    // other is the difference worth reporting.
    AddIfDifferent(out, prefix + ".started", left.has_start ? 1 : 0,
                   right.has_start ? 1 : 0);
    AddIfDifferent(out, prefix + ".pulled", left.has_pull ? 1 : 0,
                   right.has_pull ? 1 : 0);
    AddIfDifferent(out, prefix + ".balanced", left.has_balance ? 1 : 0,
                   right.has_balance ? 1 : 0);

    if (left.payments.size() != right.payments.size())
    {
        out.push_back(Divergence{prefix + ".payment_count",
                                 std::to_string(left.payments.size()),
                                 std::to_string(right.payments.size()), ""});
    }
    else
    {
        for (std::size_t i = 0; i < left.payments.size(); ++i)
        {
            const std::string path = prefix + ".payment[" + std::to_string(i) + "]";
            AddIfDifferent(out, path + ".tender_type", left.payments[i].tender_type,
                           right.payments[i].tender_type);
            AddIfDifferent(out, path + ".amount", left.payments[i].amount,
                           right.payments[i].amount);
            AddIfDifferent(out, path + ".user_id", left.payments[i].user_id,
                           right.payments[i].user_id);
            AddIfDifferent(out, path + ".target_id", left.payments[i].target_id,
                           right.payments[i].target_id);
        }
    }

    if (left.balances.size() != right.balances.size())
    {
        out.push_back(Divergence{
            prefix + ".balance_count",
            std::to_string(left.balances.size()),
            std::to_string(right.balances.size()),
            "Drawer::Write emits a balance row only when `entered` is non-zero, "
            "so the legacy side cannot represent a tender that was counted and "
            "came to nothing -- a real outcome, and different from never having "
            "counted it. Expect the SQL side to hold more rows."});
        return;
    }

    for (std::size_t i = 0; i < left.balances.size(); ++i)
    {
        const std::string path = prefix + ".balance[" + std::to_string(i) + "]";
        AddIfDifferent(out, path + ".tender_type", left.balances[i].tender_type,
                       right.balances[i].tender_type);
        AddIfDifferent(out, path + ".tender_id", left.balances[i].tender_id,
                       right.balances[i].tender_id);
        AddIfDifferent(out, path + ".entered", left.balances[i].entered,
                       right.balances[i].entered);
    }
}

} // namespace

LaborPeriodSnapshot SnapshotOf(LaborPeriod &period)
{
    LaborPeriodSnapshot out;
    out.serial_number = period.serial_number;
    out.has_end = period.end_time.IsSet();

    for (WorkEntry *entry = period.WorkList(); entry != nullptr;
         entry = entry->next)
    {
        WorkEntrySnapshot snap;
        snap.user_id = entry->user_id;
        snap.job = entry->job;
        snap.pay_rate = entry->pay_rate;
        snap.pay_amount = entry->pay_amount;
        snap.tips = entry->tips;
        snap.overtime = entry->overtime;
        snap.end_shift = entry->end_shift;
        snap.has_start = entry->start.IsSet();
        snap.has_end = entry->end.IsSet();
        out.entries.push_back(snap);
    }
    return out;
}

DrawerSnapshot SnapshotOf(Drawer &drawer)
{
    DrawerSnapshot out;
    out.serial_number = drawer.serial_number;
    out.host = (drawer.host.Value() != nullptr) ? drawer.host.Value() : "";
    out.position = drawer.position;
    out.number = drawer.number;
    out.owner_id = drawer.owner_id;
    out.puller_id = drawer.puller_id;
    out.media_balanced = drawer.media_balanced;
    out.has_start = drawer.start_time.IsSet();
    out.has_pull = drawer.pull_time.IsSet();
    out.has_balance = drawer.balance_time.IsSet();

    for (const DrawerPayment *payment = drawer.PaymentList(); payment != nullptr;
         payment = payment->next)
    {
        DrawerPaymentSnapshot snap;
        snap.tender_type = payment->tender_type;
        snap.amount = payment->amount;
        snap.user_id = payment->user_id;
        snap.target_id = payment->target_id;
        out.payments.push_back(snap);
    }

    for (const DrawerBalance *balance = drawer.BalanceList(); balance != nullptr;
         balance = balance->next)
    {
        DrawerBalanceSnapshot snap;
        snap.tender_type = balance->tender_type;
        snap.tender_id = balance->tender_id;
        snap.entered = balance->entered;
        out.balances.push_back(snap);
    }
    return out;
}

CheckSnapshot SnapshotOf(Check &check)
{
    CheckSnapshot out;
    out.serial_number = check.serial_number;
    out.type = check.type;
    out.flags = check.flags;
    out.guests = check.guests;
    out.label = (check.label.Value() != nullptr) ? check.label.Value() : "";
    out.comment = (check.comment.Value() != nullptr) ? check.comment.Value() : "";

    int seq = 0;
    for (SubCheck *sub = check.SubList(); sub != nullptr; sub = sub->next, ++seq)
    {
        SubCheckSnapshot sub_out;
        sub_out.seq = seq;
        sub_out.status = sub->status;
        sub_out.check_type = sub->check_type;

        // Flatten the tree the same way both backends have to report it: roots
        // in list order, each root's modifiers immediately after it, with
        // parent_index pointing back at the root's position in this flattened
        // list. An index rather than an id, because ids are backend-specific
        // and would diverge for reasons that mean nothing.
        for (const Order *order = sub->OrderList(); order != nullptr;
             order = order->next)
        {
            const int parent_position = static_cast<int>(sub_out.orders.size());

            OrderSnapshot root;
            root.item_name = (order->item_name.Value() != nullptr)
                                 ? order->item_name.Value() : "";
            root.item_type = order->item_type;
            root.item_family = order->item_family;
            root.sales_type = order->sales_type;
            root.item_cost = order->item_cost;
            root.count = order->count;
            root.seat = order->seat;
            root.qualifier = order->qualifier;
            root.call_order = order->call_order;
            root.parent_index = -1;
            sub_out.orders.push_back(std::move(root));

            for (const Order *mod = order->modifier_list; mod != nullptr;
                 mod = mod->next)
            {
                OrderSnapshot child;
                child.item_name = (mod->item_name.Value() != nullptr)
                                      ? mod->item_name.Value() : "";
                child.item_type = mod->item_type;
                child.item_family = mod->item_family;
                child.sales_type = mod->sales_type;
                child.item_cost = mod->item_cost;
                child.count = mod->count;
                child.seat = mod->seat;
                child.qualifier = mod->qualifier;
                child.call_order = mod->call_order;
                child.parent_index = parent_position;
                sub_out.orders.push_back(std::move(child));
            }
        }

        for (const Payment *payment = sub->PaymentList(); payment != nullptr;
             payment = payment->next)
        {
            PaymentSnapshot pay;
            pay.tender_type = payment->tender_type;
            pay.tender_id = payment->tender_id;
            pay.amount = payment->amount;
            pay.flags = payment->flags;
            sub_out.payments.push_back(pay);
        }

        out.subchecks.push_back(std::move(sub_out));
    }
    return out;
}

std::vector<Divergence> Diff(const StoreSnapshot &left, const StoreSnapshot &right)
{
    std::vector<Divergence> found;

    // Matched by serial number rather than by position. A check missing from
    // one side would otherwise shift every subsequent comparison and bury the
    // one real difference under dozens of spurious ones.
    std::map<int, const CheckSnapshot *> right_by_serial;
    for (const CheckSnapshot &check : right.checks)
        right_by_serial[check.serial_number] = &check;

    for (const CheckSnapshot &check : left.checks)
    {
        const auto it = right_by_serial.find(check.serial_number);
        if (it == right_by_serial.end())
        {
            found.push_back(Divergence{
                "check[" + std::to_string(check.serial_number) + "]", "present",
                "absent", "this check exists only on the left backend"});
            continue;
        }
        CompareCheck(found, check, *it->second);
        right_by_serial.erase(it);
    }

    for (const auto &[serial, check] : right_by_serial)
    {
        (void)check;
        found.push_back(Divergence{"check[" + std::to_string(serial) + "]",
                                   "absent", "present",
                                   "this check exists only on the right backend"});
    }

    // Labor periods, matched by serial. Included from the day they became
    // dual-written, deliberately: the drawer hole below is what happens when
    // that step is skipped.
    std::map<int, const LaborPeriodSnapshot *> right_labor;
    for (const LaborPeriodSnapshot &period : right.labor)
        right_labor[period.serial_number] = &period;

    for (const LaborPeriodSnapshot &period : left.labor)
    {
        const auto it = right_labor.find(period.serial_number);
        if (it == right_labor.end())
        {
            found.push_back(Divergence{
                "labor[" + std::to_string(period.serial_number) + "]", "present",
                "absent", "this labor period exists only on the left backend"});
            continue;
        }
        CompareLaborPeriod(found, period, *it->second);
        right_labor.erase(it);
    }

    for (const auto &[serial, period] : right_labor)
    {
        (void)period;
        found.push_back(Divergence{"labor[" + std::to_string(serial) + "]",
                                   "absent", "present",
                                   "this labor period exists only on the right backend"});
    }

    // Drawers, matched the same way. Omitting these was a real hole while they
    // were being written but not compared: the report would say "no divergence"
    // over half the money, which is worse than no coverage because it produces
    // confidence rather than the absence of it.
    std::map<int, const DrawerSnapshot *> right_drawers;
    for (const DrawerSnapshot &drawer : right.drawers)
        right_drawers[drawer.serial_number] = &drawer;

    for (const DrawerSnapshot &drawer : left.drawers)
    {
        const auto it = right_drawers.find(drawer.serial_number);
        if (it == right_drawers.end())
        {
            found.push_back(Divergence{
                "drawer[" + std::to_string(drawer.serial_number) + "]", "present",
                "absent", "this drawer exists only on the left backend"});
            continue;
        }
        CompareDrawer(found, drawer, *it->second);
        right_drawers.erase(it);
    }

    for (const auto &[serial, drawer] : right_drawers)
    {
        (void)drawer;
        found.push_back(Divergence{"drawer[" + std::to_string(serial) + "]",
                                   "absent", "present",
                                   "this drawer exists only on the right backend"});
    }

    return found;
}

std::string DescribeDivergence(const StoreSnapshot &left,
                               const StoreSnapshot &right,
                               const std::vector<Divergence> &found)
{
    if (found.empty())
        return {};

    std::ostringstream out;
    out << found.size() << " divergence(s) between '" << left.backend
        << "' (left) and '" << right.backend << "' (right):\n";
    for (const Divergence &d : found)
    {
        out << "  " << d.path << ": " << left.backend << "=" << d.left << ", "
            << right.backend << "=" << d.right;
        if (!d.note.empty())
            out << "\n      " << d.note;
        out << "\n";
    }
    return out.str();
}

} // namespace vt::store
