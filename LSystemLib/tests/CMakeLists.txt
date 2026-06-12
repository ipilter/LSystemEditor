set(LSYSTEM_LIB_TEST_DIR "${CMAKE_CURRENT_LIST_DIR}")

file(GLOB LSYSTEM_LIB_TEST_CPP CONFIGURE_DEPENDS "${LSYSTEM_LIB_TEST_DIR}/*_test.cpp")

add_executable(LSystemLibTests ${LSYSTEM_LIB_TEST_CPP})
set_target_properties(LSystemLibTests PROPERTIES FOLDER "LSystemLib/tests")

source_group(TREE "${LSYSTEM_LIB_TEST_DIR}" PREFIX "" FILES ${LSYSTEM_LIB_TEST_CPP})

target_sources(LSystemLibTests PRIVATE "${LSYSTEM_LIB_TEST_DIR}/CMakeLists.txt")
set_source_files_properties("${LSYSTEM_LIB_TEST_DIR}/CMakeLists.txt" PROPERTIES HEADER_FILE_ONLY TRUE)
source_group("" FILES "${LSYSTEM_LIB_TEST_DIR}/CMakeLists.txt")

target_link_libraries(LSystemLibTests PRIVATE
    LSystemLib
    GTest::gtest_main
    glm::glm
)
target_include_directories(LSystemLibTests PRIVATE
    "${LSYSTEM_LIB_TEST_DIR}/../src/details"
)

include(GoogleTest)
gtest_discover_tests(LSystemLibTests)
