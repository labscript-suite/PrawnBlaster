add_executable(prawnblaster
        prawnblaster.c
        )

pico_generate_pio_header(prawnblaster ${CMAKE_CURRENT_LIST_DIR}/pseudoclock.pio)


# Pull in our pico_stdlib which aggregates commonly used features
target_link_libraries(prawnblaster pico_stdlib hardware_pio pico_multicore)

# enable usb output, disable uart output
pico_enable_stdio_usb(prawnblaster 1)
pico_enable_stdio_uart(prawnblaster 0)

# create map/bin/hex/uf2 file etc.
pico_add_extra_outputs(prawnblaster)

