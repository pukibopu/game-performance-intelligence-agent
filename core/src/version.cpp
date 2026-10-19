#include "gpia/version.hpp"

#ifndef GPIA_CORE_VERSION
#error "GPIA_CORE_VERSION must be defined by the build system"
#endif

namespace gpia
{
std::string_view version() noexcept
{
    return GPIA_CORE_VERSION;
}
} // namespace gpia
