#pragma once

#include <coco/platform/Loop_TIM2.hpp>
#include <coco/platform/Flash_FLASH.hpp>
#include <coco/Storage_Flash.hpp>


using namespace coco;

// drivers for FlashTest
struct Drivers {
	// flash start address
	static constexpr int FLASH_ADDRESS = 0x8000000 + 32768;

	Loop_TIM2 loop;
	Flash_FLASH flash{FLASH_ADDRESS, 4, 8192};
	Storage_Flash storage{flash};
};
