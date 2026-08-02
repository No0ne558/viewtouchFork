/*
 * snapshot.cc - Field-by-field comparison of two store snapshots.
 */

#include "snapshot.hh"

#include "check.hh"

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

} // namespace

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
