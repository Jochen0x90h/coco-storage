#pragma once

#include <coco/platform/Loop_RTC0.hpp>
#include <coco/platform/Flash_NVMC.hpp>
#include <coco/Storage_Flash.hpp>


using namespace coco;

// drivers for FlashTest
struct Drivers {
	Loop_RTC0 loop;
	Flash_NVMC flash{0xe0000 - 4 * 32768, 4, 32768};
	Storage_Flash storage{flash};
};
