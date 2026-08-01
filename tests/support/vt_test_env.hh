/*
 * Shared test environment for ViewTouch unit tests.
 *
 * Much of the business logic reaches the MasterSystem global rather than taking
 * its dependencies as arguments. Credit::Clear(), for example, is called from
 * the Credit constructor and reads
 * MasterSystem->settings.authorize_method (credit.cc:172), so simply declaring
 * a `Credit credit;` in a test segfaults on a null MasterSystem.
 *
 * Removing that coupling is a much larger refactor than the current work
 * warrants, so instead tests initialise the global once and share it. Any test
 * that constructs a business object should call EnsureMasterSystem() first --
 * or, more conveniently, derive its fixture from VtSystemFixture.
 */

#ifndef VT_TEST_ENV_HH
#define VT_TEST_ENV_HH

namespace vt_test {

// Creates the MasterSystem global if it does not already exist. Idempotent and
// safe to call from every test; the instance is shared for the whole run and
// deliberately never torn down, since teardown order against other globals is
// not something the production code defines.
void EnsureMasterSystem();

// Convenience fixture: derive a TEST_CASE_METHOD from this to get the global
// initialised before the test body runs.
struct VtSystemFixture
{
    VtSystemFixture() { EnsureMasterSystem(); }
};

} // namespace vt_test

#endif // VT_TEST_ENV_HH
