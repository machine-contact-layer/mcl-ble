cmake_minimum_required(VERSION 3.16)
project(mcl_ble LANGUAGES C)

option(MCL_BLE_BUILD_TESTS "Build host-side reference tests" ON)

set(MCL_LINK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../mcl-link" CACHE PATH "Path to mcl-link repository")

if(NOT EXISTS "${MCL_LINK_DIR}/include/mcl/link.h")
    message(FATAL_ERROR
        "mcl-ble requires mcl-link. Set MCL_LINK_DIR to the mcl-link checkout.")
endif()

# The binding carries MCL Link frames, so it depends on Link and on nothing else.
# There are deliberately no sockets here: how bytes reach the network is the
# integrator's decision, and a binding must not become a network stack.
add_library(mcl_ble STATIC
    src/ble_binding.c
    "${MCL_LINK_DIR}/src/link.c")
target_include_directories(mcl_ble PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    "${MCL_LINK_DIR}/include")
set_target_properties(mcl_ble PROPERTIES
    C_STANDARD 99
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS NO)

if(MCL_BLE_BUILD_TESTS)
    enable_testing()

    add_executable(mcl_ble_test_binding tests/test_ble_binding.c)
    target_link_libraries(mcl_ble_test_binding PRIVATE mcl_ble)
    set_target_properties(mcl_ble_test_binding PROPERTIES
        C_STANDARD 99
        C_STANDARD_REQUIRED YES
        C_EXTENSIONS NO)
    add_test(NAME mcl_ble_binding COMMAND mcl_ble_test_binding)
endif()
