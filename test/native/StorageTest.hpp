#pragma once

#include <coco/platform/Loop_native.hpp>
#include <coco/platform/Flash_File.hpp>
#include <coco/BufferStorage.hpp>


using namespace coco;

//constexpr int PAGE_SIZE = 1024;
//constexpr int BLOCK_SIZE = 2;
//constexpr int PAGE_SIZE = 4096;
//constexpr int BLOCK_SIZE = 4;
constexpr int PAGE_SIZE = 2048;
constexpr int BLOCK_SIZE = 8;

BufferStorage::Info storageInfo {
	PAGE_SIZE,
	BLOCK_SIZE,
	0, // address
	8192, // sector size
	2, // sector count
	BufferStorage::Type::MEM_4N
};


// drivers for FlashTest
struct Drivers {
	Loop_native loop;
	Flash_File flash{"flash.bin", 16384, PAGE_SIZE, BLOCK_SIZE};
	Flash_File::Buffer buffer{256, flash};
};
