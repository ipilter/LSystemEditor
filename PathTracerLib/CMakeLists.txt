set(PATHTRACER_LIB_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
set(PATHTRACER_LIB_INCLUDE_DIR "${PATHTRACER_LIB_DIR}/src")

set(PATHTRACER_LIB_SOURCES
    "${PATHTRACER_LIB_DIR}/src/PhysicalCamera.cpp"
    "${PATHTRACER_LIB_DIR}/src/EnvironmentMap.cpp"
    "${PATHTRACER_LIB_DIR}/src/MeshAccel/MeshBvhBuilder.cpp"
    "${PATHTRACER_LIB_DIR}/src/MeshAccel/MeshAccelScene.cpp"
    "${PATHTRACER_LIB_DIR}/src/MeshAccel/MeshAccelWavefrontExport.cpp"
    "${PATHTRACER_LIB_DIR}/src/MeshAccel/MeshSceneContent.cpp"
    "${PATHTRACER_LIB_DIR}/src/Spectral/SpectralState.cpp"
    "${PATHTRACER_LIB_DIR}/src/PathTracer.cpp")

set(PATHTRACER_CUDA_SOURCES
    "${PATHTRACER_LIB_DIR}/src/PathTracer.cu")

add_library(PathTracerLib STATIC
    ${PATHTRACER_LIB_SOURCES}
    ${PATHTRACER_CUDA_SOURCES})

file(GLOB_RECURSE PATHTRACER_LIB_HEADERS CONFIGURE_DEPENDS
    "${PATHTRACER_LIB_DIR}/src/*.h"
    "${PATHTRACER_LIB_DIR}/src/*.cuh")

target_sources(PathTracerLib PRIVATE ${PATHTRACER_LIB_HEADERS})
set_source_files_properties(${PATHTRACER_LIB_HEADERS} PROPERTIES HEADER_FILE_ONLY TRUE)

source_group(TREE "${PATHTRACER_LIB_DIR}" PREFIX "" FILES
    ${PATHTRACER_LIB_SOURCES}
    ${PATHTRACER_CUDA_SOURCES}
    ${PATHTRACER_LIB_HEADERS})

target_include_directories(PathTracerLib PUBLIC
    "${PATHTRACER_LIB_INCLUDE_DIR}"
    "${PATHTRACER_LIB_INCLUDE_DIR}/Geometry"
    "${PATHTRACER_LIB_INCLUDE_DIR}/MeshAccel"
    "${PATHTRACER_LIB_INCLUDE_DIR}/Brdf"
    "${PATHTRACER_LIB_INCLUDE_DIR}/Sampling"
    "${PATHTRACER_LIB_INCLUDE_DIR}/Spectral"
    "${PATHTRACER_LIB_INCLUDE_DIR}/Texture")
target_compile_definitions(PathTracerLib PRIVATE
    "PATHTRACER_RGB2SPEC_COEFF_PATH=\"${PATHTRACER_LIB_DIR}/src/Spectral/srgb.coeff\"")
option(PATHTRACER_ENABLE_PROFILE "Enable render pipeline profiling logs" ON)
if(PATHTRACER_ENABLE_PROFILE)
    target_compile_definitions(PathTracerLib PRIVATE PATHTRACER_PROFILE)
endif()
target_include_directories(PathTracerLib PRIVATE
    "${CMAKE_SOURCE_DIR}/Application/src")
target_link_libraries(PathTracerLib PUBLIC
    CUDA::cudart
    CUDA::curand
    stb_image
    Qt6::Core
    Qt6::Gui
    glm::glm)
set_target_properties(PathTracerLib PROPERTIES FOLDER "PathTracerLib")

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    message(FATAL_ERROR "PathTracerLib must be added via add_subdirectory from the project root.")
endif()

add_executable(path_tracer_budget_tests tests/PathTracerBudgetTests.cpp)
target_link_libraries(path_tracer_budget_tests PRIVATE PathTracerLib)
target_include_directories(path_tracer_budget_tests PRIVATE "${PATHTRACER_LIB_INCLUDE_DIR}")
set_target_properties(path_tracer_budget_tests PROPERTIES FOLDER "PathTracerLib/tests")
add_test(NAME PathTracerBudget COMMAND path_tracer_budget_tests)

add_executable(path_tracer_adaptive_sampling_tests tests/PathTracerAdaptiveSamplingTests.cpp)
target_link_libraries(path_tracer_adaptive_sampling_tests PRIVATE PathTracerLib)
target_include_directories(path_tracer_adaptive_sampling_tests PRIVATE "${PATHTRACER_LIB_INCLUDE_DIR}")
set_target_properties(path_tracer_adaptive_sampling_tests PROPERTIES FOLDER "PathTracerLib/tests")
add_test(NAME PathTracerAdaptiveSampling COMMAND path_tracer_adaptive_sampling_tests)

add_executable(path_tracer_preview_levels_tests tests/PathTracerPreviewLevelsTests.cpp)
target_link_libraries(path_tracer_preview_levels_tests PRIVATE PathTracerLib)
target_include_directories(path_tracer_preview_levels_tests PRIVATE "${PATHTRACER_LIB_INCLUDE_DIR}")
set_target_properties(path_tracer_preview_levels_tests PROPERTIES FOLDER "PathTracerLib/tests")
add_test(NAME PathTracerPreviewLevels COMMAND path_tracer_preview_levels_tests)

add_executable(path_tracer_exposure_tests tests/PathTracerExposureTests.cpp)
target_link_libraries(path_tracer_exposure_tests PRIVATE PathTracerLib)
target_include_directories(path_tracer_exposure_tests PRIVATE "${PATHTRACER_LIB_INCLUDE_DIR}")
set_target_properties(path_tracer_exposure_tests PROPERTIES FOLDER "PathTracerLib/tests")
add_test(NAME PathTracerExposure COMMAND path_tracer_exposure_tests)

add_executable(path_tracer_camera_direction_tests
    tests/PathTracerCameraDirectionTests.cpp
    "${CMAKE_SOURCE_DIR}/Application/src/AppLog.cpp")
target_link_libraries(path_tracer_camera_direction_tests PRIVATE PathTracerLib Qt6::Core Qt6::Gui glm::glm)
target_include_directories(path_tracer_camera_direction_tests PRIVATE
    "${PATHTRACER_LIB_INCLUDE_DIR}"
    "${CMAKE_SOURCE_DIR}/Application/src")
set_target_properties(path_tracer_camera_direction_tests PROPERTIES
    AUTOMOC ON
    FOLDER "PathTracerLib/tests")
add_test(NAME PathTracerCameraDirection COMMAND path_tracer_camera_direction_tests)

add_executable(path_tracer_camera_sampling_tests tests/CameraSamplingTests.cpp)
target_link_libraries(path_tracer_camera_sampling_tests PRIVATE PathTracerLib glm::glm)
target_include_directories(path_tracer_camera_sampling_tests PRIVATE "${PATHTRACER_LIB_INCLUDE_DIR}")
set_target_properties(path_tracer_camera_sampling_tests PROPERTIES FOLDER "PathTracerLib/tests")
add_test(NAME PathTracerCameraSampling COMMAND path_tracer_camera_sampling_tests)

add_executable(path_tracer_spectral_tests tests/PathTracerSpectralTests.cpp)
target_link_libraries(path_tracer_spectral_tests PRIVATE PathTracerLib)
target_compile_definitions(path_tracer_spectral_tests PRIVATE
    "PATHTRACER_RGB2SPEC_COEFF_PATH=\"${PATHTRACER_LIB_DIR}/src/Spectral/srgb.coeff\"")
target_include_directories(path_tracer_spectral_tests PRIVATE
    "${PATHTRACER_LIB_INCLUDE_DIR}"
    "${PATHTRACER_LIB_INCLUDE_DIR}/Spectral"
    "${PATHTRACER_LIB_INCLUDE_DIR}/Geometry")
set_target_properties(path_tracer_spectral_tests PROPERTIES FOLDER "PathTracerLib/tests")
add_test(NAME PathTracerSpectral COMMAND path_tracer_spectral_tests)

add_executable(path_tracer_principled_brdf_tests tests/PathTracerPrincipledBrdfTests.cpp)
target_link_libraries(path_tracer_principled_brdf_tests PRIVATE PathTracerLib)
target_include_directories(path_tracer_principled_brdf_tests PRIVATE
    "${PATHTRACER_LIB_INCLUDE_DIR}"
    "${PATHTRACER_LIB_INCLUDE_DIR}/Brdf"
    "${PATHTRACER_LIB_INCLUDE_DIR}/Geometry"
    "${PATHTRACER_LIB_INCLUDE_DIR}/MeshAccel")
set_target_properties(path_tracer_principled_brdf_tests PROPERTIES FOLDER "PathTracerLib/tests")
add_test(NAME PathTracerPrincipledBrdf COMMAND path_tracer_principled_brdf_tests)

add_executable(path_tracer_region_render_tests tests/PathTracerRegionRenderTests.cpp)
target_link_libraries(path_tracer_region_render_tests PRIVATE PathTracerLib)
target_include_directories(path_tracer_region_render_tests PRIVATE "${PATHTRACER_LIB_INCLUDE_DIR}")
set_target_properties(path_tracer_region_render_tests PROPERTIES FOLDER "PathTracerLib/tests")
add_test(NAME PathTracerRegionRender COMMAND path_tracer_region_render_tests)

add_executable(path_tracer_procedural_texture_tests tests/PathTracerProceduralTextureTests.cpp)
target_link_libraries(path_tracer_procedural_texture_tests PRIVATE PathTracerLib)
target_include_directories(path_tracer_procedural_texture_tests PRIVATE
    "${PATHTRACER_LIB_INCLUDE_DIR}"
    "${PATHTRACER_LIB_INCLUDE_DIR}/Texture"
    "${PATHTRACER_LIB_INCLUDE_DIR}/Geometry"
    "${PATHTRACER_LIB_INCLUDE_DIR}/MeshAccel")
set_target_properties(path_tracer_procedural_texture_tests PROPERTIES FOLDER "PathTracerLib/tests")
add_test(NAME PathTracerProceduralTexture COMMAND path_tracer_procedural_texture_tests)

add_executable(path_tracer_mesh_uv_tests tests/PathTracerMeshUvTests.cpp)
target_link_libraries(path_tracer_mesh_uv_tests PRIVATE PathTracerLib pathtracer_mesh_builder)
target_include_directories(path_tracer_mesh_uv_tests PRIVATE
    "${PATHTRACER_LIB_INCLUDE_DIR}"
    "${PATHTRACER_LIB_INCLUDE_DIR}/MeshAccel"
    "${CMAKE_SOURCE_DIR}/Application/src"
    "${CMAKE_SOURCE_DIR}/Application/src/MeshBuilder"
    "${CMAKE_SOURCE_DIR}/Application/src/Procedural")
set_target_properties(path_tracer_mesh_uv_tests PROPERTIES FOLDER "PathTracerLib/tests")
add_test(NAME PathTracerMeshUv COMMAND path_tracer_mesh_uv_tests)

add_executable(path_tracer_shadow_epsilon_tests tests/PathTracerShadowEpsilonTests.cpp)
target_link_libraries(path_tracer_shadow_epsilon_tests PRIVATE PathTracerLib)
target_include_directories(path_tracer_shadow_epsilon_tests PRIVATE
    "${PATHTRACER_LIB_INCLUDE_DIR}"
    "${PATHTRACER_LIB_INCLUDE_DIR}/MeshAccel"
    "${PATHTRACER_LIB_INCLUDE_DIR}/Geometry"
    "${PATHTRACER_LIB_INCLUDE_DIR}/Sampling")
set_target_properties(path_tracer_shadow_epsilon_tests PROPERTIES FOLDER "PathTracerLib/tests")
add_test(NAME PathTracerShadowEpsilon COMMAND path_tracer_shadow_epsilon_tests)

add_executable(path_tracer_scene_units_tests tests/PathTracerSceneUnitsTests.cpp)
target_link_libraries(path_tracer_scene_units_tests PRIVATE PathTracerLib)
target_include_directories(path_tracer_scene_units_tests PRIVATE
    "${PATHTRACER_LIB_INCLUDE_DIR}"
    "${PATHTRACER_LIB_INCLUDE_DIR}/MeshAccel"
    "${PATHTRACER_LIB_INCLUDE_DIR}/Geometry")
set_target_properties(path_tracer_scene_units_tests PROPERTIES FOLDER "PathTracerLib/tests")
add_test(NAME PathTracerSceneUnits COMMAND path_tracer_scene_units_tests)
