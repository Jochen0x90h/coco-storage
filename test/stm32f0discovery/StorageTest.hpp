#pragma once

#include <coco/BufferStorage.hpp>
#include <coco/platform/Loop_TIM2.hpp>
#include <coco/platform/Flash_FLASH.hpp>
#include <coco/board/config.hpp>


using namespace coco;

BufferStorage::Info storageInfo{
	FLASH_ADDRESS + FLASH_SIZE - 8192 * 2, // address
	Flash_FLASH::BLOCK_SIZE,
	Flash_FLASH::PAGE_SIZE,
	8192, // sector size
	2, // sector count
	BufferStorage::Type::FLASH_4N
};


// drivers for FlashTest
struct Drivers {
	Loop_TIM2 loop{SYS_CLOCK};
	Flash_FLASH::Buffer<256> buffer;
};
