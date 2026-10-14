# The emulator core: portable C11, no platform dependencies. Derived from
# S5LBox (MIT, see third_party/S5LBox-LICENSE); the board layer is ours.
add_library(emucore STATIC
    src/arm/arm_interp.c
    src/arm/vfp.c
    src/mmu.c
    src/snapshot.c
    src/vm_source.c
    src/vm_block.c
    src/md_bridge.c
    src/md_raw_bridge.c
    src/soc/uart.c
    src/soc/vic.c
    src/soc/timer.c
    src/soc/mbx.c
    src/soc/power.c
    src/soc/clcd.c
    src/soc/tvout.c
    src/soc/i2c.c
    src/soc/pcf50635.c
    src/soc/wm8991.c
    src/soc/wm8758.c
    src/soc/i2s.c
    src/soc/spi.c
    src/soc/gpioic.c
    src/soc/buttons.c
    src/soc/mtz2.c
    src/soc/mt_common.c
    src/soc/mtz1.c
    src/soc/usbotg.c
    src/soc/pl080.c
    src/soc/nor.c
    src/soc/nand.c
    src/soc/storage.c
    src/soc/machine.c
    src/board/board.c
    src/board/m68.c
    src/board/n82.c
    src/firmware/img3.c
    src/firmware/aes.c
    src/firmware/loader.c
    src/firmware/devicetree.c
    src/firmware/lzss.c
    src/firmware/macho.c
    src/firmware/ksyms.c
    src/boot/bringup.c
    src/boot/dt_patch.c
    src/boot/bringup_os1.c
)
target_include_directories(emucore PUBLIC include)
if(NOT WIN32 AND NOT APPLE)
    target_link_libraries(emucore PUBLIC m)
endif()
if(NOT MSVC)
    target_compile_options(emucore PRIVATE -Wall -Wextra)
    set_source_files_properties(src/arm/vfp.c PROPERTIES COMPILE_OPTIONS "-ffp-contract=off")
endif()

# Unit tests that need no firmware.
if(NOT IPHONE1_WEB)
    foreach(t test_arm test_soc test_devicetree test_macho test_lzss test_loader test_vfp
              test_spi test_i2c test_gpioic test_buttons test_mtz2 test_mtz1 test_pl080
              test_mbx test_nand test_snapshot test_ksyms test_img3 test_aes
              test_board test_bringup_os1 test_wm8758 test_clock_carry
              test_pcf50635_update test_pcf50635_pending_snapshot)
        add_executable(${t} tests/${t}.c)
        target_link_libraries(${t} emucore)
        add_test(NAME ${t} COMMAND ${t})
    endforeach()
    # Exercise the browser C API natively against real device models. The
    # Emscripten annotation stub is scoped to this test target only.
    add_executable(test_web_api ../web/tests/native_api.c)
    target_include_directories(test_web_api PRIVATE ../web/tests/include)
    target_link_libraries(test_web_api PRIVATE emucore)
    add_test(NAME test_web_api COMMAND test_web_api)
endif()
