/*
 * Characterization tests for Credit magstripe parsing (main/data/credit.cc)
 *
 * ParseSwipe consumes raw card-reader output -- untrusted, externally supplied
 * input that reaches fixed-size char buffers directly. It had no coverage,
 * because credit.cc was compiled into the vt_main executable and no test could
 * link it.
 *
 * Everything below drives the public entry point rather than the private
 * ParseTrack* helpers, so these exercise the same path a real swipe takes:
 * ParseSwipe -> GetTrack -> ParseTrackN -> ValidateCardInfo.
 *
 * These are characterization tests: they pin what the code does today, since
 * that is the contract the SQL migration must preserve. The Debug build runs
 * with ASan and UBSan, so the long-input cases are live overflow probes -- an
 * out-of-bounds write inside a parser fails the test run rather than silently
 * corrupting adjacent Credit members.
 *
 * Every PAN below is a well-known publicly published test number, not real
 * cardholder data.
 */

#include <catch2/catch_all.hpp>
#include "main/data/credit.hh"
#include "main/data/settings.hh"
#include "main/data/system.hh"
#include "src/core/data_file.hh"
#include "main/data/store/day_contents.hh"
#include "main/data/store/store.hh"
#include "sql/database.hh"
#include "sql/statement.hh"
#include "support/vt_test_env.hh"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace fs = std::filesystem;

namespace {

// Track 2 layout: ; PAN = expiry(4) service(3) PVV(5) discretionary ?
std::string Track2(const std::string &pan,
                   const std::string &expiry = "2512",
                   const std::string &tail = "1011234567890")
{
    return ";" + pan + "=" + expiry + tail + "?";
}

} // namespace

TEST_CASE_METHOD(vt_test::VtSystemFixture, "ParseSwipe reads a well-formed track 2", "[credit][swipe]")
{
    SECTION("the PAN is recovered")
    {
        Credit credit;
        credit.ParseSwipe(Track2("4111111111111111").c_str());

        REQUIRE_FALSE(credit.IsEmpty());
        REQUIRE(std::string(credit.PAN(1)) == "4111111111111111");
    }

    SECTION("a card with no field separator yields nothing")
    {
        Credit credit;
        credit.ParseSwipe(";4111111111111111");
        REQUIRE(credit.IsEmpty());
    }

    SECTION("non-card input is ignored rather than misparsed")
    {
        Credit credit;
        credit.ParseSwipe("this is not a card");
        REQUIRE(credit.IsEmpty());
    }

    SECTION("empty input does not crash")
    {
        Credit credit;
        credit.ParseSwipe("");
        REQUIRE(credit.IsEmpty());
    }

    SECTION("only sentinels recovers no card number")
    {
        // Note IsEmpty() is not the right assertion here. ParseSwipe stores the
        // raw track into `swipe` as soon as GetTrack extracts one, before
        // ParseTrack2 has had a chance to reject it, and IsEmpty() reports false
        // whenever `swipe` is set. So a malformed track is "not empty" while
        // still yielding no usable card. Callers that care must check the PAN.
        Credit credit;
        credit.ParseSwipe(";?");
        REQUIRE(std::string(credit.PAN(1)).empty());
    }
}

TEST_CASE_METHOD(vt_test::VtSystemFixture, "ParseSwipe survives hostile input", "[credit][swipe][bounds]")
{
    // Reaching the end of each section without a sanitizer report is the
    // assertion. ParseTrack2's discretionary-data loop is bounded only by the
    // input length and the '?' sentinel -- unlike every other field in that
    // function, which also has an index limit -- and it writes into a
    // STRLENGTH (512) buffer while ParseSwipe hands it a STRLONG (2048) one.

    SECTION("a very long discretionary field")
    {
        Credit credit;
        const std::string swipe =
            ";4111111111111111=2512101" + std::string(1500, '9') + "?";
        credit.ParseSwipe(swipe.c_str());
        SUCCEED("no out-of-bounds write");
    }

    SECTION("a long track with no end sentinel")
    {
        Credit credit;
        const std::string swipe =
            ";4111111111111111=2512101" + std::string(1500, '9');
        credit.ParseSwipe(swipe.c_str());
        SUCCEED("no out-of-bounds write");
    }

    SECTION("an absurdly long PAN")
    {
        Credit credit;
        const std::string swipe = ";" + std::string(500, '4') + "=25121011234567890?";
        credit.ParseSwipe(swipe.c_str());
        SUCCEED("no out-of-bounds write");
    }

    SECTION("a run of start sentinels")
    {
        Credit credit;
        credit.ParseSwipe(std::string(2000, ';').c_str());
        SUCCEED("no out-of-bounds write");
    }

    SECTION("a track 1 start sentinel with no content")
    {
        Credit credit;
        credit.ParseSwipe("%");
        SUCCEED("no out-of-bounds read");
    }

    SECTION("embedded NUL-adjacent truncation")
    {
        Credit credit;
        credit.ParseSwipe(";4111111111111111=");
        SUCCEED("no out-of-bounds read");
    }

    SECTION("a track 3 with no end sentinel")
    {
        // ParseTrack3's discretionary loop tested only for '?', with no NUL
        // check and no destination bound, so input without a terminating '?'
        // ran off the end of the input buffer as well as past t3_disc.
        Credit credit;
        const std::string swipe =
            ";4111111111111111=25121011234567890?;" + std::string(1200, '7');
        credit.ParseSwipe(swipe.c_str());
        SUCCEED("no out-of-bounds read or write");
    }

    SECTION("a long track 1")
    {
        Credit credit;
        const std::string swipe =
            "%B4111111111111111^DOE/JOHN^2512101" + std::string(1200, '3') + "?";
        credit.ParseSwipe(swipe.c_str());
        SUCCEED("no out-of-bounds write");
    }
}

TEST_CASE_METHOD(vt_test::VtSystemFixture, "Card brand is derived from the swiped PAN", "[credit][cardtype]")
{
    SECTION("Visa, 16 digits starting 4")
    {
        Credit credit;
        credit.ParseSwipe(Track2("4111111111111111").c_str());
        REQUIRE(credit.CreditType() == CREDIT_TYPE_VISA);
    }

    SECTION("MasterCard, 16 digits in the 51-55 range")
    {
        Credit credit;
        credit.ParseSwipe(Track2("5500000000000004").c_str());
        REQUIRE(credit.CreditType() == CREDIT_TYPE_MASTERCARD);
    }

    SECTION("Amex, 15 digits starting 34 or 37")
    {
        Credit credit;
        credit.ParseSwipe(Track2("340000000000009").c_str());
        REQUIRE(credit.CreditType() == CREDIT_TYPE_AMEX);
    }

    SECTION("Discover, 16 digits starting 6011")
    {
        Credit credit;
        credit.ParseSwipe(Track2("6011000000000004").c_str());
        REQUIRE(credit.CreditType() == CREDIT_TYPE_DISCOVER);
    }

    SECTION("a too-short number is not classified as a known brand")
    {
        Credit credit;
        credit.ParseSwipe(Track2("411111").c_str());
        REQUIRE(credit.CreditType() != CREDIT_TYPE_VISA);
    }
}

TEST_CASE_METHOD(vt_test::VtSystemFixture, "MaskCardNumber redacts all but the last four", "[credit][pan][mask]")
{
    // This is what keeps full PANs out of stored records when
    // save_entire_cc_num is off, so its exactness matters.

    SECTION("a 16-digit PAN keeps only its last four digits")
    {
        Credit credit;
        credit.ParseSwipe(Track2("4111111111111111").c_str());
        credit.MaskCardNumber();

        const std::string masked = credit.PAN(1);
        REQUIRE(masked.size() == 16);
        REQUIRE(masked.substr(12) == "1111");
        REQUIRE(masked.substr(0, 12) == std::string(12, 'x'));
    }

    SECTION("PAN(0) masks without mutating the stored number")
    {
        Credit credit;
        credit.ParseSwipe(Track2("4111111111111111").c_str());

        const std::string masked = credit.PAN(0);
        REQUIRE(masked.substr(0, 12) == std::string(12, 'x'));

        // PAN(1) is the branch save_entire_cc_num selects, and the reason
        // cardholder data has to stay isolated in the SQL schema.
        REQUIRE(std::string(credit.PAN(1)) == "4111111111111111");
    }

    SECTION("masking is idempotent")
    {
        Credit credit;
        credit.ParseSwipe(Track2("4111111111111111").c_str());
        credit.MaskCardNumber();
        const std::string once = credit.PAN(1);
        credit.MaskCardNumber();
        REQUIRE(std::string(credit.PAN(1)) == once);
    }

    SECTION("an Amex PAN masks to 15 characters")
    {
        Credit credit;
        credit.ParseSwipe(Track2("340000000000009").c_str());
        credit.MaskCardNumber();

        const std::string masked = credit.PAN(1);
        REQUIRE(masked.size() == 15);
        REQUIRE(masked.substr(11) == "0009");
    }

    SECTION("masking an empty number does not underflow")
    {
        Credit credit;
        credit.MaskCardNumber();
        REQUIRE(std::string(credit.PAN(1)).empty());
    }

    SECTION("ClearCardNumber removes the PAN entirely")
    {
        Credit credit;
        credit.ParseSwipe(Track2("4111111111111111").c_str());
        REQUIRE_FALSE(credit.IsEmpty());

        credit.ClearCardNumber();
        REQUIRE(std::string(credit.PAN(1)).empty());
    }
}

TEST_CASE_METHOD(vt_test::VtSystemFixture,
                 "A stored card number is masked unless the operator asked otherwise",
                 "[credit][pan][at-rest]")
{
    /*
     * What reaches the disk, asserted on the bytes rather than argued from the
     * code, because this is the one field where being wrong is a breach rather
     * than a bug.
     *
     * The defect: Credit::Write made an exception for a preauthorised card and
     * wrote its full number regardless of save_entire_cc_num. A site that had
     * explicitly asked for masking still had complete card numbers in plaintext
     * check files for every open tab.
     */
    Settings &settings = MasterSystem->settings;
    const int previous = settings.save_entire_cc_num;

    const fs::path path = fs::temp_directory_path() / "vt_credit_at_rest.dat";
    std::error_code ec;

    // Built the way the application builds one -- a swipe -- rather than by
    // assigning private fields, so what is asserted is what a real card does.
    auto WriteCredit = [&](bool preauthed) {
        Credit credit;
        credit.ParseSwipe(Track2("4111111111111111").c_str());
        REQUIRE_FALSE(credit.IsEmpty());
        if (preauthed)
        {
            credit.SetState(CCAUTH_PREAUTH);
            REQUIRE(credit.IsPreauthed() == 1);
        }

        OutputDataFile out;
        REQUIRE(out.Open(path.string(), 1) == 0);
        REQUIRE(credit.Write(out, 1) == 0);
        out.Close();

        std::ifstream in(path, std::ios::binary);
        std::ostringstream body;
        body << in.rdbuf();
        return body.str();
    };

    SECTION("masked by default, and a preauth is no exception")
    {
        settings.save_entire_cc_num = 0;

        const std::string ordinary = WriteCredit(/*preauthed=*/false);
        REQUIRE(ordinary.find("4111111111111111") == std::string::npos);
        REQUIRE(ordinary.find("1111") != std::string::npos);   // last four kept

        // The case that used to leak. Same assertion, and it is the whole point
        // of the change: a preauth does not get to override the setting.
        const std::string preauth = WriteCredit(/*preauthed=*/true);
        REQUIRE(preauth.find("4111111111111111") == std::string::npos);
        REQUIRE(preauth.find("1111") != std::string::npos);
    }

    SECTION("the full number is written only when explicitly configured")
    {
        // Not an accident and not a default. A site that wants unmasked numbers
        // on disk -- to complete a preauth across a restart without a re-swipe
        // -- has exactly one way to ask for it.
        settings.save_entire_cc_num = 1;
        REQUIRE(WriteCredit(/*preauthed=*/false).find("4111111111111111")
                != std::string::npos);
    }

    SECTION("a reloaded masked number asks to be re-entered")
    {
        // Nothing is stranded by masking: RequireSwipe is what the rest of the
        // system already uses to notice a number it cannot transact with.
        // Round-tripped through the writer and reader, so the masked value is
        // the one the format actually produces rather than one typed here.
        settings.save_entire_cc_num = 0;
        (void)WriteCredit(/*preauthed=*/true);

        int version = 0;
        InputDataFile in;
        REQUIRE(in.Open(path.string(), version) == 0);
        Credit reloaded;
        REQUIRE(reloaded.Read(in, version) == 0);
        in.Close();

        REQUIRE(reloaded.RequireSwipe() == 1);

        Credit usable;
        usable.ParseSwipe(Track2("4111111111111111").c_str());
        REQUIRE(usable.RequireSwipe() == 0);
    }

    settings.save_entire_cc_num = previous;
    fs::remove(path, ec);
}

TEST_CASE_METHOD(vt_test::VtSystemFixture,
                 "Credit transactions reach SQL under the same masking rule",
                 "[credit][pan][at-rest][sqlite]")
{
    /*
     * The credit databases were held back until the cardholder-data hardening
     * landed, because migrating them is what widens where those bytes live.
     * This is the assertion that makes the widening safe: the database column
     * gets whatever Credit::PAN(save_entire_cc_num) returned and nothing else,
     * so the file and the database cannot disagree about what left memory.
     */
    Settings &settings = MasterSystem->settings;
    const int previous = settings.save_entire_cc_num;

    const fs::path db_path = fs::temp_directory_path() / "vt_credit_sql.vtdb";
    std::error_code ec;
    for (const char *suffix : {"", "-wal", "-shm"})
        fs::remove(db_path.string() + suffix, ec);

    CreditDB voids(CC_DBTYPE_VOID);
    auto *credit = new Credit;
    credit->ParseSwipe(Track2("4111111111111111").c_str());
    credit->SetApproval("OK123");
    credit->Amount(2500);
    voids.Add(credit);

    auto WriteAndRead = [&](int store_full) -> std::string {
        for (const char *suffix : {"", "-wal", "-shm"})
            fs::remove(db_path.string() + suffix, ec);
        settings.save_entire_cc_num = store_full;

        vt::store::StoreError error = vt::store::StoreError::Io;
        auto store = vt::store::MakeSqliteStore(db_path.string(), error);
        REQUIRE(error == vt::store::StoreError::Ok);

        vt::sql::Database db;
        REQUIRE(db.Open(db_path.string()) == vt::sql::Status::Ok);
        int64_t day = 0;
        REQUIRE(db.QueryInt("SELECT id FROM business_day "
                            "WHERE closed_at_local IS NULL;", day)
                == vt::sql::Status::Ok);
        REQUIRE(vt::store::WriteCreditTransactions(db, day, &voids, nullptr,
                                                   nullptr)
                == vt::store::StoreError::Ok);

        vt::sql::Statement stmt;
        REQUIRE(stmt.Prepare(db, "SELECT card_number FROM credit_transaction;")
                == vt::sql::Status::Ok);
        vt::sql::Status step = vt::sql::Status::Ok;
        REQUIRE(stmt.Step(step));
        return stmt.ColumnText(0);
    };

    SECTION("masked by default")
    {
        REQUIRE(WriteAndRead(0) != "4111111111111111");
    }

    SECTION("full only when configured, and the row records which")
    {
        REQUIRE(WriteAndRead(1) == "4111111111111111");

        vt::sql::Database db;
        REQUIRE(db.Open(db_path.string()) == vt::sql::Status::Ok);
        int64_t masked = -1;
        REQUIRE(db.QueryInt("SELECT pan_is_masked FROM credit_transaction;", masked)
                == vt::sql::Status::Ok);
        // Recorded rather than inferred, so an audit can answer "did this site
        // ever store full numbers" with a query instead of a regex.
        REQUIRE(masked == 0);
    }

    settings.save_entire_cc_num = previous;
    for (const char *suffix : {"", "-wal", "-shm"})
        fs::remove(db_path.string() + suffix, ec);
}
