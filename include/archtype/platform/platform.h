#ifndef ARCHTYPE_PLATFORM_PLATFORM_H
#define ARCHTYPE_PLATFORM_PLATFORM_H

#include "archtype/core/defines.h"

AT_EXTERN_C_BEGIN

/* Initializes the platform layer. Call once before creating any window.   */
/* Returns false if initialization failed.                                 */
AT_API bool at_platform_init(void);

/* Shuts down the platform layer. Destroy every window before calling it.  */
AT_API void at_platform_shutdown(void);

/* Processes pending window and input events without blocking.             */
AT_API void at_platform_poll_events(void);

/* Blocks until at least one event arrives (useful while minimized).       */
AT_API void at_platform_wait_events(void);

/* Returns the time in seconds since at_platform_init() was called.        */
AT_API double at_platform_get_time(void);

/* Returns the folder that contains the executable, ending with a path     */
/* separator (e.g. "D:/games/mygame/"). Returns "" if it cannot be found.  */
/* Relative asset paths given to the engine are resolved from this folder. */
AT_API const char *at_platform_get_base_path(void);

AT_EXTERN_C_END

#endif /* ARCHTYPE_PLATFORM_PLATFORM_H */