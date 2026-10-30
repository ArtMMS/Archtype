set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# C example
add_executable(hello_c c/hello/main.c)
target_link_libraries(hello_c PRIVATE Archtype::archtype)

# C++ example (uses the same C headers through extern "C")
add_executable(hello_cpp cpp/hello/main.cpp)
target_link_libraries(hello_cpp PRIVATE Archtype::archtype)

# Window and main loop example
add_executable(window_c c/window/main.c)
target_link_libraries(window_c PRIVATE Archtype::archtype)