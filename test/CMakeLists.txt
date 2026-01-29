# Generate a test for a board
# TEST the test application, implemented in ${TEST}.cpp
# BOARD_LIB a library for a board containing SystemInit() and a linker script for embedded platforms
function(board_test TEST BOARD_LIB)
    # check if board library exists for the current platform
    if(TARGET ${BOARD_LIB})
        string(REGEX REPLACE ".*\\:" "" BOARD ${BOARD_LIB})
        set(NAME "${TEST}-${BOARD}")
        message("*** Board: ${BOARD}")

        add_executable(${NAME}
            ${TEST}.cpp
        )
        target_include_directories(${NAME}
            PRIVATE
                ../
                ${BOARD}
        )
        target_link_libraries(${NAME}
            ${BOARD_LIB}
            coco-loop::coco-loop
            coco-flash::coco-flash
            ${PROJECT_NAME}
        )

        # generate hex file for flashing the target
        if(${CMAKE_CROSSCOMPILING})
            #message("*** Generate Hex for ${NAME} using ${CMAKE_OBJCOPY}")
            add_custom_command(TARGET ${NAME}
                POST_BUILD
                COMMAND ${CMAKE_OBJCOPY} -O ihex ${NAME} ${NAME}.hex
            )
        endif()
    endif()
endfunction()

board_test(StorageTest coco-devboards::native)
board_test(StorageTest coco-devboards::nrf52dongle)
board_test(StorageTest coco-devboards::stm32f0discovery)
board_test(StorageTest coco-devboards::stm32f3348discovery)
board_test(StorageTest coco-devboards::nucleo-g431rb)
board_test(StorageTest coco-devboards::nucleo-g474re)
board_test(StorageTest coco-devboards::nucleo-g491re)
board_test(StorageTest coco-devboards::nucleo-h503rb)
board_test(StorageTest coco-devboards::nucleo-u385rg-q)
