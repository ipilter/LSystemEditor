cmake_minimum_required(VERSION 3.24)
project(PathTracer LANGUAGES CXX CUDA)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set_property(GLOBAL PROPERTY USE_FOLDERS ON)
set_property(GLOBAL PROPERTY PREDEFINED_TARGETS_FOLDER "CMake")
set_property(GLOBAL PROPERTY AUTOMOC_SOURCE_GROUP "QtMocs")

if(CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_CONFIGURATION_TYPES "Debug;Release" CACHE STRING "" FORCE)
endif()

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
if(CMAKE_CONFIGURATION_TYPES)
    foreach(CONFIG ${CMAKE_CONFIGURATION_TYPES})
        string(TOUPPER ${CONFIG} CONFIG_UPPER)
        set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${CONFIG_UPPER} "${CMAKE_BINARY_DIR}/bin/${CONFIG}")
    endforeach()
else()
    foreach(CONFIG DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
        set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${CONFIG} "${CMAKE_BINARY_DIR}/bin/${CONFIG}")
    endforeach()
endif()

if(DEFINED ENV{QTDIR})
    list(PREPEND CMAKE_PREFIX_PATH "$ENV{QTDIR}")
endif()

find_package(Qt6 REQUIRED COMPONENTS Widgets OpenGL OpenGLWidgets)
find_package(CUDAToolkit REQUIRED)

enable_language(CUDA)

find_package(glm QUIET CONFIG)
if(NOT glm_FOUND)
    include(FetchContent)
    FetchContent_Declare(glm
        GIT_REPOSITORY https://github.com/g-truc/glm.git
        GIT_TAG        1.0.1
        GIT_SHALLOW    TRUE)
    if(POLICY CMP0169)
        cmake_policy(SET CMP0169 OLD)
    endif()
    FetchContent_GetProperties(glm)
    if(NOT glm_POPULATED)
        FetchContent_Populate(glm)
    endif()
    add_library(glm INTERFACE)
    add_library(glm::glm ALIAS glm)
    target_include_directories(glm INTERFACE ${glm_SOURCE_DIR})
endif()

file(GLOB_RECURSE PATHTRACER_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cu"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.hpp")

add_executable(pathtracer WIN32 ${PATHTRACER_SOURCES})
target_include_directories(pathtracer PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Application"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Renderer"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/View"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Controller"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Model")
target_link_libraries(pathtracer PRIVATE Qt6::Widgets Qt6::OpenGL Qt6::OpenGLWidgets CUDA::cudart CUDA::curand glm::glm)
set_target_properties(pathtracer PROPERTIES
    OUTPUT_NAME pathtracer
    AUTOMOC ON
    FOLDER "src")

source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}/src" PREFIX "" FILES ${PATHTRACER_SOURCES})

target_sources(pathtracer PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/CMakeLists.txt")
set_source_files_properties("${CMAKE_CURRENT_SOURCE_DIR}/CMakeLists.txt" PROPERTIES HEADER_FILE_ONLY TRUE)
source_group("" FILES "${CMAKE_CURRENT_SOURCE_DIR}/CMakeLists.txt")

find_program(WINDEPLOYQT_EXECUTABLE windeployqt
    HINTS "$ENV{QTDIR}/bin"
    DOC "Qt deployment tool")

if(WIN32 AND WINDEPLOYQT_EXECUTABLE)
    add_custom_command(TARGET pathtracer POST_BUILD
        COMMAND PowerShell -NoProfile -ExecutionPolicy Bypass -Command
            "if (-not (Test-Path '$<TARGET_FILE_DIR:pathtracer>/Qt6OpenGLWidgetsd.dll')) { & '${WINDEPLOYQT_EXECUTABLE}' --no-translations --no-opengl-sw '$<TARGET_FILE:pathtracer>' }"
        COMMENT "Deploying Qt debug redistributables if needed"
        CONFIG Debug)
    add_custom_command(TARGET pathtracer POST_BUILD
        COMMAND PowerShell -NoProfile -ExecutionPolicy Bypass -Command
            "if (-not (Test-Path '$<TARGET_FILE_DIR:pathtracer>/Qt6OpenGLWidgets.dll')) { & '${WINDEPLOYQT_EXECUTABLE}' --no-translations --no-compiler-runtime --no-opengl-sw '$<TARGET_FILE:pathtracer>' }"
        COMMENT "Deploying Qt release redistributables if needed"
        CONFIG Release)
endif()

if(WIN32)
    set(CUDA_RUNTIME_DLL "${CUDAToolkit_BIN_DIR}/x64/cudart64_13.dll")
    if(NOT EXISTS "${CUDA_RUNTIME_DLL}")
        set(CUDA_RUNTIME_DLL "${CUDAToolkit_BIN_DIR}/cudart64_13.dll")
    endif()
    add_custom_command(TARGET pathtracer POST_BUILD
        COMMAND PowerShell -NoProfile -ExecutionPolicy Bypass -Command
            "if (-not (Test-Path '$<TARGET_FILE_DIR:pathtracer>/cudart64_13.dll')) { if (Test-Path '${CUDA_RUNTIME_DLL}') { Copy-Item -Force '${CUDA_RUNTIME_DLL}' '$<TARGET_FILE_DIR:pathtracer>/cudart64_13.dll' } else { Write-Warning 'CUDA runtime DLL not found at ${CUDA_RUNTIME_DLL}' } }"
        COMMENT "Copying CUDA runtime if needed"
    )
endif()
