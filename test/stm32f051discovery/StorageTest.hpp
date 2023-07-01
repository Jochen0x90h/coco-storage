#pragma once

#include <coco/platform/Loop_TIM2.hpp>
#include <coco/platform/Flash_FLASH.hpp>
#include <coco/board/config.hpp>
#include <coco/Storage_Buffer.hpp>


using namespace coco;

Storage_Buffer::Info storageInfo{
	Flash_FLASH::PAGE_SIZE,
	Flash_FLASH::BLOCK_SIZE,
	FLASH_ADDRESS + FLASH_SIZE - 8192 * 2, // address
	8192, // sector size
	2, // sector count
	Storage_Buffer::Type::FLASH_4N
};


// drivers for FlashTest
struct Drivers {
	Loop_TIM2 loop;
	Flash_FLASH::Buffer<256> buffer;
};
