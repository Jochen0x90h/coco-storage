#include "BufferStorage.hpp"
#include <coco/align.hpp>
#include <coco/bits.hpp>
#include <coco/convert.hpp>
#include <coco/debug.hpp>


// todo: check if an entry has the same content when writing

namespace coco {

namespace {

// small entries where data can be stored in the entry
constexpr int SMALL_SIZE = 3;
constexpr int SMALL_FLAG = 0x80;

}

BufferStorage::BufferStorage(const Info &info, Buffer &buffer)
    : info_(info), buffer_(buffer), semaphore_(1)
{
    assert(info.wordSize >= 1 && firstBit(info.wordSize) == info.wordSize);
    assert(info.pageSize >= 1 && firstBit(info.pageSize) == info.pageSize);
    assert(info.sectorSize >= 1 && info.sectorSize % info.pageSize == 0 && info.sectorSize <= 32768 * info.wordSize);
    assert(info.sectorCount >= 2);

    // align size of allocation table entry to flash block size
    entrySize_ = int(sizeof(Entry) + info.wordSize - 1) & ~(info.wordSize - 1);

    // calc offset shift
    offsetShift_ = 0;
    for (int i = 1; i < info.wordSize; i <<= 1)
        ++offsetShift_;
}

const Storage::State &BufferStorage::state() {
    return state_;
}

AwaitableCoroutine BufferStorage::mount(int &result) {
    // acquire semaphore
    co_await semaphore_.untilAcquired();
    Semaphore::Guard guard(semaphore_);

    state_ = State::BUSY;
    auto &buffer = buffer_;
    co_await buffer.acquire();

    /*
        Cases for recovery of sector state
        E = Empty
        O = Open
        C = Closed

        Three sectors, first is head, last is tail but still empty
        O E E
        C E E (closed head)
        C O E (new entry)

        Three sectors, first is head, last is tail
        O E C
        C E C (closed head)
        C O C (copied tail to empty sector)
        C O E (erased tail)

        Two sectors, first is head and tail, second is empty
        O E
        C E (closed head)
        C O (copied tail to empty sector)
        E O (erased tail)
    */

    // find the current sector
    int lastI = info_.sectorCount - 1;
    SectorState lastState;
    int head = 0;
    bool foundEmpty = false;
    auto foundState = SectorState::EMPTY;
    for (int i = -1; i < info_.sectorCount; ++i) {
        // detect sector state
        SectorState sectorState;
        {
            int sectorIndex = i == -1 ? lastI : i;
            int sectorOffset = sectorIndex * info_.sectorSize;

            // read close indicator at start of sector
            co_await read(sectorOffset, sizeof(Entry));
            if (buffer.size() < int(sizeof(Entry))) {
                // something went wrong
                result = Result::FATAL_ERROR;
                state_ = State::READY;
                co_return;
            }
            if (buffer.cast<Entry &>().empty()) {
                // sector is empty or open: read first data entry (second entry from start of sector)
                co_await read(sectorOffset + entrySize_, sizeof(Entry));
                if (buffer.size() < int(sizeof(Entry))) {
                    // something went wrong
                    result = Result::FATAL_ERROR;
                    state_ = State::READY;
                    co_return;
                }
                if (buffer.cast<Entry &>().empty()) {
                    // sector is empty
                    sectorState = SectorState::EMPTY;
                } else {
                    // sector is open
                    sectorState = SectorState::OPEN;
                }
            } else {
                // sector is closed
                sectorState = SectorState::CLOSED;
            }
        }

        if (i >= 0) {
            // if a non-empty sector is followed by an empty sector, the non-empty sector is head
            if (lastState != SectorState::EMPTY && sectorState == SectorState::EMPTY) {
                head = lastI;
                foundEmpty = true;
                foundState = lastState;
            }

            // if a closed sector is followed by an open sector, the closed sector is head
            if (!foundEmpty && lastState == SectorState::CLOSED && sectorState == SectorState::OPEN) {
                head = lastI;
                foundState = lastState;
            }
            lastI = i;
        }
        lastState = sectorState;
    }

    // make sure the next sector is empty which is not the case when copying of tail to empty sector was interrupted
    int next = head + 1 == info_.sectorCount ? 0 : head + 1;
    co_await eraseSector(next);


    switch (foundState) {
    case SectorState::EMPTY:
        // this happens if the flash is empty, make sure the sector is really empty
        //flash.eraseSectorBlocking(head);
        sectorIndex_ = head;
        sectorOffset_ = sectorIndex_ * info_.sectorSize;

        // set entry and data offsets
        entryWriteOffset_ = entrySize_;
        dataWriteOffset_ = info_.sectorSize;
        break;
    case SectorState::OPEN: {
        // typical case where one sector is open for write
        sectorIndex_ = head;
        sectorOffset_ = sectorIndex_ * info_.sectorSize;

        // set entry and data offsets
        std::pair<int, int> offsets;
        co_await detectOffsets(head, offsets);
        entryWriteOffset_ = offsets.first;
        dataWriteOffset_ = offsets.second;
        break;
    }
    case SectorState::CLOSED:
        // we were interrupted in the garbage collection process

        // go to next sector
        sectorIndex_ = next;
        sectorOffset_ = sectorIndex_ * info_.sectorSize;

        // set entry and data offsets
        entryWriteOffset_ = entrySize_;
        dataWriteOffset_ = info_.sectorSize;

        // garbage collect tail sector
        co_await gc(next);

        break;
    }
    result = OK;
    state_ = State::READY;
}

AwaitableCoroutine BufferStorage::clear(int &result) {
    // acquire semaphore
    co_await semaphore_.untilAcquired();
    Semaphore::Guard guard(semaphore_);

    state_ = State::BUSY;

    co_await buffer_.acquire();
//debug::set(debug::MAGENTA);

    // erase flash
    for (int i = 0; i < info_.sectorCount; ++i) {
        //debug::out << "erase sector " << dec(i) << '\n';
        co_await eraseSector(i);
    }
//debug::set(debug::CYAN);

    // initialize member variables
    sectorIndex_ = 0;
    sectorOffset_ = 0;
    entryWriteOffset_ = entrySize_;
    dataWriteOffset_ = info_.sectorSize;

    result = OK;
    state_ = State::READY;
}

AwaitableCoroutine BufferStorage::read(int id, void *data, int size, int &result) {
    // acquire semaphore
    co_await semaphore_.untilAcquired();
    Semaphore::Guard guard(semaphore_);

    uint8_t *dst = reinterpret_cast<uint8_t *>(data);

    // fill data with zeros
    std::fill(dst, dst + size, 0);

    // check state
    if (state_ != State::READY) {
        assert(false);
        size = 0;
        result = NOT_READY;
        co_return;
    }

    // check id
    if (uint32_t(id) > 0xffff) {
        assert(false);
        size = 0;
        result = INVALID_ID;
        co_return;
    }
    state_ = State::BUSY;
    auto &buffer = buffer_;

    // get sector info (allocation table starts from front, data from back)
    int sectorIndex = sectorIndex_;
    int sectorOffset = sectorOffset_;
    int entryOffset = entryWriteOffset_ - entrySize_;
    int dataOffset = info_.sectorSize;

    // iterate over sectors
    int i = 0;
    while (true) {
        // iterate over allocation table entries from last to first (newest to oldest)
        while (entryOffset > 0) {
            // read entry
            co_await read(sectorOffset + entryOffset, sizeof(Entry));
            if (buffer.size() < int(sizeof(Entry))) {
                // something went wrong
                result = FATAL_ERROR;
                state_ = State::READY;
                co_return;
            }
            Entry &entry = buffer.cast<Entry &>();

            // check if entry is valid
            if (isEntryValid(entryOffset, dataOffset, entry)) {
                // check if found
                if (entry.id == id) {
                    // read data
                    if ((entry.small.size & SMALL_FLAG) == 0) {
                        // not a small entry
                        int dataSize = entry.size;
                        int s = std::min(size, dataSize);

                        // calc offset in memory (offset of sector + offset of entry)
                        int offset = sectorOffset + (entry.offset << offsetShift_);
                        while (s > 0) {
                            int capacity = buffer.capacity() & ~(info_.wordSize - 1);
                            int toRead = std::min(s, capacity);

                            co_await read(offset, toRead);
                            int read = buffer.size();
                            if (read < toRead) {
                                // something went wrong
                                state_ = State::READY;
                                result = FATAL_ERROR;
                                co_return;
                            }
                            std::copy(buffer.data(), buffer.data() + read, dst);
                            offset += read;
                            dst += read;
                            s -= read;
                        }
                        result = dataSize;
                    } else {
                        // small entry with inline data
                        int dataSize = entry.small.size & 3;
                        int s = std::min(size, dataSize);
                        std::copy(entry.small.data, entry.small.data + s, dst);
                        result = dataSize;
                    }
                    state_ = State::READY;
                    co_return;
                }
            }
            entryOffset -= entrySize_;
        }

        ++i;
        if (i == info_.sectorCount - 1)
            break;

        // go to previous sector
        sectorIndex = sectorIndex == 0 ? info_.sectorCount - 1 : sectorIndex - 1;
        sectorOffset = sectorIndex * info_.sectorSize;

        // get offset of last entry in allocation table
        co_await getLastEntry(sectorOffset, entryOffset);
    }

    // not found (which is ok)
    state_ = State::READY;
    result = 0;
}

AwaitableCoroutine BufferStorage::write(int id, const void *data, int size, int &result) {
    // acquire semaphore
    co_await semaphore_.untilAcquired();
    Semaphore::Guard guard(semaphore_);

    // check state
    if (state_ != State::READY) {
        assert(false);
        result = NOT_READY;
        co_return;
    }

    // check id
    if (uint32_t(id) > 0xffff) {
        assert(false);
        result = INVALID_ID;
        co_return;
    }

    // check size, must fit into a sector which has at least two entries (one for the single entry and one for closing)
    if (uint32_t(size) > uint32_t(info_.sectorSize - entrySize_ * 2)) {
        assert(false);
        result = WRITE_SIZE_EXCEEDED;
        co_return;
    }
    state_ = State::BUSY;
    auto &buffer = buffer_;

    // check if entry exists and has same data
    // todo

    // check if entry will fit
    int gcCount = 0;
    while (entryWriteOffset_ + entrySize_ + size > dataWriteOffset_) {
        // data does not fit, we need to start a new sector

        // check if all sectors were already garbage collected which means we are out of memory
        ++gcCount;
        if (gcCount >= info_.sectorCount) {
            result = OUT_OF_MEMORY;
            co_return;
        }

        // close current sector and go to next sector (which is erased)
        co_await closeSector();

        co_await gc(sectorIndex_);
    }

    // write data
    auto src = reinterpret_cast<const uint8_t *>(data);
    if (size > SMALL_SIZE) {
        int offset = dataWriteOffset_ - align(size, info_.wordSize);
        dataWriteOffset_ = offset;
        int s = size;
        while (s > 0) {
            int capacity = buffer.capacity() & ~(info_.wordSize - 1);
            int toWrite = std::min(s, capacity);

            std::copy(src, src + toWrite, buffer.data());
            co_await write(sectorOffset_ + offset, toWrite);
            offset += toWrite;
            src += toWrite;
            s -= toWrite;
        }
    }

    // write entry (with inline data if size <= SMALL_SIZE)
    co_await writeEntry(id, size, src);

    result = size;
    state_ = State::READY;
}

// reference: https://www.ccsinfo.com/forum/viewtopic.php?t=24977
uint16_t BufferStorage::crc16(const void *data, int size, uint16_t crc) {
    auto *it = reinterpret_cast<const uint8_t *>(data);
    auto *end = it + size;
    for (; it < end; ++it) {
        uint16_t x = (crc >> 8) ^ *it;
        x ^= x >> 4;
        crc = (crc << 8) ^ (x << 12) ^ (x << 5) ^ x;
    }
    return crc;
}

/*
// more on crc: https://www.mikrocontroller.net/attachment/91385/crc16.c
// crc with table (can be obtained from https://crccalc.com/?crc=12&method=crc16&datatype=ascii&outtype=0):
uint16_t crc16(const void *data, int size, uint16_t crc) {
    auto *d = reinterpret_cast<const uint8_t *>(data);
    for (int i = 0; i < size; ++i) {
        crc = crc16Table[(crc >> 8) ^ d[i]] ^ (crc << 8);
    }
    return crc;
}
*/

bool BufferStorage::isEntryValid(int entryOffset, int dataOffset, const Entry &entry) {
    // check checksum
    if (entry.checksum != calcChecksum(entry))
        return false;

    if ((entry.small.size & SMALL_FLAG) == 0) {
        // not a small entry: check if data is in valid range
        int offset = entry.offset << offsetShift_;
        if (offset < entryOffset + entrySize_ || offset + entry.size > dataOffset)
            return false;
    }

    return true;
}

AwaitableCoroutine BufferStorage::detectOffsets(int sectorIndex, std::pair<int, int>& offsets) {
    auto &buffer = buffer_;
    int sectorOffset = sectorIndex * info_.sectorSize;
    int entryOffset = entrySize_;
    int dataOffset = info_.sectorSize;

    // iterate over entries
    while (entryOffset <= dataOffset) {
        // read next entry
        co_await read(sectorOffset + entryOffset, sizeof(Entry));
        if (buffer.size() < int(sizeof(Entry))) {
            // something went wrong
            break;
        }
        auto &entry = buffer.cast<Entry &>();

        // end of list is indicated by an empty entry
        if (entry.empty())
            break;

        // check if entry is valid
        if (isEntryValid(entryOffset, dataOffset, entry) && (entry.small.size & SMALL_FLAG) == 0) {
            // set new data offset
            dataOffset = entry.offset << offsetShift_;
        }
        entryOffset += entrySize_;
    }

    // check if data is actually empty and does not contain incomplete writes
    // check from entryOffset (behind last entry) to dataOffset (start of data of last entry)
    int size = dataOffset - entryOffset;
    int o = entryOffset;
    while (size > 0) {
        int capacity = buffer.capacity() & ~(info_.wordSize - 1);
        int toCheck = std::min(size, capacity);

        co_await read(sectorOffset + o, toCheck);

        int read = buffer.size();
        for (int i = 0; i < read; ++i) {
            if (buffer[i] != 0xff) {
                // down-align to block size
                dataOffset = (o + i) & ~(info_.wordSize - 1);
            }
        }
        size -= toCheck;
        o += toCheck;
    }

    offsets = {entryOffset, dataOffset};
}

AwaitableCoroutine BufferStorage::getLastEntry(int sectorOffset, int &entryOffsetResult) {
    auto &buffer = buffer_;

    // read close entry (assumption is that it is present and valid)
    {
        co_await read(sectorOffset, sizeof(Entry));
        if (buffer.size() < int(sizeof(Entry))) {
            // something went wrong
            entryOffsetResult = -1;
            co_return;
        }
        auto &entry = buffer.cast<Entry &>();

        // check if empty, should not happen as the sector is assumed to be closed
        // todo: report malfunction
        //if (entry.isEmpty())
        //	return 0;

        // return offset if close entry is valid
        if (isCloseEntryValid(entry)) {
            entryOffsetResult = entry.offset << offsetShift_;
            co_return;
        }
    }

    // close entry is not valid, therefore detect last entry
    int entryOffset = entrySize_;
    int validOffset = 0;
    int dataOffset = info_.sectorSize;

    // iterate over entries
    while (entryOffset <= dataOffset) {
        // read next entry
        co_await read(sectorOffset + entryOffset, sizeof(Entry));
        if (buffer.size() < int(sizeof(Entry))) {
            // something went wrong
            entryOffsetResult = -1;
            co_return;
        }
        auto &entry = buffer.cast<Entry &>();

        // end of list is indicated by an empty entry
        if (entry.empty())
            break;

        // check if entry is valid
        if (isEntryValid(entryOffset, dataOffset, entry)) {
            validOffset = entryOffset;

            if (entry.size > SMALL_SIZE) {
                // set new data offset
                dataOffset = entry.offset << offsetShift_;
            }
        }
        entryOffset += entrySize_;
    }

    entryOffsetResult = validOffset;
}

Awaitable<CoroutineTask<Buffer::Events>> BufferStorage::writeEntry(int id, int size, const uint8_t *data) {
    auto &buffer = buffer_;

    // create entry
    auto &entry = buffer.cast<Entry &>();
    entry.id = id;
    if (size > SMALL_SIZE) {
        entry.size = size;
        entry.offset = dataWriteOffset_ >> offsetShift_;
    } else {
        // small entry: inline data
        entry.small.size = SMALL_FLAG | size | 0x7c; // unused bits set to 1
        std::copy(data, data + size, entry.small.data);
        std::fill(entry.small.data + size, entry.small.data + 3, 0xff); // fill unused bytes with 0xff to reduce flash wear
    }
    entry.checksum = calcChecksum(entry);

    // calc offset and advance entry write offset
    int offset = sectorOffset_ + entryWriteOffset_;
    entryWriteOffset_ += entrySize_;

    // write entry
    return write(offset, sizeof(Entry));
}


Awaitable<CoroutineTask<Buffer::Events>> BufferStorage::closeSector() {
    auto &buffer = buffer_;

    // create entry
    auto &entry = buffer.cast<Entry &>();
    entry.id = 0xffff;
    entry.size = 0;
    entry.offset = (entryWriteOffset_ - entrySize_) >> offsetShift_; // offset of last entry in sector, gets used by getLastEntry()
    entry.checksum = calcChecksum(entry);

    // offset of close indicator entry at start of sector (index 0)
    int offset = sectorOffset_;

    // use next sector
    sectorIndex_ = sectorIndex_ + 1 == info_.sectorCount ? 0 : sectorIndex_ + 1;
    sectorOffset_ = sectorIndex_ * info_.sectorSize;
    entryWriteOffset_ = entrySize_;
    dataWriteOffset_ = info_.sectorSize;

    // write close entry at start of sector
    return write(offset, sizeof(Entry));
}

bool BufferStorage::isCloseEntryValid(const Entry &entry) {
    // check checksum
    if (entry.checksum != calcChecksum(entry))
        return false;

    // check if index is 0xffff and length is 0
    if (entry.id != 0xffff && entry.size != 0)
        return false;

    // check if there is at least one entry and the offset is inside the sector
    int offset = entry.offset << offsetShift_;
    if (offset >= entrySize_ && offset < info_.sectorSize)
        return false;

    return true;
}

AwaitableCoroutine BufferStorage::eraseSector(int index) {
    auto &buffer = buffer_;
    int sectorOffset = index * info_.sectorSize;

    if ((info_.flags & Flags::ERASE) == 0) {
        // erase command not supported: explicitly fill with 0xff
        int s = info_.sectorSize;
        int offset = sectorOffset;
        while (s > 0) {
            int capacity = buffer.capacity() & ~(info_.wordSize - 1);
            int toWrite = std::min(s, capacity);

            std::fill(buffer.data(), buffer.data() + capacity, 0xff);
            co_await write(offset, toWrite);
            offset += toWrite;
            s -= toWrite;
        }
    } else {
        // erase command supported
        for (int offset = 0; offset < info_.sectorSize; offset += info_.pageSize) {
            //debug::out << "erase page " << dec(offset / info_.pageSize) << '\n';
            co_await erase(sectorOffset + offset);
//debug::set(debug::YELLOW);
        }
    }
}

AwaitableCoroutine BufferStorage::gc(int emptySectorIndex) {
    // get sector at tail
    int tailSectorIndex = emptySectorIndex + 1 == info_.sectorCount ? 0 : sectorIndex_ + 1;
    int tailSectorOffset = tailSectorIndex * info_.sectorSize;

    // copy all entries from sector at tail to the sector at head
    int tailEntryOffset = entrySize_;
    int tailDataOffset = info_.sectorSize;
    int tailLastEntryOffset;
    co_await getLastEntry(tailSectorOffset, tailLastEntryOffset);
    while (tailEntryOffset <= tailLastEntryOffset) {
        // read entry
        co_await read(tailSectorOffset + tailEntryOffset, sizeof(Entry));
        if (buffer_.size() < int(sizeof(Entry))) {
            // something went wrong
            co_return;
        }
        Entry tailEntry = buffer_.cast<Entry &>();

        if (isEntryValid(tailEntryOffset, tailDataOffset, tailEntry)) {
            if (tailEntry.size > SMALL_SIZE) {
                // set new data offset, only for verification
                tailDataOffset = tailEntry.offset << offsetShift_;
            }

            // check if the entry is outdated (contains a newer entry with same id)
            int searchSectorIndex = tailSectorIndex;
            int searchEntryOffset = tailEntryOffset + entrySize_;
            int searchDataOffset = tailDataOffset;

            // search in all sectors
            for (int i = 0; i < info_.sectorCount - 1; ++i) {
                int searchSectorOffset = searchSectorIndex * info_.sectorSize;

                // get offset of last entry in allocation table
                int searchLastEntryOffset;
                co_await getLastEntry(searchSectorOffset, searchLastEntryOffset);

                // iterate over entries
                while (searchEntryOffset <= searchLastEntryOffset) {
                    co_await read(searchSectorOffset + searchEntryOffset, sizeof(Entry));
                    if (buffer_.size() < int(sizeof(Entry))) {
                        // something went wrong
                        co_return;
                    }
                    Entry &searchEntry = buffer_.cast<Entry &>();

                    // check if entry is valid
                    if (isEntryValid(searchEntryOffset, searchDataOffset, searchEntry)) {
                        // check if found
                        if (searchEntry.id == tailEntry.id)
                            goto found;

                        if (searchEntry.size > SMALL_SIZE) {
                            // set new data offset
                            searchDataOffset = searchEntry.offset << offsetShift_;
                        }
                    }
                    searchEntryOffset += entrySize_;
                }

                // go to next sector
                searchSectorIndex = searchSectorIndex + 1 == info_.sectorCount ? 0 : searchSectorIndex + 1;
                searchEntryOffset = entrySize_; // skip close entry at beginning of sector
                searchDataOffset = info_.sectorSize;
            }

            // not found: copy entry if it has size > 0
            {
                int dataSize;
                if ((tailEntry.small.size & SMALL_FLAG) == 0) {
                    // not a small entry: copy data
                    dataSize = tailEntry.size;
                    int offset = dataWriteOffset_ - align(dataSize, info_.wordSize);
                    dataWriteOffset_ = offset;
                    int tailOffset = tailSectorOffset + tailDataOffset;
                    int s = dataSize;
                    while (s > 0) {
                        int capacity = buffer_.capacity() & ~(info_.wordSize - 1);
                        int toCopy = std::min(s, capacity);

                        co_await read(tailOffset, toCopy);
                        // todo: check if read successful

                        co_await write(sectorOffset_ + offset, toCopy);
                        tailOffset += toCopy;
                        offset += toCopy;
                        s -= toCopy;
                    }
                } else {
                    // small entry
                    dataSize = tailEntry.small.size & 3;
                }

                // write entry if not empty (with inline data if tailEntry.size <= SMALL_SIZE)
                if (dataSize > 0)
                    co_await writeEntry(tailEntry.id, dataSize, tailEntry.small.data);
            }
found:
            ;
        }
        tailEntryOffset += entrySize_;
    }

    // erase sector at tail
    co_await eraseSector(tailSectorIndex);
}

} // namespace coco
