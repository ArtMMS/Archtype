# Math tests
add_executable(test_math test_math.c)
target_link_libraries(test_math PRIVATE Archtype::archtype)
add_test(NAME math COMMAND test_math)