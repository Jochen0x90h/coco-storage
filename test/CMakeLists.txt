function(board_test TEST TARGET)
	#message("*** Target: ${TARGET}")
	# check if target exists
	if(TARGET ${TARGET})
		string(REGEX REPLACE ".*\\:" "" BOARD ${TARGET})
		set(NAME "${TEST}-${BOARD}")
		message("*** Test ${TEST} on board ${BOARD}")

		add_executable(${NAME}
			${TEST}.cpp
		)
		target_include_directories(${NAME}
			PRIVATE
				../
				${BOARD}
		)
		target_link_libraries(${NAME}
			${TARGET}
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

#message("*** coco-devboards: ${coco-devboards_COMPONENT_NAMES}")

board_test(StorageTest coco-devboards::native)
board_test(StorageTest coco-devboards::nrf52dongle)
board_test(StorageTest coco-devboards::stm32f0discovery)
board_test(StorageTest coco-devboards::stm32f3348discovery)
board_test(StorageTest coco-devboards::stm32g431nucleo)


# hex generator
macro(generate_hex TARGET)
	if(${CMAKE_CROSSCOMPILING})
		#message("*** Generate Hex for ${TARGET} using ${CMAKE_OBJCOPY}")
		add_custom_command(TARGET ${TARGET}
			POST_BUILD
			COMMAND ${CMAKE_OBJCOPY} -O ihex ${TARGET} ${TARGET}.hex
		)
	endif()
endmacro()
if(false)
# iterate over boards and generate tests
foreach(COMPONENT ${coco-devboards_COMPONENT_NAMES})
	string(REGEX REPLACE ".*\\:" "" BOARD ${COMPONENT})
	message("*** Board: ${BOARD}")

	# check if board specific header exists
	if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/${BOARD}/StorageTest.hpp)
		set(NAME "StorageTest-${BOARD}")

		add_executable(${NAME}
			StorageTest.cpp
		)
		target_include_directories(${NAME}
			PRIVATE
				../
				${BOARD}
		)

		# find link.ld of board
		get_target_property(INCLUDES ${COMPONENT} INTERFACE_INCLUDE_DIRECTORIES)
		target_link_directories(${NAME}
			PRIVATE
				${INCLUDES}
		)

		target_link_libraries(${NAME}
			coco::coco
			coco-loop::coco-loop
			coco-flash::coco-flash
			${COMPONENT}
			${PROJECT_NAME}
		)

		# windows specific libraries
		if(WIN32)
			target_link_libraries(${NAME} wsock32 ws2_32 winmm)
		endif()

		# generate hex file for flashing the target
		generate_hex(${NAME})
	endif()
endforeach()
endif()