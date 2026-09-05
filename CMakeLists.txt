# iphone-1 — booting the genuine iPhone OS 1.0 (1A543a) on an emulated iPhone1,1.
cmake_minimum_required(VERSION 3.16)
project(iphone1 C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

option(IPHONE1_WEB "Build the Emscripten browser frontend" OFF)
if(NOT IPHONE1_WEB)
    set(CMAKE_C_FLAGS_RELEASE "-O3 -DNDEBUG" CACHE STRING "" FORCE)
    include(CheckIPOSupported)
    check_ipo_supported(RESULT ipo_supported OUTPUT ipo_error LANGUAGES C)
    if(ipo_supported)
        set(CMAKE_INTERPROCEDURAL_OPTIMIZATION ON)
    endif()
endif()

enable_testing()
add_subdirectory(core)
if(IPHONE1_WEB)
    add_executable(emu web/emu_api.c)
    target_link_libraries(emu PRIVATE emucore)
    target_compile_options(emu PRIVATE -O3)
    target_link_options(emu PRIVATE
        -O3
        -sALLOW_MEMORY_GROWTH=1
        -sINITIAL_MEMORY=536870912
        -sMODULARIZE=1
        -sEXPORT_ES6=0
        -sEXPORT_NAME=createEmu
        "-sEXPORTED_FUNCTIONS=['_emu_init','_emu_error','_emu_load_snapshot','_emu_run','_emu_status','_emu_frame','_emu_touch','_emu_button','_emu_set_cpi','_emu_cycles','_emu_serial_read','_malloc','_free']"
        "-sEXPORTED_RUNTIME_METHODS=['ccall','cwrap','HEAPU8','HEAPU32']"
    )
    set_target_properties(emu PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/web/dist"
    )
else()
    add_subdirectory(tools)
endif()
