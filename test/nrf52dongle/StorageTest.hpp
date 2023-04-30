#pragma once

#include <coco/platform/Loop_RTC0.hpp>
#include <coco/platform/Flash_NVMC.hpp>
#include <coco/board/config.hpp>
#include <coco/Storage_Buffer.hpp>


using namespace coco;

Storage_Buffer::Info storageInfo{
	Flash_NVMC::PAGE_SIZE,
	Flash_NVMC::BLOCK_SIZE,
	FLASH_ADDRESS + 0xe0000 - 8192 * 2, // address
	8192, // sector size
	2, // sector count
	Storage_Buffer::Type::FLASH_4N
};


// drivers for FlashTest
struct Drivers {
	Loop_RTC0 loop;
	Flash_NVMC::Buffer<256> buffer;
};
