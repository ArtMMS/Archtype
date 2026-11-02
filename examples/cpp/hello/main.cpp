#include <iostream>

#include <archtype/archtype.h>

int main() {
    const at_version version = at_get_version();

    std::cout << "Hello from C++!\n";
    std::cout << "Archtype " << at_get_version_string()
              << " (" << version.major << '.' << version.minor << '.'
              << version.patch << ")\n";
    return 0;
}
