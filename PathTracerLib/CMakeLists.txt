set(PATHTRACER_LIB_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
set(PATHTRACER_LIB_INCLUDE_DIR "${PATHTRACER_LIB_DIR}/src")

set(PATHTRACER_LIB_SOURCES
    "${PATHTRACER_LIB_DIR}/src/PhysicalCamera.cpp"
    "${PATHTRACER_LIB_DIR}/src/EnvironmentMap.cpp"
    "${PATHTRACER_LIB_DIR}/src/MeshAccel/MeshBvhBuilder.cpp"
    "${PATHTRACER_LIB_DIR}/src/MeshAccel/MeshAccelScene.cpp"
    "${PATHTRACER_LIB_DIR}/src/MeshAccel/MeshSceneContent.cpp"
    "${PATHTRACER_LIB_DIR}/src/PathTracer.cpp")

set(PATHTRACER_CUDA_SOURCES
    "${PATHTRACER_LIB_DIR}/src/PathTracer.cu")

add_library(PathTracerLib STATIC
    ${PATHTRACER_LIB_SOURCES}
    ${PATHTRACER_CUDA_SOURCES})
target_include_directories(PathTracerLib PUBLIC
    "${PATHTRACER_LIB_INCLUDE_DIR}"
    "${PATHTRACER_LIB_INCLUDE_DIR}/Geometry"
    "${PATHTRACER_LIB_INCLUDE_DIR}/MeshAccel"
    "${PATHTRACER_LIB_INCLUDE_DIR}/Brdf"
    "${PATHTRACER_LIB_INCLUDE_DIR}/Sampling")
target_include_directories(PathTracerLib PRIVATE
    "${CMAKE_SOURCE_DIR}/Application/src"
    "${CMAKE_SOURCE_DIR}/Application/src/MeshBuilder"
    "${CMAKE_SOURCE_DIR}/Application/src/Procedural")
target_link_libraries(PathTracerLib PUBLIC
    pathtracer_mesh_builder
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

add_executable(path_tracer_exposure_tests tests/PathTracerExposureTests.cpp)
target_link_libraries(path_tracer_exposure_tests PRIVATE PathTracerLib)
target_include_directories(path_tracer_exposure_tests PRIVATE "${PATHTRACER_LIB_INCLUDE_DIR}")
set_target_properties(path_tracer_exposure_tests PROPERTIES FOLDER "PathTracerLib/tests")
add_test(NAME PathTracerExposure COMMAND path_tracer_exposure_tests)

add_executable(path_tracer_camera_direction_tests
    tests/PathTracerCameraDirectionTests.cpp
    "${CMAKE_SOURCE_DIR}/Application/src/AppLog.cpp")
target_link_libraries(path_tracer_camera_direction_tests PRIVATE
    PathTracerLib Qt6::Gui Qt6::OpenGL glm::glm)
target_include_directories(path_tracer_camera_direction_tests PRIVATE
    "${PATHTRACER_LIB_INCLUDE_DIR}"
    "${CMAKE_SOURCE_DIR}/Application/src")
set_target_properties(path_tracer_camera_direction_tests PROPERTIES
    AUTOMOC ON
    FOLDER "PathTracerLib/tests")
add_test(NAME PathTracerCameraDirection COMMAND path_tracer_camera_direction_tests)
