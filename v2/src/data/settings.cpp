/**
 * @file settings.cpp
 */

#include "data/settings.hpp"

namespace vt2 {

Settings& Settings::instance() {
    static Settings instance;
    return instance;
}

} // namespace vt2
