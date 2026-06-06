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

find_package(Qt6 REQUIRED COMPONENTS Gui Widgets OpenGL OpenGLWidgets)
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

add_subdirectory(src/LSystemLib)

set(MANIFOLD_TEST OFF CACHE BOOL "" FORCE)
set(MANIFOLD_PYBIND OFF CACHE BOOL "" FORCE)
set(MANIFOLD_JSBIND OFF CACHE BOOL "" FORCE)
set(MANIFOLD_CBIND OFF CACHE BOOL "" FORCE)
set(MANIFOLD_CROSS_SECTION ON CACHE BOOL "" FORCE)
set(MANIFOLD_PAR ON CACHE BOOL "" FORCE)
set(MANIFOLD_DOWNLOADS ON CACHE BOOL "" FORCE)

include(FetchContent)
FetchContent_Declare(manifold
    GIT_REPOSITORY https://github.com/elalish/manifold.git
    GIT_TAG        v3.0.1
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(manifold)

FetchContent_Declare(stb
    GIT_REPOSITORY https://github.com/nothings/stb.git
    GIT_TAG        master
    GIT_SHALLOW    TRUE)
FetchContent_GetProperties(stb)
if(NOT stb_POPULATED)
    FetchContent_Populate(stb)
endif()
add_library(stb_image INTERFACE)
target_include_directories(stb_image INTERFACE ${stb_SOURCE_DIR})

file(GLOB_RECURSE PATHTRACER_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cu"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.hpp")

set(PATHTRACER_SAMPLING_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Renderer/Sampling/SobolDirectionData.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Renderer/Sampling/SobolTables.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Renderer/Sampling/QmcGpuResources.cpp")

set(PATHTRACER_MESH_BUILDER_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Renderer/MeshBuilder/ManifoldMeshConvert.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Renderer/Procedural/Turtle.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Renderer/Procedural/Spline.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Renderer/Procedural/Loft.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Renderer/Procedural/ProceduralAssembly.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Renderer/Procedural/ProceduralMeshBuilder.cpp")

set(PATHTRACER_MESH_ACCEL_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Renderer/MeshAccel/MeshBvhBuilder.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Renderer/MeshAccel/MeshAccelScene.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Renderer/MeshAccel/MeshSceneContent.cpp")

list(FILTER PATHTRACER_SOURCES EXCLUDE REGEX ".*/Renderer/Sampling/(SobolDirectionData|SobolTables|QmcGpuResources)\\.cpp$")
list(FILTER PATHTRACER_SOURCES EXCLUDE REGEX ".*/Renderer/MeshBuilder/ManifoldMeshConvert\\.cpp$")
list(FILTER PATHTRACER_SOURCES EXCLUDE REGEX ".*/Renderer/Procedural/.*\\.cpp$")
list(FILTER PATHTRACER_SOURCES EXCLUDE REGEX ".*/Renderer/MeshAccel/(MeshBvhBuilder|MeshAccelScene|MeshSceneContent)\\.cpp$")
list(FILTER PATHTRACER_SOURCES EXCLUDE REGEX ".*/View/AddSdfDialog\\.cpp$")
list(FILTER PATHTRACER_SOURCES EXCLUDE REGEX ".*/Renderer/Sdf/.*")
list(FILTER PATHTRACER_SOURCES EXCLUDE REGEX ".*/LSystemLib/.*")

add_library(pathtracer_sampling STATIC ${PATHTRACER_SAMPLING_SOURCES})
target_include_directories(pathtracer_sampling PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Renderer"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Renderer/Sampling")
target_link_libraries(pathtracer_sampling PUBLIC CUDA::cudart Qt6::Core)
set_target_properties(pathtracer_sampling PROPERTIES FOLDER "src")

add_library(pathtracer_mesh_builder STATIC ${PATHTRACER_MESH_BUILDER_SOURCES})
target_include_directories(pathtracer_mesh_builder PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Renderer"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Renderer/MeshBuilder"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Renderer/Procedural"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Renderer/Geometry"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Model")
target_link_libraries(pathtracer_mesh_builder PUBLIC manifold LSystemLib)
set_target_properties(pathtracer_mesh_builder PROPERTIES FOLDER "src")

add_library(pathtracer_mesh_accel STATIC ${PATHTRACER_MESH_ACCEL_SOURCES})
target_include_directories(pathtracer_mesh_accel PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Renderer"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Renderer/MeshAccel"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Renderer/MeshBuilder"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Renderer/Procedural"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Renderer/Geometry"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Model")
target_link_libraries(pathtracer_mesh_accel PUBLIC pathtracer_mesh_builder CUDA::cudart)
set_target_properties(pathtracer_mesh_accel PROPERTIES FOLDER "src")

add_executable(pathtracer WIN32 ${PATHTRACER_SOURCES})
target_include_directories(pathtracer PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Application"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Renderer"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Renderer/Sampling"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Renderer/Geometry"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Renderer/MeshBuilder"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Renderer/Procedural"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Renderer/MeshAccel"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/View"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Controller"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Model")
target_link_libraries(pathtracer PRIVATE
    pathtracer_sampling
    pathtracer_mesh_builder
    pathtracer_mesh_accel
    stb_image
    Qt6::Widgets
    Qt6::OpenGL
    Qt6::OpenGLWidgets
    CUDA::cudart
    glm::glm)
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

enable_testing()

add_executable(qmc_sampling_tests tests/QmcSamplerTests.cpp)
target_link_libraries(qmc_sampling_tests PRIVATE pathtracer_sampling)
target_include_directories(qmc_sampling_tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Renderer/Sampling"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests")
set_target_properties(qmc_sampling_tests PROPERTIES FOLDER "tests")

add_test(NAME QmcSampling COMMAND qmc_sampling_tests)

add_executable(mesh_accel_tests tests/MeshAccelTests.cpp tests/ProceduralMeshTests.cpp)
target_link_libraries(mesh_accel_tests PRIVATE pathtracer_mesh_accel Qt6::Gui)
target_include_directories(mesh_accel_tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Renderer/MeshAccel"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Renderer/MeshBuilder"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Renderer/Procedural"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Renderer/Geometry"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Model"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests")
set_target_properties(mesh_accel_tests PROPERTIES FOLDER "tests")

add_test(NAME MeshAccel COMMAND mesh_accel_tests)

add_executable(path_tracer_budget_tests tests/PathTracerBudgetTests.cpp)
target_include_directories(path_tracer_budget_tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Renderer")
set_target_properties(path_tracer_budget_tests PROPERTIES FOLDER "tests")

add_test(NAME PathTracerBudget COMMAND path_tracer_budget_tests)

add_executable(bsdf_core_tests tests/BsdfCoreTests.cpp)
target_include_directories(bsdf_core_tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Renderer"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Renderer/Sampling"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Renderer/Geometry"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Renderer/MeshAccel")
set_target_properties(bsdf_core_tests PROPERTIES FOLDER "tests")

add_test(NAME BsdfCore COMMAND bsdf_core_tests)

add_executable(path_integrator_tests tests/PathIntegratorTests.cpp)
target_include_directories(path_integrator_tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Renderer"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Renderer/Sampling"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Renderer/Geometry"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Renderer/MeshAccel")
set_target_properties(path_integrator_tests PROPERTIES FOLDER "tests")

add_test(NAME PathIntegrator COMMAND path_integrator_tests)
