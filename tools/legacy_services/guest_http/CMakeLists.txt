add_executable(test_guest_http test_http.c http.c)
add_test(NAME test_guest_http COMMAND test_guest_http)

add_executable(test_guest_http_server test_server.c http.c)
add_executable(test_guest_http_server_diagnostic test_server.c http.c)
target_compile_definitions(test_guest_http_server_diagnostic PRIVATE OWN_HTTP_DIAGNOSTICS=1)
add_test(NAME test_guest_http_lifecycle_logging
    COMMAND "${CMAKE_COMMAND}"
        "-DQUIET=$<TARGET_FILE:test_guest_http_server>"
        "-DDIAGNOSTIC=$<TARGET_FILE:test_guest_http_server_diagnostic>"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/compare-logging.cmake")
