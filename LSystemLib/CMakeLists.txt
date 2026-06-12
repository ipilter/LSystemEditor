set(LSYSTEM_LIB_DIR "${CMAKE_CURRENT_LIST_DIR}")

set(LSYSTEM_LIB_SOURCES
    "${LSYSTEM_LIB_DIR}/src/LSystem.cpp"
    "${LSYSTEM_LIB_DIR}/src/LSystemController.cpp"
    "${LSYSTEM_LIB_DIR}/src/LSystemEvaluator.cpp"
    "${LSYSTEM_LIB_DIR}/src/details/LSystemConstantsParse.cpp"
    "${LSYSTEM_LIB_DIR}/src/details/LSystemImpl.cpp"
    "${LSYSTEM_LIB_DIR}/src/details/LSystemLineParse.cpp"
    "${LSYSTEM_LIB_DIR}/src/details/LSystemMaterialParse.cpp"
    "${LSYSTEM_LIB_DIR}/src/details/ModuleStringParser.cpp")

file(GLOB_RECURSE LSYSTEM_LIB_HEADERS CONFIGURE_DEPENDS
    "${LSYSTEM_LIB_DIR}/include/*.h"
    "${LSYSTEM_LIB_DIR}/src/*.h"
    "${LSYSTEM_LIB_DIR}/src/details/*.h")

add_library(LSystemLib STATIC ${LSYSTEM_LIB_SOURCES})
set_target_properties(LSystemLib PROPERTIES
    OUTPUT_NAME LSystem
    FOLDER "LSystemLib"
)

target_sources(LSystemLib PRIVATE ${LSYSTEM_LIB_HEADERS})
set_source_files_properties(${LSYSTEM_LIB_HEADERS} PROPERTIES HEADER_FILE_ONLY TRUE)

source_group(TREE "${LSYSTEM_LIB_DIR}" PREFIX "" FILES
    ${LSYSTEM_LIB_SOURCES}
    ${LSYSTEM_LIB_HEADERS})

target_sources(LSystemLib PRIVATE "${LSYSTEM_LIB_DIR}/CMakeLists.txt")
set_source_files_properties("${LSYSTEM_LIB_DIR}/CMakeLists.txt" PROPERTIES HEADER_FILE_ONLY TRUE)
source_group("" FILES "${LSYSTEM_LIB_DIR}/CMakeLists.txt")

target_include_directories(LSystemLib PUBLIC
    $<BUILD_INTERFACE:${LSYSTEM_LIB_DIR}/include>
    $<INSTALL_INTERFACE:include>
)
target_include_directories(LSystemLib PRIVATE
    "${LSYSTEM_LIB_DIR}/src/details"
)

target_link_libraries(LSystemLib PUBLIC glm::glm)

if(MSVC)
    target_compile_options(LSystemLib PRIVATE
        "$<$<COMPILE_LANGUAGE:CXX>:/external:W0>"
        "$<$<COMPILE_LANGUAGE:CXX>:/external:I${CMAKE_SOURCE_DIR}/third_party/glm>"
    )
endif()

include(FetchContent)
FetchContent_Declare(googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG        v1.14.0
    GIT_SHALLOW    TRUE)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

add_subdirectory(tests)
