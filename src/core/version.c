#include "archtype/core/version.h"

#define AT_STRINGIFY_IMPL(x) #x
#define AT_STRINGIFY(x)      AT_STRINGIFY_IMPL(x)

#define AT_VERSION_STRING      \
    AT_STRINGIFY(AT_VERSION_MAJOR) "." \
    AT_STRINGIFY(AT_VERSION_MINOR) "." \
    AT_STRINGIFY(AT_VERSION_PATCH)

at_version at_get_version(void) {
    at_version version;
    version.major = AT_VERSION_MAJOR;
    version.minor = AT_VERSION_MINOR;
    version.patch = AT_VERSION_PATCH;
    return version;
}

const char *at_get_version_string(void) {
    return AT_VERSION_STRING;
}