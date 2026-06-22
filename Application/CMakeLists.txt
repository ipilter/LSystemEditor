set(PATHTRACER_APPLICATION_DIR "${CMAKE_CURRENT_LIST_DIR}")
set(APPLICATION_DIR "${PATHTRACER_APPLICATION_DIR}")
set(APPLICATION_SRC_DIR "${PATHTRACER_APPLICATION_DIR}/src")

file(GLOB_RECURSE PATHTRACER_MESH_BUILDER_CPP CONFIGURE_DEPENDS
    "${APPLICATION_SRC_DIR}/MeshBuilder/*.cpp"
    "${APPLICATION_SRC_DIR}/Procedural/*.cpp")
file(GLOB_RECURSE PATHTRACER_MESH_BUILDER_HEADERS CONFIGURE_DEPENDS
    "${APPLICATION_SRC_DIR}/MeshBuilder/*.h"
    "${APPLICATION_SRC_DIR}/Procedural/*.h")

add_library(pathtracer_mesh_builder STATIC ${PATHTRACER_MESH_BUILDER_CPP})

target_sources(pathtracer_mesh_builder PRIVATE ${PATHTRACER_MESH_BUILDER_HEADERS})
set_source_files_properties(${PATHTRACER_MESH_BUILDER_HEADERS} PROPERTIES HEADER_FILE_ONLY TRUE)

source_group(TREE "${APPLICATION_DIR}" PREFIX "" FILES
    ${PATHTRACER_MESH_BUILDER_CPP}
    ${PATHTRACER_MESH_BUILDER_HEADERS})

target_sources(pathtracer_mesh_builder PRIVATE "${APPLICATION_DIR}/CMakeLists.txt")
set_source_files_properties("${APPLICATION_DIR}/CMakeLists.txt" PROPERTIES HEADER_FILE_ONLY TRUE)
source_group("" FILES "${APPLICATION_DIR}/CMakeLists.txt")

target_include_directories(pathtracer_mesh_builder PUBLIC
    "${APPLICATION_SRC_DIR}"
    "${APPLICATION_SRC_DIR}/MeshBuilder"
    "${APPLICATION_SRC_DIR}/Procedural"
    "${APPLICATION_SRC_DIR}/Model"
    "${CMAKE_SOURCE_DIR}/PathTracerLib/src"
    "${CMAKE_SOURCE_DIR}/PathTracerLib/src/Geometry"
    "${CMAKE_SOURCE_DIR}/PathTracerLib/src/MeshAccel")
target_link_libraries(pathtracer_mesh_builder PUBLIC manifold LSystemLib)
set_target_properties(pathtracer_mesh_builder PROPERTIES FOLDER "Application")

function(application_add_pathtracer_executable)
    set(_application_dir "${CMAKE_SOURCE_DIR}/Application")
    set(_application_src_dir "${_application_dir}/src")

    if(NOT TARGET PathTracerLib)
        message(FATAL_ERROR "PathTracerLib must be configured before creating the pathtracer executable.")
    endif()

    file(GLOB_RECURSE APPLICATION_APP_CPP CONFIGURE_DEPENDS
        "${_application_src_dir}/*.cpp")
    file(GLOB_RECURSE APPLICATION_APP_HEADERS CONFIGURE_DEPENDS
        "${_application_src_dir}/*.h")

    list(FILTER APPLICATION_APP_CPP EXCLUDE REGEX ".*/MeshBuilder/.*")
    list(FILTER APPLICATION_APP_CPP EXCLUDE REGEX ".*/Procedural/.*")
    list(FILTER APPLICATION_APP_HEADERS EXCLUDE REGEX ".*/MeshBuilder/.*")
    list(FILTER APPLICATION_APP_HEADERS EXCLUDE REGEX ".*/Procedural/.*")

    file(GLOB_RECURSE APPLICATION_MESH_BUILDER_CPP CONFIGURE_DEPENDS
        "${_application_src_dir}/MeshBuilder/*.cpp"
        "${_application_src_dir}/Procedural/*.cpp")
    file(GLOB_RECURSE APPLICATION_MESH_BUILDER_HEADERS CONFIGURE_DEPENDS
        "${_application_src_dir}/MeshBuilder/*.h"
        "${_application_src_dir}/Procedural/*.h")

    add_executable(pathtracer WIN32 ${APPLICATION_APP_CPP})

    target_sources(pathtracer PRIVATE ${APPLICATION_APP_HEADERS})
    set_source_files_properties(${APPLICATION_APP_HEADERS} PROPERTIES HEADER_FILE_ONLY TRUE)

    # Mirror MeshBuilder/Procedural files in the executable project for Solution Explorer.
    target_sources(pathtracer PRIVATE
        ${APPLICATION_MESH_BUILDER_CPP}
        ${APPLICATION_MESH_BUILDER_HEADERS})
    set_source_files_properties(
        ${APPLICATION_MESH_BUILDER_CPP}
        ${APPLICATION_MESH_BUILDER_HEADERS}
        PROPERTIES EXCLUDE_FROM_BUILD TRUE)
    set_source_files_properties(
        ${APPLICATION_MESH_BUILDER_HEADERS}
        PROPERTIES HEADER_FILE_ONLY TRUE)

    target_include_directories(pathtracer PRIVATE
        "${_application_src_dir}"
        "${_application_src_dir}/View"
        "${_application_src_dir}/Controller"
        "${_application_src_dir}/Model"
        "${CMAKE_SOURCE_DIR}/PathTracerLib/src"
        "${CMAKE_SOURCE_DIR}/PathTracerLib/src/Sampling"
        "${CMAKE_SOURCE_DIR}/PathTracerLib/src/Geometry"
        "${CMAKE_SOURCE_DIR}/PathTracerLib/src/MeshAccel")
    target_link_libraries(pathtracer PRIVATE
        PathTracerLib
        pathtracer_mesh_builder
        Qt6::Widgets
        Qt6::OpenGL
        Qt6::OpenGLWidgets
        Qt6::Concurrent
        CUDA::cudart
        glm::glm)
    set_target_properties(pathtracer PROPERTIES
        OUTPUT_NAME pathtracer
        AUTOMOC ON
        FOLDER "Application")

    source_group(TREE "${_application_dir}" PREFIX "" FILES
        ${APPLICATION_APP_CPP}
        ${APPLICATION_APP_HEADERS}
        ${APPLICATION_MESH_BUILDER_CPP}
        ${APPLICATION_MESH_BUILDER_HEADERS})

    target_sources(pathtracer PRIVATE
        "${CMAKE_SOURCE_DIR}/CMakeLists.txt"
        "${_application_dir}/CMakeLists.txt")
    set_source_files_properties(
        "${CMAKE_SOURCE_DIR}/CMakeLists.txt"
        "${_application_dir}/CMakeLists.txt"
        PROPERTIES HEADER_FILE_ONLY TRUE)
    source_group("" FILES
        "${CMAKE_SOURCE_DIR}/CMakeLists.txt"
        "${_application_dir}/CMakeLists.txt")

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
endfunction()

function(application_add_pathtracer_tests)
    set(_application_dir "${CMAKE_SOURCE_DIR}/Application")
    set(_application_src_dir "${_application_dir}/src")

    if(NOT TARGET PathTracerLib)
        message(FATAL_ERROR "PathTracerLib must be configured before creating application tests.")
    endif()

    set(MESH_ACCEL_TEST_SOURCES
        "${CMAKE_SOURCE_DIR}/PathTracerLib/tests/MeshAccelTests.cpp"
        "${_application_dir}/tests/ProceduralMeshTests.cpp")

    add_executable(mesh_accel_tests ${MESH_ACCEL_TEST_SOURCES})
    target_link_libraries(mesh_accel_tests PRIVATE PathTracerLib pathtracer_mesh_builder Qt6::Gui)
    target_include_directories(mesh_accel_tests PRIVATE
        "${CMAKE_SOURCE_DIR}/PathTracerLib/src"
        "${CMAKE_SOURCE_DIR}/PathTracerLib/src/MeshAccel"
        "${_application_src_dir}"
        "${_application_src_dir}/MeshBuilder"
        "${_application_src_dir}/Procedural"
        "${CMAKE_SOURCE_DIR}/PathTracerLib/src/Geometry"
        "${_application_src_dir}/Model"
        "${CMAKE_SOURCE_DIR}/PathTracerLib/tests")
    set_target_properties(mesh_accel_tests PROPERTIES FOLDER "Application/tests")

    source_group(TREE "${CMAKE_SOURCE_DIR}/PathTracerLib" PREFIX "PathTracerLib" FILES
        "${CMAKE_SOURCE_DIR}/PathTracerLib/tests/MeshAccelTests.cpp")
    source_group(TREE "${_application_dir}" PREFIX "" FILES
        "${_application_dir}/tests/ProceduralMeshTests.cpp")

    target_sources(mesh_accel_tests PRIVATE "${_application_dir}/CMakeLists.txt")
    set_source_files_properties("${_application_dir}/CMakeLists.txt" PROPERTIES HEADER_FILE_ONLY TRUE)
    source_group("" FILES "${_application_dir}/CMakeLists.txt")

    add_test(NAME MeshAccel COMMAND mesh_accel_tests)
endfunction()
