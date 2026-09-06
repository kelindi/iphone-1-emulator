# iphone-1 — booting the genuine iPhone OS 1.0 (1A543a) on an emulated iPhone1,1.
cmake_minimum_required(VERSION 3.16)
project(iphone1 C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

option(IPHONE1_WEB "Build the Emscripten browser frontend" OFF)
option(IPHONE1_WASM_JIT "Build the guarded WebAssembly execution accelerator" ON)
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
    set(IPHONE1_WEB_DIST "${CMAKE_SOURCE_DIR}/web/dist" CACHE PATH
        "WebAssembly output directory (isolated comparisons can build target emu here)")
    # Emscripten's classic-script factory is CommonJS under Node, while the
    # browser controller uses ES modules. Keep the generated smoke-test import
    # independent of the repository's package.json module type.
    file(GENERATE OUTPUT "${IPHONE1_WEB_DIST}/package.json"
         CONTENT "{\"type\":\"commonjs\"}\n")
    add_executable(emu web/emu_api.c)
    target_link_libraries(emu PRIVATE emucore)
    if(IPHONE1_WASM_JIT)
        target_sources(emu PRIVATE web/jit-runtime.c)
        target_compile_definitions(emu PRIVATE S5LBOX_WASM_JIT=1)
        target_compile_definitions(emucore PRIVATE S5LBOX_WASM_JIT=1)
        target_link_options(emu PRIVATE -sALLOW_TABLE_GROWTH=1)
    endif()
    target_compile_options(emu PRIVATE -O3)
    target_link_options(emu PRIVATE
        -O3
        -sALLOW_MEMORY_GROWTH=1
        -sINITIAL_MEMORY=536870912
        -sSTACK_SIZE=4194304
        -sMODULARIZE=1
        -sEXPORT_ES6=0
        -sEXPORT_NAME=createEmu
        "-sEXPORTED_FUNCTIONS=['_emu_init','_emu_error','_emu_load_snapshot','_emu_host_clock','_emu_clock_delay','_emu_run','_emu_status','_emu_frame','_emu_touch','_emu_touch_contacts','_emu_release_inputs','_emu_inputs_idle','_emu_button_state','_emu_save_snapshot','_emu_snapshot_size','_emu_free_snapshot','_emu_snapshot_begin','_emu_snapshot_advance','_emu_snapshot_data','_emu_ticks','_emu_button','_emu_set_cpi','_emu_cycles','_emu_serial_read','_malloc','_free']"
        "-sEXPORTED_RUNTIME_METHODS=['ccall','cwrap','HEAPU8','HEAPU32','addFunction','removeFunction']"
    )
    set_target_properties(emu PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${IPHONE1_WEB_DIST}"
    )
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    if(IPHONE1_WASM_JIT)
        find_program(IPHONE1_NODE_EXECUTABLE NAMES node)
        if(NOT IPHONE1_NODE_EXECUTABLE)
            message(FATAL_ERROR "Node.js is required to generate the browser accelerator")
        endif()
        set(jit_outputs
            "${CMAKE_SOURCE_DIR}/web/jit/seed.wasm"
            "${CMAKE_SOURCE_DIR}/web/jit/regions.json"
            "${CMAKE_SOURCE_DIR}/web/jit/abi.json")
        add_custom_command(OUTPUT ${jit_outputs}
            COMMAND "${CMAKE_COMMAND}" -E env "JIT_RUNTIME=${IPHONE1_WEB_DIST}/emu.js"
                    "${IPHONE1_NODE_EXECUTABLE}" tools/jit/generate-seed.mjs
            WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
            DEPENDS emu tools/jit/generate-seed.mjs tools/jit/compiler.cjs
                    tools/jit/seed-input.json.gz package-lock.json
            COMMENT "Generating the guarded browser instruction seed"
            VERBATIM)
        add_custom_target(jit_seed ALL DEPENDS ${jit_outputs})
    endif()
    add_custom_target(offline_bundle ALL
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tools/build-offline.py"
        DEPENDS emu
        COMMENT "Versioning the complete offline browser bundle")
    if(IPHONE1_WASM_JIT)
        add_dependencies(offline_bundle jit_seed)
    endif()
else()
    add_subdirectory(tools)
endif()
