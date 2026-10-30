cmake_minimum_required(VERSION 3.5)
project(digital_forensics_simulator LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(forensics_core
    src/simulator.cpp
)
target_include_directories(forensics_core PUBLIC include)

add_library(forensics_http
    src/http_server.cpp
)
target_include_directories(forensics_http PUBLIC include)
target_link_libraries(forensics_http PUBLIC forensics_core)

if(WIN32)
    target_link_libraries(forensics_http PUBLIC ws2_32)
endif()

add_executable(forensics_simulator
    app/main.cpp
)
target_link_libraries(forensics_simulator PRIVATE forensics_http)

include(CTest)
if(BUILD_TESTING)
    add_executable(forensics_tests
        tests/simulator_tests.cpp
    )
    target_link_libraries(forensics_tests PRIVATE forensics_core)
    add_test(NAME forensics_tests COMMAND forensics_tests)
endif()
