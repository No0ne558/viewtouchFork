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
#include "support/vt_test_env.hh"

#include <string>

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
