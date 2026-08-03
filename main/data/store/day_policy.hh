/*
 * day_policy.hh - the tax and rounding policy a business day traded under.
 *
 * SubCheck::FigureTotals reads a *frozen* policy rather than the live Settings,
 * so that changing a rate today cannot restate a day that closed last year. In
 * the file format that snapshot lives in the Archive; here it is one row in
 * `day_policy`.
 *
 * There is exactly one function that writes that row, and that is the point of
 * this file. The last time policy copying was open-coded in more than one place
 * -- System::EndDay had its own run of assignments -- it omitted tax_VAT and
 * advertise_fund, and because Settings::FigureVAT treats 0 as a real rate
 * rather than "unset", every archived check silently reported no VAT. A new
 * policy field must have one site to add itself to, not three.
 *
 * Two sources produce a DayPolicy:
 *
 *   From live Settings, at the moment a day closes. Complete and authoritative.
 *
 *   From an Archive, during import. Incomplete by construction: the format
 *   never stored tax_takeout_food at all, and the archives themselves may
 *   predate the fields or have lost them. Marked snapshot_complete = 0.
 */

#ifndef VT_STORE_DAY_POLICY_HH
#define VT_STORE_DAY_POLICY_HH

#include "store.hh"

#include <cstdint>
#include <string>

class Archive;
class Settings;

namespace vt::sql { class Database; }

namespace vt::store {

struct DayPolicy
{
    double tax_food{0.0};
    double tax_alcohol{0.0};
    double tax_room{0.0};
    double tax_merchandise{0.0};
    double tax_GST{0.0};
    double tax_PST{0.0};
    double tax_HST{0.0};
    double tax_QST{0.0};
    double tax_VAT{0.0};
    double royalty_rate{0.0};
    double advertise_fund{0.0};

    int price_rounding{0};
    int change_for_credit{0};
    int change_for_roomcharge{0};
    int change_for_checks{0};
    int change_for_gift{0};
    int discount_alcohol{0};
    int tax_takeout_food{0};

    // Is this row the policy that was actually in force, or the best available
    // reconstruction? Only a day closed by this build can say the former.
    bool complete{false};
};

// The policy in force right now. Used when a day closes: these are the rates
// the day just traded under, read at the one moment they are still current.
[[nodiscard]] DayPolicy PolicyFromSettings(const Settings &settings);

// The policy an archive froze, with today's Settings filling the one field the
// format never stored. Always incomplete -- see the header comment.
[[nodiscard]] DayPolicy PolicyFromArchive(const Archive &archive,
                                          const Settings &settings);

// The only writer of `day_policy`. Idempotent per day: a second call for the
// same day replaces the row rather than failing, so a re-run of an interrupted
// EndDay does not leave two policies for one day.
[[nodiscard]] StoreError WriteDayPolicy(vt::sql::Database &db, int64_t day_id,
                                        const DayPolicy &policy);

} // namespace vt::store

#endif // VT_STORE_DAY_POLICY_HH
