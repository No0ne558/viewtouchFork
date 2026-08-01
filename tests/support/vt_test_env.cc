#include "vt_test_env.hh"

#include "main/data/system.hh"

#include <memory>

namespace vt_test {

void EnsureMasterSystem()
{
    if (MasterSystem == nullptr)
    {
        MasterSystem = std::make_unique<System>();
    }
}

} // namespace vt_test
