set(overclocks 0;1)

foreach (overclock IN LISTS overclocks)
    # Compute firmware name
    set(firmware_name prawnblaster)
    if(PICO_PLATFORM MATCHES "^rp2350")
        set(firmware_name "${firmware_name}_rp2350")
    else()
        set(firmware_name "${firmware_name}_${PICO_PLATFORM}")
    endif()
    if(overclock)
        set(firmware_name "${firmware_name}_overclock")
    endif()

    add_executable(${firmware_name} prawnblaster.cpp fast_serial.c)

    pico_generate_pio_header(${firmware_name} ${CMAKE_CURRENT_LIST_DIR}/pseudoclock.pio)

    # Pass in number of instructions to firmware as a compiler definition
    set(num_instructions 30000)
    if(PICO_PLATFORM MATCHES "^rp2350")
        set(num_instructions 60000)
    endif()
    target_compile_definitions(${firmware_name} PUBLIC "PRAWNBLASTER_NUM_INSTRUCTIONS=${num_instructions}")

    # Pass in board type to firmware as a compiler definition. Note that PICO_BOARD is passed in by the SDK, but it's passed in a string which isn't valid and so I can't use it...
    # This is also, to some extent, a duplicate of the above PRAWNBLASTER_NUM_INSTRUCTIONS but I think it makes sense to keep these seperate.
    if (PICO_BOARD STREQUAL "pico")
        target_compile_definitions(${firmware_name} PUBLIC "PRAWNBLASTER_PICO_BOARD=1")
    elseif (PICO_BOARD STREQUAL "pico2")
        target_compile_definitions(${firmware_name} PUBLIC "PRAWNBLASTER_PICO_BOARD=2")
    else ()
        message(FATAL_ERROR "Unsupported PICO_BOARD")
    endif()
    

    # Pass in overclock state to firmware as a compiler definition
    if(overclock)
        target_compile_definitions(${firmware_name} PUBLIC "PRAWNBLASTER_OVERCLOCK=1")
    endif()

    # Pull in our pico_stdlib which aggregates commonly used features
    target_link_libraries(${firmware_name} pico_stdlib hardware_pio pico_multicore pico_unique_id hardware_clocks hardware_dma tinyusb_device tinyusb_board)
    target_include_directories(${firmware_name} PRIVATE .)

    # create map/bin/hex/uf2 file etc.
    pico_add_extra_outputs(${firmware_name})
        
endforeach()
