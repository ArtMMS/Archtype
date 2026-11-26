# Math tests
add_executable(test_math test_math.c)
target_link_libraries(test_math PRIVATE Archtype::archtype)
add_test(NAME math COMMAND test_math)


# 2D helpers: rectangles, sprite sheets, animations, texture atlas packing
add_executable(test_2d test_2d.c)
target_link_libraries(test_2d PRIVATE Archtype::archtype)
add_test(NAME 2d COMMAND test_2d)

# 3D helpers: perspective camera and fly camera angles
add_executable(test_3d test_3d.c)
target_link_libraries(test_3d PRIVATE Archtype::archtype)
add_test(NAME 3d COMMAND test_3d)