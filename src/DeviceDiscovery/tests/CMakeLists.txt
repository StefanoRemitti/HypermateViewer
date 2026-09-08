set(DEVDISC_TESTS
  test_interface_discovery
  test_ssh_detector
  test_ifconfig_parser
  test_thread_pool
  test_port_scanner
)

foreach(test_name IN LISTS DEVDISC_TESTS)
  add_executable(${test_name} ${test_name}.cpp)
  target_link_libraries(${test_name} PRIVATE devdisc_core)
  target_include_directories(${test_name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
  target_compile_options(${test_name} PRIVATE -Wall -Wextra -Wpedantic)
  add_test(NAME ${test_name} COMMAND ${test_name})
endforeach()
