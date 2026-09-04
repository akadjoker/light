#include "light_api.h"

namespace light
{

#define LIGHT_STR2(x) #x
#define LIGHT_STR(x) LIGHT_STR2(x)

    const char* version_string()
    {
        return LIGHT_STR(LIGHT_VERSION_MAJOR) "." LIGHT_STR(LIGHT_VERSION_MINOR) "." LIGHT_STR(
            LIGHT_VERSION_PATCH);
    }

} // namespace light
