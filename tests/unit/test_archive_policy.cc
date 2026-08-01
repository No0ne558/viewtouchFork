/*
 * Tests for the frozen tax-policy snapshot an Archive carries.
 *
 * Each Archive stores a copy of the tax rates that were in force on that
 * business day. SubCheck::FigureTotals reads those frozen rates rather than the
 * live Settings, which is what stops a rate change today from silently
 * rewriting last year's totals. That makes the completeness of the snapshot a
 * correctness property, not a detail.
 *
 * These tests exist because it was not complete: System::EndDay copied 16 of
 * the 18 policy fields into each new archive and omitted tax_VAT and
 * advertise_fund. Since NewArchive() builds via Archive(TimeInfo&), which zeroes
 * every rate, and Settings::FigureVAT treats 0 as a real rate rather than a
 * missing-value sentinel, every archived check recomputed VAT as zero.
 */

#include <catch2/catch_all.hpp>
#include "main/data/archive.hh"
#include "main/data/settings.hh"
#include "src/core/time_info.hh"

namespace {

// Rates chosen so that no two fields share a value; a field copied from the
// wrong source is then visible rather than coincidentally correct.
void SetDistinctRates(Settings &settings)
{
    settings.tax_food         = 0.01;
    settings.tax_alcohol      = 0.02;
    settings.tax_room         = 0.03;
    settings.tax_merchandise  = 0.04;
    settings.tax_GST          = 0.05;
    settings.tax_PST          = 0.06;
    settings.tax_HST          = 0.07;
    settings.tax_QST          = 0.08;
    settings.tax_VAT          = 0.09;
    settings.royalty_rate     = 0.10;
    settings.advertise_fund   = 0.11;
}

} // namespace

TEST_CASE("A new Archive starts with a zeroed policy snapshot", "[archive][policy]")
{
    TimeInfo end;
    end.Set();
    Archive archive(end);

    // Archive(TimeInfo&) is what System::NewArchive() uses. It deliberately
    // zeroes the rates; EndDay is responsible for filling them in.
    SECTION("every rate starts at zero")
    {
        REQUIRE(archive.tax_food == Catch::Approx(0.0));
        REQUIRE(archive.tax_alcohol == Catch::Approx(0.0));
        REQUIRE(archive.tax_VAT == Catch::Approx(0.0));
        REQUIRE(archive.advertise_fund == Catch::Approx(0.0));
    }
}

TEST_CASE("Archive policy snapshot covers every rate FigureTotals reads",
          "[archive][policy][vat]")
{
    Settings settings;
    SetDistinctRates(settings);

    TimeInfo end;
    end.Set();
    Archive archive(end);
    archive.CopyPolicyFrom(settings);

    SECTION("the ordinary tax rates are captured")
    {
        REQUIRE(archive.tax_food == Catch::Approx(settings.tax_food));
        REQUIRE(archive.tax_alcohol == Catch::Approx(settings.tax_alcohol));
        REQUIRE(archive.tax_room == Catch::Approx(settings.tax_room));
        REQUIRE(archive.tax_merchandise == Catch::Approx(settings.tax_merchandise));
    }

    SECTION("the Canadian rates are captured")
    {
        REQUIRE(archive.tax_GST == Catch::Approx(settings.tax_GST));
        REQUIRE(archive.tax_PST == Catch::Approx(settings.tax_PST));
        REQUIRE(archive.tax_HST == Catch::Approx(settings.tax_HST));
        REQUIRE(archive.tax_QST == Catch::Approx(settings.tax_QST));
    }

    SECTION("tax_VAT is captured")
    {
        // The regression. FigureTotals does `VAT_tax = archive->tax_VAT` and
        // passes it to FigureVAT, which uses `tax >= 0 ? tax : tax_VAT`. Zero
        // satisfies `>= 0`, so a missing snapshot silently charges no VAT at all
        // rather than falling back to the live setting.
        REQUIRE(archive.tax_VAT == Catch::Approx(settings.tax_VAT));
        REQUIRE(archive.tax_VAT != Catch::Approx(0.0));
    }

    SECTION("advertise_fund is captured")
    {
        REQUIRE(archive.advertise_fund == Catch::Approx(settings.advertise_fund));
        REQUIRE(archive.advertise_fund != Catch::Approx(0.0));
    }

    SECTION("royalty_rate is captured")
    {
        REQUIRE(archive.royalty_rate == Catch::Approx(settings.royalty_rate));
    }
}

TEST_CASE("A zero VAT snapshot yields zero VAT, not the live rate",
          "[archive][policy][vat]")
{
    // Pins the interaction that made the missing snapshot invisible, so that
    // anyone changing FigureVAT's sentinel handling has to confront it.
    Settings settings;
    settings.tax_VAT = 0.20;

    TimeInfo when;
    when.Set();

    SECTION("an explicit zero is honoured as a rate")
    {
        REQUIRE(settings.FigureVAT(10000, when, 0.0) == 0);
    }

    SECTION("only a negative value falls back to the configured rate")
    {
        REQUIRE(settings.FigureVAT(10000, when, -1) == 2000);
    }
}
