/*
 * backend_config.hh - Which persistence backend a site runs, and how it goes
 * back.
 *
 * Cutover is a config change, not a build. A site moves through the modes in
 * order and can stop or reverse at any of them:
 *
 *   Legacy   files only. What every site runs today, and the default -- a
 *            deployment that does not know about this setting must not change
 *            behaviour.
 *   Dual     files authoritative, SQLite shadowing. Every save goes to both and
 *            divergence is measurable against that site's own data. This is
 *            where the evidence for cutover comes from.
 *   Sqlite   SQLite authoritative. Files are no longer written.
 *
 * ROLLBACK.
 *
 * From Legacy or Dual there is nothing to undo: the files were authoritative
 * throughout and are complete. Set the mode back and restart.
 *
 * From Sqlite the procedure is to reinstall the previous version against the
 * data directory, which is intact because the importer never modified it (see
 * importer.hh, and the test that asserts it on the bytes). What is lost is
 * everything written after cutover, since those writes went only to the
 * database.
 *
 * That is the real cost and it is stated rather than glossed: cutting over is
 * reversible only back to the moment of cutover. A site that has traded for a
 * week on SQLite and then rolls back loses that week. The mitigation is to run
 * Dual long enough that cutover is boring, not to promise a rollback that
 * cannot exist.
 *
 * A legacy-format exporter would narrow that window and is deliberately not
 * built here. It could only emit the CURRENT format version -- the writers take
 * a version argument but Check::Write rejects anything but the constant, and
 * every caller passes the constant -- so an exporter cannot produce files an
 * older binary would read if the format has moved on. Building one without
 * that caveat understood would be selling a rollback that silently fails.
 */

#ifndef VT_STORE_BACKEND_CONFIG_HH
#define VT_STORE_BACKEND_CONFIG_HH

#include "store.hh"

#include <memory>
#include <string>

class System;

namespace vt::store {

enum class BackendMode
{
    Legacy = 0,   // files only -- the default
    Dual,         // files authoritative, SQLite shadowing
    Sqlite,       // SQLite authoritative
};

[[nodiscard]] const char *BackendModeName(BackendMode mode) noexcept;

// Parse a mode name. Unknown or empty text yields Legacy and sets
// `recognised` to false, so a typo in a config file degrades to the safe mode
// rather than to whatever an enum cast produces -- and the caller can still
// warn about it instead of silently accepting nonsense.
[[nodiscard]] BackendMode ParseBackendMode(const std::string &text,
                                           bool &recognised) noexcept;

struct BackendSettings
{
    BackendMode mode{BackendMode::Legacy};
    // Where the database lives. Only consulted for Dual and Sqlite.
    std::string database_path;
};

/*
 * Read the backend settings from a config file.
 *
 * Looks for `mode` and `database_path` under the [persistence] section. A
 * missing file, missing section or missing key all yield Legacy, because the
 * overwhelmingly common case is a site that has never heard of this setting and
 * must keep behaving exactly as it does now.
 */
[[nodiscard]] BackendSettings ReadBackendSettings(const std::string &config_path);

/*
 * Build the store a site's settings ask for.
 *
 * Returns nullptr with `error` set when the requested backend cannot be opened.
 * The caller decides what to do about that; this deliberately does NOT fall
 * back to Legacy on failure. A site that asked for Sqlite and silently got
 * files would write a day's takings somewhere nobody is looking.
 */
[[nodiscard]] std::unique_ptr<Store> MakeConfiguredStore(
    const BackendSettings &settings, System *system, StoreError &error);

} // namespace vt::store

#endif // VT_STORE_BACKEND_CONFIG_HH
