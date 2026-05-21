cmake_minimum_required(VERSION 3.20)
project(medical_imaging CXX C)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Threads REQUIRED)

# ── imaging_core static library ───────────────────────────────────────────
add_library(imaging_core STATIC
    src/StateMachine.cpp
    src/TransitionLog.cpp
    src/ImagePipeline.cpp
    src/RtEngine.cpp
)
target_include_directories(imaging_core PUBLIC include)
target_link_libraries(imaging_core PUBLIC Threads::Threads)
target_compile_options(imaging_core PRIVATE -Wall -Wextra -Wpedantic)
# Required so imaging_core.a can be linked into imaging_engine.so
set_target_properties(imaging_core PROPERTIES POSITION_INDEPENDENT_CODE ON)

# ── shared library for C# P/Invoke ───────────────────────────────────────
add_library(imaging_engine SHARED src/c_api.cpp)
target_link_libraries(imaging_engine PRIVATE imaging_core)

# ── demo executable ───────────────────────────────────────────────────────
add_executable(imaging_demo src/main.cpp)
target_link_libraries(imaging_demo PRIVATE imaging_core)

# ── GoogleTests ───────────────────────────────────────────────────────────
if(BUILD_TESTING)
    include(FetchContent)
    FetchContent_Declare(googletest
        URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.tar.gz
        URL_HASH SHA256=8ad598c73ad796e0d8280b082cebd82a630d73e73cd3c70057938a6501bba5d7
        DOWNLOAD_EXTRACT_TIMESTAMP NEW)
    set(INSTALL_GTEST OFF)
    FetchContent_MakeAvailable(googletest)

    add_executable(test_imaging tests/test_state_machine.cpp)
    target_link_libraries(test_imaging PRIVATE imaging_core GTest::gtest_main)
    target_compile_options(test_imaging PRIVATE -Wall -Wextra)
    add_test(NAME imaging_tests COMMAND test_imaging)
    enable_testing()
endif()
