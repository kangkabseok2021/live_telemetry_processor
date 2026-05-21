cmake_minimum_required(VERSION 3.20)
project(live_telemetry_processor CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Threads REQUIRED)

# ── backend_lib — no Qt, no sockets dependency for tests ─────────────────────
add_library(backend_lib STATIC
    backend/TelemetryFrame.cpp
    backend/PacketParser.cpp
    backend/GncValidator.cpp
    backend/CoordTransform.cpp
    backend/TrajectoryPredictor.cpp
    backend/MetricStore.cpp
)
target_include_directories(backend_lib PUBLIC backend)
target_link_libraries(backend_lib PUBLIC Threads::Threads)
target_compile_options(backend_lib PRIVATE -Wall -Wextra -Wpedantic)

# ── telemetry_node — full executable (UDP + Qt UI) ─────────────────────────
if(CMAKE_SYSTEM_NAME STREQUAL "Linux" OR CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    find_package(Qt6 QUIET COMPONENTS Widgets)
    if(Qt6_FOUND)
        qt6_standard_project_setup()
        add_executable(telemetry_node
            backend/UdpReceiver.cpp
            ui/TrajectoryWidget.cpp
            ui/StatusPanel.cpp
            ui/MainWindow.cpp
            ui/main_ui.cpp
        )
        target_link_libraries(telemetry_node
            PRIVATE backend_lib Qt6::Widgets Threads::Threads)
        target_compile_options(telemetry_node PRIVATE -Wall -Wextra)
    else()
        message(STATUS "Qt6 not found — skipping telemetry_node UI target")
    endif()

    # Headless backend runner (no Qt)
    add_executable(telemetry_headless
        backend/UdpReceiver.cpp
        backend/main_headless.cpp
    )
    target_link_libraries(telemetry_headless PRIVATE backend_lib Threads::Threads)
    target_compile_options(telemetry_headless PRIVATE -Wall -Wextra)
endif()

# ── GoogleTests ───────────────────────────────────────────────────────────────
enable_testing()
if(BUILD_TESTING)
    include(FetchContent)
    FetchContent_Declare(
        googletest
        URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.tar.gz
        URL_HASH SHA256=8ad598c73ad796e0d8280b082cebd82a630d73e73cd3c70057938a6501bba5d7
        DOWNLOAD_EXTRACT_TIMESTAMP NEW
    )
    set(INSTALL_GTEST OFF)
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)

    add_executable(test_backend tests/test_backend.cpp)
    target_link_libraries(test_backend PRIVATE backend_lib GTest::gtest_main)
    target_compile_options(test_backend PRIVATE -Wall -Wextra)

    add_test(NAME backend_tests COMMAND test_backend)
endif()

# Medical Imaging State Machine (C++23 subfolder)
add_subdirectory(medical_imaging)
