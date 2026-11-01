# The optional C++ example is the only target that enables C++.
enable_language(CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(hello_cpp hello/main.cpp)
target_link_libraries(hello_cpp PRIVATE Archtype::archtype)
