#include "BufferStorage.hpp"
#include <coco/align.hpp>
#include <coco/bits.hpp>
#include <coco/debug.hpp>


// todo: check if an entry has the same content when writing

namespace coco {

// small entries where data can be stored in the entry
constexpr int SMALL_SIZE = 3;
constexpr int SMALL_FLAG = 0x80;

BufferStorage::BufferStorage(const Info &info, Buffer &buffer)
    : info(info), buffer(buffer), semaphore(1)
{
    assert(info.blockSize >= 1 && firstBit(info.blockSize) == info.blockSize);
    assert(info.pageSize >= 1 && firstBit(info.pageSize) == info.pageSize);
    assert(info.sectorSize >= 1 && info.sectorSize % info.pageSize == 0 && info.sectorSize <= 32768 * info.blockSize);
    assert(info.sectorCount >= 2);

    // align size of allocation table entry to flash block size
    this->entrySize = int(sizeof(Entry) + info.blockSize - 1) & ~(info.blockSize - 1);

    // calc offset shift
    this->offsetShift = 0;
    for (int i = 1; i < info.blockSize; i <<= 1)
        ++this->offsetShift;

    // set header size of the buffer
    switch (info.type) {
    case Type::MEM_4N:
    case Type::FLASH_4N:
        this->buffer.headerResize(4);
        break;
    case Type::MEM_1C2B:
    case Type::FLASH_1C2B:
        this->buffer.headerResize(3);
        break;
    }
}

const Storage::State &BufferStorage::state() {
    return this->stat;
}

AwaitableCoroutine BufferStorage::mount(int &result) {
    // acquire semaphore
    co_await this->semaphore.untilAcquired();
    Semaphore::Guard guard(this->semaphore);

    this->stat = State::BUSY;
    auto &buffer = this->buffer;
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
    int lastI = this->info.sectorCount - 1;
    SectorState lastState;
    int head = 0;
    bool foundEmpty = false;
    auto foundState = SectorState::EMPTY;
    for (int i = -1; i < this->info.sectorCount; ++i) {
        // detect sector state
        SectorState sectorState;
        {
            int sectorIndex = i == -1 ? lastI : i;
            int sectorOffset = sectorIndex * this->info.sectorSize;

            // read close indicator at start of sector
            setOffset(sectorOffset, Command::READ);
            co_await buffer.read(sizeof(Entry));
            if (buffer.size() < int(sizeof(Entry))) {
                // something went wrong
                result = Result::FATAL_ERROR;
                this->stat = State::READY;
                co_return;
            }
            if (buffer.value<Entry>().empty()) {
                // sector is empty or open: read first data entry (second entry from start of sector)
                setOffset(sectorOffset + this->entrySize, Command::READ);
                co_await this->buffer.read(sizeof(Entry));
                if (buffer.size() < int(sizeof(Entry))) {
                    // something went wrong
                    result = Result::FATAL_ERROR;
                    this->stat = State::READY;
                    co_return;
                }
                if (buffer.value<Entry>().empty()) {
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
    int next = head + 1 == this->info.sectorCount ? 0 : head + 1;
    co_await eraseSector(next);


    switch (foundState) {
    case SectorState::EMPTY:
        // this happens if the flash is empty, make sure the sector is really empty
        //flash.eraseSectorBlocking(head);
        this->sectorIndex = head;
        this->sectorOffset = this->sectorIndex * this->info.sectorSize;

        // set entry and data offsets
        this->entryWriteOffset = this->entrySize;
        this->dataWriteOffset = this->info.sectorSize;
        break;
    case SectorState::OPEN: {
        // typical case where one sector is open for write
        this->sectorIndex = head;
        this->sectorOffset = this->sectorIndex * this->info.sectorSize;

        // set entry and data offsets
        std::pair<int, int> offsets;
        co_await detectOffsets(head, offsets);
        this->entryWriteOffset = offsets.first;
        this->dataWriteOffset = offsets.second;
        break;
    }
    case SectorState::CLOSED:
        // we were interrupted in the garbage collection process

        // go to next sector
        this->sectorIndex = next;
        this->sectorOffset = this->sectorIndex * this->info.sectorSize;

        // set entry and data offsets
        this->entryWriteOffset = this->entrySize;
        this->dataWriteOffset = this->info.sectorSize;

        // garbage collect tail sector
        co_await gc(next);

        break;
    }
    result = OK;
    this->stat = State::READY;
}

AwaitableCoroutine BufferStorage::clear(int &result) {
    // acquire semaphore
    co_await this->semaphore.untilAcquired();
    Semaphore::Guard guard(this->semaphore);

    this->stat = State::BUSY;

    co_await this->buffer.acquire();
//debug::set(debug::MAGENTA);

    // erase flash
    for (int i = 0; i < this->info.sectorCount; ++i) {
        co_await eraseSector(i);
    }
//debug::set(debug::CYAN);

    // initialize member variables
    this->sectorIndex = 0;
    this->sectorOffset = 0;
    this->entryWriteOffset = this->entrySize;
    this->dataWriteOffset = this->info.sectorSize;

    result = OK;
    this->stat = State::READY;
}

AwaitableCoroutine BufferStorage::read(int id, void *data, int size, int &result) {
    // acquire semaphore
    co_await this->semaphore.untilAcquired();
    Semaphore::Guard guard(this->semaphore);

    uint8_t *dst = reinterpret_cast<uint8_t *>(data);

    // fill data with zeros
    std::fill(dst, dst + size, 0);

    // check state
    if (this->stat != State::READY) {
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
    this->stat = State::BUSY;
    auto &buffer = this->buffer;

    // get sector info (allocation table starts from front, data from back)
    int sectorIndex = this->sectorIndex;
    int sectorOffset = this->sectorOffset;
    int entryOffset = this->entryWriteOffset - this->entrySize;
    int dataOffset = this->info.sectorSize;

    // iterate over sectors
    int i = 0;
    while (true) {
        // iterate over allocation table entries from last to first (newest to oldest)
        while (entryOffset > 0) {
            // read entry
            setOffset(sectorOffset + entryOffset, Command::READ);
            co_await buffer.read(sizeof(Entry));
            if (buffer.size() < int(sizeof(Entry))) {
                // something went wrong
                result = FATAL_ERROR;
                this->stat = State::READY;
                co_return;
            }
            Entry &entry = buffer.value<Entry>();

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
                        int offset = sectorOffset + (entry.offset << this->offsetShift);
                        while (s > 0) {
                            int capacity = buffer.capacity() & ~(this->info.blockSize - 1);
                            int toRead = std::min(s, capacity);

                            setOffset(offset, Command::READ);
                            co_await buffer.read(toRead);
                            int read = buffer.size();
                            if (read < toRead) {
                                // something went wrong
                                this->stat = State::READY;
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
                    this->stat = State::READY;
                    co_return;
                }
            }
            entryOffset -= this->entrySize;
        }

        ++i;
        if (i == this->info.sectorCount - 1)
            break;

        // go to previous sector
        sectorIndex = sectorIndex == 0 ? info.sectorCount - 1 : sectorIndex - 1;
        sectorOffset = sectorIndex * this->info.sectorSize;

        // get offset of last entry in allocation table
        co_await getLastEntry(sectorOffset, entryOffset);
    }

    // not found (which is ok)
    this->stat = State::READY;
    result = 0;
}

AwaitableCoroutine BufferStorage::write(int id, const void *data, int size, int &result) {
    // acquire semaphore
    co_await this->semaphore.untilAcquired();
    Semaphore::Guard guard(this->semaphore);

    // check state
    if (this->stat != State::READY) {
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
    if (uint32_t(size) > uint32_t(this->info.sectorSize - this->entrySize * 2)) {
        assert(false);
        result = WRITE_SIZE_EXCEEDED;
        co_return;
    }
    this->stat = State::BUSY;
    auto &buffer = this->buffer;

    // check if entry exists and has same data
    // todo

    // check if entry will fit
    int gcCount = 0;
    while (this->entryWriteOffset + this->entrySize + size > this->dataWriteOffset) {
        // data does not fit, we need to start a new sector

        // check if all sectors were already garbage collected which means we are out of memory
        ++gcCount;
        if (gcCount >= this->info.sectorCount) {
            result = OUT_OF_MEMORY;
            co_return;
        }

        // close current sector and go to next sector (which is erased)
        co_await closeSector();

        co_await gc(this->sectorIndex);
    }

    // write data
    auto src = reinterpret_cast<const uint8_t *>(data);
    if (size > SMALL_SIZE) {
        int offset = this->dataWriteOffset - align(size, this->info.blockSize);
        this->dataWriteOffset = offset;
        int s = size;
        while (s > 0) {
            int capacity = buffer.capacity() & ~(this->info.blockSize - 1);
            int toWrite = std::min(s, capacity);

            setOffset(this->sectorOffset + offset, Command::WRITE);
            std::copy(src, src + toWrite, buffer.data());
            co_await buffer.write(toWrite);
            offset += toWrite;
            src += toWrite;
            s -= toWrite;
        }
    }

    // write entry (with inline data if size <= SMALL_SIZE)
    co_await writeEntry(id, size, src);

    result = size;
    this->stat = State::READY;
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

void BufferStorage::setOffset(uint32_t offset, Command command) {
    offset += this->info.address;
    switch (this->info.type) {
    case Type::MEM_4N:
    case Type::FLASH_4N:
        this->buffer.header<uint32_t>() = offset;
        break;
    case Type::MEM_1C2B:
    case Type::FLASH_1C2B:
        {
            uint8_t *header = this->buffer.headerData();
            header[0] = this->info.commands[int(command)];
            header[1] = offset >> 8;
            header[2] = offset;
        }
        break;
    }
}

bool BufferStorage::isEntryValid(int entryOffset, int dataOffset, const Entry &entry) {
    // check checksum
    if (entry.checksum != calcChecksum(entry))
        return false;

    if ((entry.small.size & SMALL_FLAG) == 0) {
        // not a small entry: check if data is in valid range
        int offset = entry.offset << this->offsetShift;
        if (offset < entryOffset + this->entrySize || offset + entry.size > dataOffset)
            return false;
    }

    return true;
}

AwaitableCoroutine BufferStorage::detectOffsets(int sectorIndex, std::pair<int, int>& offsets) {
    auto &buffer = this->buffer;
    int sectorOffset = sectorIndex * this->info.sectorSize;
    int entryOffset = this->entrySize;
    int dataOffset = this->info.sectorSize;

    // iterate over entries
    while (entryOffset <= dataOffset) {
        // read next entry
        setOffset(sectorOffset + entryOffset, Command::READ);
        co_await buffer.read(sizeof(Entry));
        if (buffer.size() < int(sizeof(Entry))) {
            // something went wrong
            break;
        }
        auto &entry = buffer.value<Entry>();

        // end of list is indicated by an empty entry
        if (entry.empty())
            break;

        // check if entry is valid
        if (isEntryValid(entryOffset, dataOffset, entry) && (entry.small.size & SMALL_FLAG) == 0) {
            // set new data offset
            dataOffset = entry.offset << this->offsetShift;
        }
        entryOffset += this->entrySize;
    }

    // check if data is actually empty and does not contain incomplete writes
    // check from entryOffset (behind last entry) to dataOffset (start of data of last entry)
    int size = dataOffset - entryOffset;
    int o = entryOffset;
    while (size > 0) {
        int capacity = buffer.capacity() & ~(this->info.blockSize - 1);
        int toCheck = std::min(size, capacity);

        setOffset(sectorOffset + o, Command::READ);
        co_await buffer.read(toCheck);

        int read = buffer.size();
        for (int i = 0; i < read; ++i) {
            if (buffer[i] != 0xff) {
                // down-align to block size
                dataOffset = (o + i) & ~(this->info.blockSize - 1);
            }
        }
        size -= toCheck;
        o += toCheck;
    }

    offsets = {entryOffset, dataOffset};
}

AwaitableCoroutine BufferStorage::getLastEntry(int sectorOffset, int &entryOffsetResult) {
    auto &buffer = this->buffer;

    // read close entry (assumption is that it is present and valid)
    {
        setOffset(sectorOffset, Command::READ);
        co_await buffer.read(sizeof(Entry));
        if (buffer.size() < int(sizeof(Entry))) {
            // something went wrong
            entryOffsetResult = -1;
            co_return;
        }
        auto &entry = buffer.value<Entry>();

        // check if empty, should not happen as the sector is assumed to be closed
        // todo: report malfunction
        //if (entry.isEmpty())
        //	return 0;

        // return offset if close entry is valid
        if (isCloseEntryValid(entry)) {
            entryOffsetResult = entry.offset << this->offsetShift;
            co_return;
        }
    }

    // close entry is not valid, therefore detect last entry
    int entryOffset = this->entrySize;
    int validOffset = 0;
    int dataOffset = this->info.sectorSize;

    // iterate over entries
    while (entryOffset <= dataOffset) {
        // read next entry
        setOffset(sectorOffset + entryOffset, Command::READ);
        co_await buffer.read(sizeof(Entry));
        if (buffer.size() < int(sizeof(Entry))) {
            // something went wrong
            entryOffsetResult = -1;
            co_return;
        }
        auto &entry = buffer.value<Entry>();

        // end of list is indicated by an empty entry
        if (entry.empty())
            break;

        // check if entry is valid
        if (isEntryValid(entryOffset, dataOffset, entry)) {
            validOffset = entryOffset;

            if (entry.size > SMALL_SIZE) {
                // set new data offset
                dataOffset = entry.offset << this->offsetShift;
            }
        }
        entryOffset += this->entrySize;
    }

    entryOffsetResult = validOffset;
}

Awaitable<Buffer::Events> BufferStorage::writeEntry(int id, int size, const uint8_t *data) {
    auto &buffer = this->buffer;

    // set offset and advance entry write offset
    int offset = this->sectorOffset + this->entryWriteOffset;
    setOffset(offset, Command::WRITE);
    this->entryWriteOffset += this->entrySize;

    // create entry
    auto &entry = buffer.value<Entry>();
    entry.id = id;
    if (size > SMALL_SIZE) {
        entry.size = size;
        entry.offset = this->dataWriteOffset >> this->offsetShift;
    } else {
        // small entry: inline data
        entry.small.size = SMALL_FLAG | size;
        std::copy(data, data + size, entry.small.data);
        std::fill(entry.small.data + size, entry.small.data + 3, 0xff); // fill unused bytes with 0xff to reduce flash wear
    }
    entry.checksum = calcChecksum(entry);

    // write entry
    return buffer.write(sizeof(Entry));
}


Awaitable<Buffer::Events> BufferStorage::closeSector() {
    auto &buffer = this->buffer;

    // create entry
    auto &entry = buffer.value<Entry>();
    entry.id = 0xffff;
    entry.size = 0;
    entry.offset = (this->entryWriteOffset - this->entrySize) >> this->offsetShift; // offset of last entry in sector, gets used by getLastEntry()
    entry.checksum = calcChecksum(entry);

    // offset of close indicator entry at start of sector (index 0)
    int offset = this->sectorOffset;

    // use next sector
    this->sectorIndex = this->sectorIndex + 1 == this->info.sectorCount ? 0 : this->sectorIndex + 1;
    this->sectorOffset = this->sectorIndex * this->info.sectorSize;
    this->entryWriteOffset = this->entrySize;
    this->dataWriteOffset = this->info.sectorSize;

    // write close entry at start of sector
    setOffset(offset, Command::WRITE);
    return buffer.write(sizeof(Entry));
}

bool BufferStorage::isCloseEntryValid(const Entry &entry) {
    // check checksum
    if (entry.checksum != calcChecksum(entry))
        return false;

    // check if index is 0xffff and length is 0
    if (entry.id != 0xffff && entry.size != 0)
        return false;

    // check if there is at least one entry and the offset is inside the sector
    int offset = entry.offset << this->offsetShift;
    if (offset >= this->entrySize && offset < this->info.sectorSize)
        return false;

    return true;
}

AwaitableCoroutine BufferStorage::eraseSector(int index) {
    auto &buffer = this->buffer;
    int sectorOffset = index * this->info.sectorSize;

    if (this->info.type < Type::FLASH_4N) {
        // generic memory: explicitly fill with 0xff
        int s = this->info.sectorSize;
        int offset = sectorOffset;
        while (s > 0) {
            int capacity = buffer.capacity() & ~(this->info.blockSize - 1);
            int toWrite = std::min(s, capacity);

            std::fill(buffer.data(), buffer.data() + capacity, 0xff);
            setOffset(offset, Command::WRITE);
            co_await buffer.write(toWrite);
            offset += toWrite;
            s -= toWrite;
        }
    } else {
        // flash: use page erase
        for (int offset = 0; offset < this->info.sectorSize; offset += this->info.pageSize) {
            setOffset(sectorOffset + offset, Command::ERASE);
            co_await buffer.erase();
//debug::set(debug::YELLOW);
        }
    }
}

AwaitableCoroutine BufferStorage::gc(int emptySectorIndex) {
    // get sector at tail
    int tailSectorIndex = emptySectorIndex + 1 == this->info.sectorCount ? 0 : sectorIndex + 1;
    int tailSectorOffset = tailSectorIndex * this->info.sectorSize;

    // copy all entries from sector at tail to the sector at head
    int tailEntryOffset = this->entrySize;
    int tailDataOffset = this->info.sectorSize;
    int tailLastEntryOffset;
    co_await getLastEntry(tailSectorOffset, tailLastEntryOffset);
    while (tailEntryOffset <= tailLastEntryOffset) {
        // read entry
        setOffset(tailSectorOffset + tailEntryOffset, Command::READ);
        co_await buffer.read(sizeof(Entry));
        if (buffer.size() < int(sizeof(Entry))) {
            // something went wrong
            co_return;
        }
        Entry tailEntry = buffer.value<Entry>();

        if (isEntryValid(tailEntryOffset, tailDataOffset, tailEntry)) {
            if (tailEntry.size > SMALL_SIZE) {
                // set new data offset, only for verification
                tailDataOffset = tailEntry.offset << this->offsetShift;
            }

            // check if the entry is outdated (contains a newer entry with same id)
            int searchSectorIndex = tailSectorIndex;
            int searchEntryOffset = tailEntryOffset + this->entrySize;
            int searchDataOffset = tailDataOffset;

            // search in all sectors
            for (int i = 0; i < this->info.sectorCount - 1; ++i) {
                int searchSectorOffset = searchSectorIndex * this->info.sectorSize;

                // get offset of last entry in allocation table
                int searchLastEntryOffset;
                co_await getLastEntry(searchSectorOffset, searchLastEntryOffset);

                // iterate over entries
                while (searchEntryOffset <= searchLastEntryOffset) {
                    setOffset(searchSectorOffset + searchEntryOffset, Command::READ);
                    co_await buffer.read(sizeof(Entry));
                    if (buffer.size() < int(sizeof(Entry))) {
                        // something went wrong
                        co_return;
                    }
                    Entry &searchEntry = buffer.value<Entry>();

                    // check if entry is valid
                    if (isEntryValid(searchEntryOffset, searchDataOffset, searchEntry)) {
                        // check if found
                        if (searchEntry.id == tailEntry.id)
                            goto found;

                        if (searchEntry.size > SMALL_SIZE) {
                            // set new data offset
                            searchDataOffset = searchEntry.offset << this->offsetShift;
                        }
                    }
                    searchEntryOffset += this->entrySize;
                }

                // go to next sector
                searchSectorIndex = searchSectorIndex + 1 == info.sectorCount ? 0 : searchSectorIndex + 1;
                searchEntryOffset = this->entrySize; // skip close entry at beginning of sector
                searchDataOffset = this->info.sectorSize;
            }

            // not found: copy entry if it has size > 0
            {
                int dataSize;
                if ((tailEntry.small.size & SMALL_FLAG) == 0) {
                    // not a small entry: copy data
                    dataSize = tailEntry.size;
                    int offset = this->dataWriteOffset - align(dataSize, this->info.blockSize);
                    this->dataWriteOffset = offset;
                    int tailOffset = tailSectorOffset + tailDataOffset;
                    int s = dataSize;
                    while (s > 0) {
                        int capacity = buffer.capacity() & ~(this->info.blockSize - 1);
                        int toCopy = std::min(s, capacity);

                        setOffset(tailOffset, Command::READ);
                        co_await buffer.read(toCopy);
                        // todo: check if read successful

                        setOffset(this->sectorOffset + offset, Command::WRITE);
                        co_await buffer.write(toCopy);
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
        tailEntryOffset += this->entrySize;
    }

    // erase sector at tail
    co_await eraseSector(tailSectorIndex);
}

} // namespace coco
