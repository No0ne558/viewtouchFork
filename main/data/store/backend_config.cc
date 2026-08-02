/*
 * backend_config.cc - See backend_config.hh, in particular the rollback note.
 */

#include "backend_config.hh"

#include "conf_file.hh"
#include "dual_store.hh"
#include "vt_logger.hh"

#include <algorithm>
#include <cctype>
#include <exception>
#include <filesystem>
#include <optional>
#include <memory>
#include <string>

namespace vt::store {

namespace {

constexpr std::string_view kSection = "persistence";

std::string Lowered(const std::string &text)
{
    std::string out = text;
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return out;
}

} // namespace

const char *BackendModeName(BackendMode mode) noexcept
{
    switch (mode)
    {
    case BackendMode::Legacy: return "legacy";
    case BackendMode::Dual:   return "dual";
    case BackendMode::Sqlite: return "sqlite";
    }
    return "legacy";
}

BackendMode ParseBackendMode(const std::string &text, bool &recognised) noexcept
{
    const std::string lowered = Lowered(text);
    recognised = true;
    if (lowered == "legacy")
        return BackendMode::Legacy;
    if (lowered == "dual")
        return BackendMode::Dual;
    if (lowered == "sqlite")
        return BackendMode::Sqlite;

    // An unrecognised mode degrades to Legacy rather than to whatever an enum
    // cast would produce. The caller still learns it was not understood.
    recognised = false;
    return BackendMode::Legacy;
}

BackendSettings ReadBackendSettings(const std::string &config_path)
{
    BackendSettings settings;

    // ConfFile's loading constructor throws when the file is absent or will not
    // parse -- which is exactly the case that has to degrade quietly, since
    // almost every site has never heard of this setting and has no such file.
    // Letting that escape would turn "no persistence config" into a startup
    // failure. The existence check handles the common path; the catch covers a
    // file that exists but is malformed, and neither may propagate.
    std::error_code ec;
    if (!std::filesystem::exists(config_path, ec))
        return settings;

    std::optional<std::string> mode_text;
    std::optional<std::string> path_text;
    try
    {
        ConfFile conf(config_path, true);
        mode_text = conf.TryGetValue("mode", kSection);
        path_text = conf.TryGetValue("database_path", kSection);
    }
    catch (const std::exception &e)
    {
        ::vt::Logger::error("cannot read persistence settings from '{}' ({}); "
                            "using legacy", config_path, e.what());
        return settings;
    }

    if (const auto &mode = mode_text; mode.has_value())
    {
        bool recognised = false;
        settings.mode = ParseBackendMode(*mode, recognised);
        if (!recognised)
        {
            ::vt::Logger::error(
                "persistence mode '{}' is not one of legacy/dual/sqlite; "
                "using legacy", *mode);
        }
    }

    if (const auto &path = path_text; path.has_value())
    {
        settings.database_path = *path;
    }

    // Asking for a database without saying where is a configuration error, not
    // something to guess a path for. Degrading to Legacy is the safe reading:
    // files keep working and the operator gets told.
    if (settings.mode != BackendMode::Legacy && settings.database_path.empty())
    {
        ::vt::Logger::error(
            "persistence mode is '{}' but no database_path is set; using legacy",
            BackendModeName(settings.mode));
        settings.mode = BackendMode::Legacy;
    }

    return settings;
}

std::unique_ptr<Store> MakeConfiguredStore(const BackendSettings &settings,
                                           System *system, StoreError &error)
{
    error = StoreError::Ok;

    if (settings.mode == BackendMode::Legacy)
        return MakeLegacyFileStore(system);

    StoreError sqlite_error = StoreError::Ok;
    auto sqlite = MakeSqliteStore(settings.database_path, sqlite_error);
    if (sqlite == nullptr)
    {
        // No fallback to Legacy. A site that asked for a database and silently
        // got files would write a day's takings somewhere nobody is looking
        // for them.
        error = sqlite_error;
        ::vt::Logger::error("cannot open the persistence database at '{}': {}",
                            settings.database_path, StoreErrorName(sqlite_error));
        return nullptr;
    }

    if (settings.mode == BackendMode::Sqlite)
        return sqlite;

    auto legacy = MakeLegacyFileStore(system);
    if (legacy == nullptr)
    {
        error = StoreError::Io;
        return nullptr;
    }

    // Legacy stays the primary in dual mode. That is the whole posture: the
    // proven backend remains authoritative and the new one shadows it until a
    // site's own divergence report says otherwise.
    auto dual = MakeDualRunStore(std::move(legacy), std::move(sqlite));
    if (dual == nullptr)
    {
        error = StoreError::Io;
        return nullptr;
    }
    return dual;
}

} // namespace vt::store
