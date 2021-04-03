add_executable(prawnblaster
        prawnblaster.cpp
        )

pico_generate_pio_header(prawnblaster ${CMAKE_CURRENT_LIST_DIR}/pseudoclock.pio)


# Pull in our pico_stdlib which aggregates commonly used features
target_link_libraries(prawnblaster pico_stdlib hardware_pio pico_multicore hardware_clocks hardware_dma)

# enable usb output, disable uart output
pico_enable_stdio_usb(prawnblaster 1)
pico_enable_stdio_uart(prawnblaster 0)

# create map/bin/hex/uf2 file etc.
pico_add_extra_outputs(prawnblaster)

add_executable(prawnblasteroverclock
        prawnblaster.cpp
        )

pico_generate_pio_header(prawnblasteroverclock ${CMAKE_CURRENT_LIST_DIR}/pseudoclock.pio)

set_target_properties(prawnblasteroverclock PROPERTIES COMPILE_DEFINITIONS PRAWNBLASTER_OVERCLOCK=1)

# Pull in our pico_stdlib which aggregates commonly used features
target_link_libraries(prawnblasteroverclock pico_stdlib hardware_pio pico_multicore hardware_clocks hardware_dma)

# enable usb output, disable uart output
pico_enable_stdio_usb(prawnblasteroverclock 1)
pico_enable_stdio_uart(prawnblasteroverclock 0)

# create map/bin/hex/uf2 file etc.
pico_add_extra_outputs(prawnblasteroverclock)