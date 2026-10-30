#include <stdio.h>

#include <archtype/archtype.h>

int main(void) {
    at_version version = at_get_version();

    printf("Hello from C!\n");
    printf("Archtype %s (%u.%u.%u)\n",
           at_get_version_string(),
           (unsigned)version.major,
           (unsigned)version.minor,
           (unsigned)version.patch);

    return 0;
}