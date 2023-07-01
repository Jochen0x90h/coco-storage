add_library(${PROJECT_NAME})
target_sources(${PROJECT_NAME}
	PUBLIC FILE_SET headers TYPE HEADERS FILES
		#Storage.hpp
		#Storage_Flash.hpp
		Storage.hpp
		Storage_Buffer.hpp
	PRIVATE
		#Storage.cpp
		#Storage_Flash.cpp
		Storage.cpp
		Storage_Buffer.cpp
)

target_link_libraries(${PROJECT_NAME}
	coco::coco
	coco-buffer::coco-buffer
)

target_include_directories(${PROJECT_NAME}
	PUBLIC
		..
)

# install the library
install(TARGETS ${PROJECT_NAME}
	FILE_SET headers DESTINATION include/coco
	FILE_SET platform_headers DESTINATION include
	LIBRARY DESTINATION lib
	ARCHIVE DESTINATION lib
)
