lsystems_glob_tests(LSYSTEM_LIB_TEST_CPP "${CMAKE_CURRENT_SOURCE_DIR}")

add_executable(LSystemLibTests ${LSYSTEM_LIB_TEST_CPP})
set_target_properties(LSystemLibTests PROPERTIES FOLDER "LSystemLib")

lsystems_target_mirror_tree(LSystemLibTests "${CMAKE_CURRENT_SOURCE_DIR}/..")

target_link_libraries(LSystemLibTests PRIVATE
    LSystemLib
    GTest::gtest_main
    glm::glm
)
target_include_directories(LSystemLibTests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../src/details"
)

include(GoogleTest)
gtest_discover_tests(LSystemLibTests)
