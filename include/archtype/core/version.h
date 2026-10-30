#ifndef ARCHTYPE_CORE_VERSION_H
#define ARCHTYPE_CORE_VERSION_H

#include "archtype/core/defines.h"

AT_EXTERN_C_BEGIN

typedef struct at_version {
    uint32_t major;
    uint32_t minor;
    uint32_t patch;
} at_version;

/* Returns the engine version as numbers. */
AT_API at_version at_get_version(void);

/* Returns the engine version as a string, e.g. "0.1.0". */
AT_API const char *at_get_version_string(void);

AT_EXTERN_C_END

#endif /* ARCHTYPE_CORE_VERSION_H */