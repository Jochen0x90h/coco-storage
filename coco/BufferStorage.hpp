#pragma once

#include "Storage.hpp"
#include <coco/Buffer.hpp>
#include <coco/Semaphore.hpp>


namespace coco {

/// @brief Storage implementation working on a buffer with address header such as internal or external flash.
/// Multiple coroutines can use it at the same time, a semaphore makes sure that only one modification is done at a time.
///
/// Inspired by Zephyr
/// https://docs.zephyrproject.org/latest/services/storage/nvs/nvs.html
/// https://github.com/zephyrproject-rtos/zephyr/blob/main/subsys/fs/nvs/nvs.c
class BufferStorage : public Storage {
public:
    /// Memory type
    enum class Flags : uint8_t {
        NONE = 0,

        // supports erase command
        ERASE = 1,
    };

    /// Memory info
    struct Info {
        /// Start address in memory
        uint32_t address;

        /// Size of a sector, must be a multiple of pageSize and up to 32768 * blockSize
        int sectorSize;

        /// Number of sectors, must be at least 2
        int8_t sectorCount;

        /// Memory type ()
        Flags flags;

        /// Size of a word that has to be written at once, must be power of two
        int16_t wordSize;

        /// Size of a page that has to be erased at once, must be power of two
        int pageSize;

        /// Memory type
        //Type type;

        /// Commands for serial memory (read, write, erase)
        //uint8_t commands[3];
    };

    /// @brief Constructor.
    /// @param info Memory info
    /// @param buffer Buffer to operate on. Header capacity must match the memory type.
    BufferStorage(const Info &info, Buffer &buffer);

    const State &state() override;
    [[nodiscard]] AwaitableCoroutine mount(int &result) override;
    [[nodiscard]] AwaitableCoroutine clear(int &result) override;
    [[nodiscard]] AwaitableCoroutine read(int id, void *data, int size, int &result) override;
    [[nodiscard]] AwaitableCoroutine write(int id, void const *data, int size, int &result) override;
    using Storage::read;
    using Storage::write;

    /// CRC-16/CCITT-FALSE (https://crccalc.com/?crc=12&method=crc16&datatype=ascii&outtype=0)
    static uint16_t crc16(const void *data, int size, uint16_t crc = 0xffff);

protected:
    enum SectorState {
        EMPTY,
        OPEN,
        CLOSED
    };

    // allocation table entry
    union Entry {
        // normal entry with size > 3
        struct {
            // id of entry
            uint16_t id;

#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
            // size of data
            uint16_t size;

            // offset of data in sector
            uint16_t offset;
#else
            // offset of data in sector
            uint16_t offset;

            // size of data
            uint16_t size;
#endif
            // checksum of the entry
            uint16_t checksum;
        };

        // small entry with size <= 3
        struct {
            uint16_t id;

#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
            // inline data
            uint8_t data[3];

            // size of inline data, must be mapped to upper byte of offset
            uint8_t size;
#else
            // size of inline data, must be mapped to upper byte of offset
            uint8_t size;

            // inline data
            uint8_t data[3];
#endif
            uint16_t checksum;
        } small;

        uint32_t data[2];

        bool empty() {return (this->data[0] & this->data[1]) == 0xffffffff;}
    };

    // calculate the checksum of an entry
    static uint16_t calcChecksum(const Entry &entry) {
        return crc16(&entry, offsetof(Entry, checksum));
    }

    COCO_NODISCARD auto read(uint32_t offset, int size) {
        buffer_.header<uint32_t>() = info_.address + offset;
        return buffer_.read(size);
    }

    COCO_NODISCARD auto write(uint32_t offset, int size) {
        buffer_.header<uint32_t>() = info_.address + offset;
        return buffer_.write(size);
    }

    COCO_NODISCARD auto erase(uint32_t offset) {
        buffer_.header<uint32_t>() = info_.address + offset;
        return buffer_.erase();
    }

    // check if allocation table entry is valid
    bool isEntryValid(int entryOffset, int dataOffset, const Entry &entry);

    // detect the entry and data offsets for an open sector
    AwaitableCoroutine detectOffsets(int sectorIndex, std::pair<int, int>& offsets);

    // get the offset of the last entry in a closed sector
    AwaitableCoroutine getLastEntry(int sectorOffset, int &entryOffsetResult);

    // write an entry (without data unless size is up to 2)
    Awaitable<CoroutineTask<Buffer::Events>> writeEntry(int id, int size, const uint8_t *data);

    // close the current sector
    Awaitable<CoroutineTask<Buffer::Events>> closeSector();

    // check if closing allocation table entry is valid
    bool isCloseEntryValid(const Entry &entry);

    // erase a sector
    AwaitableCoroutine eraseSector(int index);

    // copy an entry in the given sector to the current sector
    void copyEntry(int sector, Entry &entry);

    // garbage collect
    AwaitableCoroutine gc(int emptySectorIndex);


    // memory info
    Info info_;

    // buffer for reading/writing on memory
    Buffer &buffer_;

    // size of allocation table entry (Entry) aligned to flash block size
    int entrySize_;

    // shift of offset allocation table entry (Entry) according to info.blockSize
    int offsetShift_;

    State state_ = State::NOT_MOUNTED;

    // current sector
    int sectorIndex_ = 0;
    int sectorOffset_ = 0;

    // write offsets in current sector
    int entryWriteOffset_;
    int dataWriteOffset_;

    Semaphore semaphore_;
};
COCO_ENUM(BufferStorage::Flags)

} // namespace coco
