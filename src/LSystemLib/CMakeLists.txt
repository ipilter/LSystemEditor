option(LSYSTEM_BUILD_TESTS "Build LSystemLib unit tests" OFF)

set(LSYSTEM_LIB_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/src/LSystem.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/LSystemController.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/LSystemEvaluator.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/details/LSystemConstantsParse.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/details/LSystemImpl.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/details/LSystemLineParse.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/details/LSystemMaterialParse.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/details/ModuleStringParser.cpp"
)

add_library(LSystemLib STATIC ${LSYSTEM_LIB_SOURCES})
set_target_properties(LSystemLib PROPERTIES
    OUTPUT_NAME LSystem
    FOLDER "LSystemLib"
)

target_include_directories(LSystemLib PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)
target_include_directories(LSystemLib PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src/details"
)

target_link_libraries(LSystemLib PUBLIC glm::glm)

if(MSVC)
    target_compile_options(LSystemLib PRIVATE
        "$<$<COMPILE_LANGUAGE:CXX>:/external:W0>"
        "$<$<COMPILE_LANGUAGE:CXX>:/external:I${CMAKE_SOURCE_DIR}/third_party/glm>"
    )
endif()

if(LSYSTEM_BUILD_TESTS)
    add_subdirectory(tests)
endif()
