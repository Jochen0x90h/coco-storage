#pragma once

#include <coco/platform/Loop_native.hpp>
#include <coco/platform/Flash_File.hpp>
#include <coco/BufferStorage.hpp>


using namespace coco;

//constexpr int WORD_SIZE = 2;
//constexpr int PAGE_SIZE = 1024;
//constexpr int WORD_SIZE = 4;
//constexpr int PAGE_SIZE = 4096;
constexpr int WORD_SIZE = 8;
constexpr int PAGE_SIZE = 2048;

const BufferStorage::Info storageInfo {
    0, // address
    WORD_SIZE,
    PAGE_SIZE,
    8192, // sector size
    2, // sector count
    BufferStorage::Type::MEM_4N
};


// drivers for StorageTest
struct Drivers {
    Loop_native loop;
    Flash_File flash{"StorageTest.bin", 16384, PAGE_SIZE, WORD_SIZE};
    Flash_File::Buffer flashBuffer{256, flash};
};

Drivers drivers;
