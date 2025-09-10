#pragma once

#include <coco/BufferStorage.hpp>
#include <coco/platform/Loop_RTC0.hpp>
#include <coco/platform/Flash_flash.hpp>
#include <coco/board/config.hpp>


using namespace coco;

const BufferStorage::Info storageInfo{
    FLASH_ADDRESS + 0xe0000 - 8192 * 2, // address
    flash::BLOCK_SIZE,
    flash::PAGE_SIZE,
    8192, // sector size
    2, // sector count
    BufferStorage::Type::FLASH_4N
};


// drivers for FlashTest
struct Drivers {
    Loop_RTC0 loop;
    Flash_flash flash;
    Flash_flash::Buffer<256> buffer{flash};
};
