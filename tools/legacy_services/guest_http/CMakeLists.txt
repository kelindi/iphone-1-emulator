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

add_executable(test_guest_weather test_weather.c weather.c)
add_test(NAME test_guest_weather COMMAND test_guest_weather)

add_executable(test_guest_dns_record_probe test_dns_record_probe.c)
add_test(NAME test_guest_dns_record_probe COMMAND test_guest_dns_record_probe)

add_executable(test_guest_standalone_runtime test_standalone_runtime.c standalone_runtime.c)
target_compile_definitions(test_guest_standalone_runtime PRIVATE OWN_STANDALONE_ENVIRON=own_test_environ)
add_test(NAME test_guest_standalone_runtime COMMAND test_guest_standalone_runtime)

set(guest_gateway_test_sources test_weather_server.c weather_http.c
    weather_request.c weather.c stocks.c http.c dns_packet.c youtube_auth.c youtube_catalog.c)
add_executable(test_guest_gateway_server ${guest_gateway_test_sources})
add_executable(test_guest_gateway_server_diagnostic ${guest_gateway_test_sources})
target_compile_definitions(test_guest_gateway_server_diagnostic PRIVATE OWN_HTTP_DIAGNOSTICS=1)
add_test(NAME test_guest_gateway_lifecycle_logging
    COMMAND "${CMAKE_COMMAND}"
        "-DQUIET=$<TARGET_FILE:test_guest_gateway_server>"
        "-DDIAGNOSTIC=$<TARGET_FILE:test_guest_gateway_server_diagnostic>"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/compare-logging.cmake")

find_package(Python3 COMPONENTS Interpreter)
if(Python3_Interpreter_FOUND)
    foreach(contract weather_request weather_http stocks dns_packet build_contract youtube_auth youtube_catalog)
        add_test(NAME test_guest_${contract}
            COMMAND "${Python3_EXECUTABLE}" -m unittest discover
                -s "${CMAKE_CURRENT_SOURCE_DIR}" -p "test_${contract}.py")
    endforeach()
endif()
