#pragma once

#include <coco/platform/Loop_native.hpp>
#include <coco/platform/Flash_File.hpp>
#include <coco/Storage_Flash.hpp>


using namespace coco;

// drivers for FlashTest
struct Drivers {
	Loop_native loop;
	Flash_File flash{"storageTest.bin", 4, 32768, 4};
	Storage_Flash storage{flash};
};
