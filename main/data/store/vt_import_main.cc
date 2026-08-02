/*
 * vt_import_main.cc - Run the legacy-to-SQLite import from the command line.
 *
 * A separate executable rather than a mode of vt_main, for two reasons that
 * both point the same way. The import is a one-time maintenance operation that
 * should run against a stopped POS, so folding it into the binary that opens
 * tills invites running it against a live one. And vt_main's startup brings up
 * X11, terminals and the event loop before it could reach an import; none of
 * that is wanted here.
 *
 *   vt_import --data-path /usr/viewtouch/dat [--database PATH] [--dry-run]
 *
 * The database path defaults to whatever dat/persistence.conf names, so an
 * operator does not have to type it twice and cannot type it differently from
 * what the running system will open.
 *
 * Originals are never modified -- see importer.hh, and the test that asserts it
 * on the file bytes. --dry-run additionally leaves the database untouched by
 * importing into a throwaway copy of the path, so a site can see what an import
 * would report before committing to one.
 */

#include "backend_config.hh"
#include "importer.hh"

#include "manager.hh"
#include "settings.hh"
#include "system.hh"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string>

namespace fs = std::filesystem;

namespace {

[[noreturn]] void Usage(int code)
{
    std::fprintf(code == 0 ? stdout : stderr,
        "usage: vt_import --data-path DIR [--database PATH] [--dry-run]\n"
        "\n"
        "  --data-path DIR   ViewTouch data directory (the one holding\n"
        "                    settings.dat, archive/ and current/).\n"
        "  --database PATH   Where to write. Defaults to database_path from\n"
        "                    DIR/persistence.conf.\n"
        "  --dry-run         Import into a temporary database and delete it,\n"
        "                    reporting what a real run would do.\n"
        "\n"
        "Archives are opened read-only and are never modified. Re-running is\n"
        "safe: archives already imported are skipped.\n");
    std::exit(code);
}

void PrintResult(const vt::store::ImportResult &result, bool dry_run)
{
    const vt::store::ImportStats &s = result.stats;
    std::printf("%s\n", dry_run ? "Dry run -- no database was kept." : "Import complete.");
    std::printf("  business days   %d\n", s.days);
    std::printf("  archives read   %d\n", s.archives_read);
    std::printf("  archives failed %d\n", s.archives_failed);
    std::printf("  checks          %d\n", s.checks);
    std::printf("  subchecks       %d\n", s.subchecks);
    std::printf("  orders          %d (+%d modifiers)\n", s.orders, s.modifiers);
    std::printf("  payments        %d\n", s.payments);
    std::printf("  training checks skipped   %d\n", s.training_skipped);
    std::printf("  same-day serial collisions %d\n", s.serial_collisions);

    // Naming them matters. A count alone tells an operator a day is missing
    // without telling them which one, which is not something they can act on.
    if (!result.failures.empty())
    {
        std::printf("\nArchives that could not be read:\n");
        for (const std::string &file : result.failures)
            std::printf("  %s\n", file.c_str());
        std::printf("\nThese days were NOT imported. The files are untouched.\n");
    }

    if (s.checks > 0)
    {
        std::printf(
            "\nNote: imported totals are what today's engine recomputes from the\n"
            "orders against each day's frozen tax rates. The legacy format stored\n"
            "no totals at all, so they are marked source=2 rather than presented\n"
            "as the figures that were printed on the receipts.\n");
    }
}

} // namespace

int main(int argc, char **argv)
{
    std::string data_path;
    std::string database;
    bool dry_run = false;

    for (int i = 1; i < argc; ++i)
    {
        const std::string arg = argv[i];
        const auto next = [&]() -> std::string {
            if (i + 1 >= argc)
                Usage(2);
            return argv[++i];
        };

        if (arg == "--data-path")     data_path = next();
        else if (arg == "--database") database = next();
        else if (arg == "--dry-run")  dry_run = true;
        else if (arg == "--help" || arg == "-h") Usage(0);
        else Usage(2);
    }

    if (data_path.empty())
        Usage(2);

    std::error_code ec;
    if (!fs::is_directory(data_path, ec))
    {
        std::fprintf(stderr, "not a directory: %s\n", data_path.c_str());
        return 1;
    }

    if (database.empty())
    {
        const vt::store::BackendSettings configured =
            vt::store::ReadBackendSettings(data_path + "/persistence.conf");
        database = configured.database_path;
    }
    if (database.empty())
    {
        std::fprintf(stderr,
                     "no database path: pass --database, or set database_path in "
                     "%s/persistence.conf\n", data_path.c_str());
        return 1;
    }

    const std::string archive_path = data_path + "/" ARCHIVE_DATA_DIR;
    if (!fs::is_directory(archive_path, ec))
    {
        std::fprintf(stderr, "no archive directory at %s\n", archive_path.c_str());
        return 1;
    }

    // The legacy readers reach MasterSystem from their constructors, and they
    // need Settings for the version-gated defaults of fields absent from older
    // files. Loading the site's real settings rather than defaults matters: an
    // archive's own frozen policy overrides it for anything affecting money,
    // but not everything is frozen.
    MasterSystem = std::make_unique<System>();
    MasterSystem->SetDataPath(data_path.c_str());

    Settings &settings = MasterSystem->settings;
    const std::string settings_file = data_path + "/" MASTER_SETTINGS;
    if (settings.Load(settings_file.c_str()) != 0)
    {
        // Not fatal. A site with no settings.dat can still have archives worth
        // importing, and each archive carries its own policy snapshot.
        std::fprintf(stderr,
                     "warning: could not load %s; using defaults for any field "
                     "an archive does not carry itself\n", settings_file.c_str());
    }

    std::string target = database;
    if (dry_run)
    {
        target = database + ".dryrun";
        for (const char *suffix : {"", "-wal", "-shm"})
            fs::remove(target + suffix, ec);
    }

    std::printf("Importing archives from %s\n", archive_path.c_str());
    std::printf("             into       %s%s\n", target.c_str(),
                dry_run ? " (temporary)" : "");

    const vt::store::ImportResult result =
        vt::store::ImportArchives(archive_path, target, settings);

    if (dry_run)
    {
        for (const char *suffix : {"", "-wal", "-shm"})
            fs::remove(target + suffix, ec);
    }

    if (!result.Ok())
    {
        std::fprintf(stderr, "\nImport failed: %s (%s)\n", result.message.c_str(),
                     vt::store::StoreErrorName(result.error));
        PrintResult(result, dry_run);
        // Days imported before the failure are committed and will be skipped on
        // a re-run, so repeating the command is the right recovery.
        std::fprintf(stderr,
                     "\nDays imported before the failure are committed. Fix the "
                     "cause and run this again;\nalready-imported archives are "
                     "skipped.\n");
        return 1;
    }

    PrintResult(result, dry_run);
    return (result.stats.archives_failed > 0) ? 2 : 0;
}
