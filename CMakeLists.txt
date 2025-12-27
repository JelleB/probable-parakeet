cmake_minimum_required(VERSION 3.20)

project(audio_engine_demo LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Threads REQUIRED)

add_library(audio_engine
  AudioEngine.cpp
)
target_include_directories(audio_engine PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(audio_engine PUBLIC Threads::Threads)

add_executable(example
  main.cpp
)
target_link_libraries(example PRIVATE audio_engine)

option(BUILD_TESTS "Build unit tests" ON)
if (BUILD_TESTS)
  include(CTest)
  include(FetchContent)

  # Doctest is a small, header-only C++ test framework.
  FetchContent_Declare(
    doctest
    GIT_REPOSITORY https://github.com/doctest/doctest.git
    GIT_TAG v2.4.11
  )
  FetchContent_MakeAvailable(doctest)

  add_executable(unit_tests
    tests/test_main.cpp
    tests/test_logbins.cpp
    tests/test_audioengine_centers.cpp
  )
  target_link_libraries(unit_tests PRIVATE audio_engine doctest::doctest)

  add_test(NAME unit_tests COMMAND unit_tests)
endif()

