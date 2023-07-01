#include "Storage_Buffer.hpp"
#include <coco/debug.hpp>
//#include <crc.hpp>


namespace coco {

//namespace {
// see https://www.mikrocontroller.net/attachment/91385/crc16.c

// generated in protocolTest.cpp
const uint16_t crc16Table[] {
	0x0000, 0x1021, 0x2042, 0x3063, 0x4084, 0x50a5, 0x60c6, 0x70e7, 0x8108, 0x9129, 0xa14a, 0xb16b, 0xc18c, 0xd1ad, 0xe1ce, 0xf1ef,
	0x1231, 0x0210, 0x3273, 0x2252, 0x52b5, 0x4294, 0x72f7, 0x62d6, 0x9339, 0x8318, 0xb37b, 0xa35a, 0xd3bd, 0xc39c, 0xf3ff, 0xe3de,
	0x2462, 0x3443, 0x0420, 0x1401, 0x64e6, 0x74c7, 0x44a4, 0x5485, 0xa56a, 0xb54b, 0x8528, 0x9509, 0xe5ee, 0xf5cf, 0xc5ac, 0xd58d,
	0x3653, 0x2672, 0x1611, 0x0630, 0x76d7, 0x66f6, 0x5695, 0x46b4, 0xb75b, 0xa77a, 0x9719, 0x8738, 0xf7df, 0xe7fe, 0xd79d, 0xc7bc,
	0x48c4, 0x58e5, 0x6886, 0x78a7, 0x0840, 0x1861, 0x2802, 0x3823, 0xc9cc, 0xd9ed, 0xe98e, 0xf9af, 0x8948, 0x9969, 0xa90a, 0xb92b,
	0x5af5, 0x4ad4, 0x7ab7, 0x6a96, 0x1a71, 0x0a50, 0x3a33, 0x2a12, 0xdbfd, 0xcbdc, 0xfbbf, 0xeb9e, 0x9b79, 0x8b58, 0xbb3b, 0xab1a,
	0x6ca6, 0x7c87, 0x4ce4, 0x5cc5, 0x2c22, 0x3c03, 0x0c60, 0x1c41, 0xedae, 0xfd8f, 0xcdec, 0xddcd, 0xad2a, 0xbd0b, 0x8d68, 0x9d49,
	0x7e97, 0x6eb6, 0x5ed5, 0x4ef4, 0x3e13, 0x2e32, 0x1e51, 0x0e70, 0xff9f, 0xefbe, 0xdfdd, 0xcffc, 0xbf1b, 0xaf3a, 0x9f59, 0x8f78,
	0x9188, 0x81a9, 0xb1ca, 0xa1eb, 0xd10c, 0xc12d, 0xf14e, 0xe16f, 0x1080, 0x00a1, 0x30c2, 0x20e3, 0x5004, 0x4025, 0x7046, 0x6067,
	0x83b9, 0x9398, 0xa3fb, 0xb3da, 0xc33d, 0xd31c, 0xe37f, 0xf35e, 0x02b1, 0x1290, 0x22f3, 0x32d2, 0x4235, 0x5214, 0x6277, 0x7256,
	0xb5ea, 0xa5cb, 0x95a8, 0x8589, 0xf56e, 0xe54f, 0xd52c, 0xc50d, 0x34e2, 0x24c3, 0x14a0, 0x0481, 0x7466, 0x6447, 0x5424, 0x4405,
	0xa7db, 0xb7fa, 0x8799, 0x97b8, 0xe75f, 0xf77e, 0xc71d, 0xd73c, 0x26d3, 0x36f2, 0x0691, 0x16b0, 0x6657, 0x7676, 0x4615, 0x5634,
	0xd94c, 0xc96d, 0xf90e, 0xe92f, 0x99c8, 0x89e9, 0xb98a, 0xa9ab, 0x5844, 0x4865, 0x7806, 0x6827, 0x18c0, 0x08e1, 0x3882, 0x28a3,
	0xcb7d, 0xdb5c, 0xeb3f, 0xfb1e, 0x8bf9, 0x9bd8, 0xabbb, 0xbb9a, 0x4a75, 0x5a54, 0x6a37, 0x7a16, 0x0af1, 0x1ad0, 0x2ab3, 0x3a92,
	0xfd2e, 0xed0f, 0xdd6c, 0xcd4d, 0xbdaa, 0xad8b, 0x9de8, 0x8dc9, 0x7c26, 0x6c07, 0x5c64, 0x4c45, 0x3ca2, 0x2c83, 0x1ce0, 0x0cc1,
	0xef1f, 0xff3e, 0xcf5d, 0xdf7c, 0xaf9b, 0xbfba, 0x8fd9, 0x9ff8, 0x6e17, 0x7e36, 0x4e55, 0x5e74, 0x2e93, 0x3eb2, 0x0ed1, 0x1ef0,
};

uint16_t crc16(const void *data, int size, uint16_t crc) {
	auto *d = reinterpret_cast<const uint8_t *>(data);
	for (int i = 0; i < size; ++i) {
		crc = crc16Table[(crc >> 8) ^ d[i]] ^ (crc << 8);
	}
	return crc;
}

//} // anonymous namespace


const Storage::State &Storage_Buffer::state() {
	return this->stat;
}

AwaitableCoroutine Storage_Buffer::mount(Result &result) {
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

			setOffset(sectorOffset, Command::READ);
			co_await buffer.read(sizeof(Entry));
			if (buffer.size() < sizeof(Entry)) {
				// something went wrong
				result = Result::FATAL_ERROR;
				this->stat = State::READY;
				co_return;
			}
			if (buffer.value<Entry>().empty()) {
				// sector is empty or open: read first entry
				setOffset(sectorOffset + this->entrySize, Command::READ);
				co_await this->buffer.read(sizeof(Entry));
				if (buffer.size() < sizeof(Entry)) {
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

	// make sure the next sector is empty which is not the case when copy of tail to empty sector was interrupted
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
}

AwaitableCoroutine Storage_Buffer::clear(Result &result) {
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

	result = Result::OK;
	this->stat = State::READY;
}

AwaitableCoroutine Storage_Buffer::read(int id, void *data, int &size, Result &result) {
	// check state
	if (this->stat != State::READY) {
		assert(false);
		size = 0;
		result = Result::NOT_READY;
		co_return;
	}

	// check ready
	if (uint32_t(id) > 0xffff) {
		assert(false);
		size = 0;
		result = Result::INVALID_ID;
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
			if (buffer.size() < sizeof(Entry)) {
				// something went wrong
				result = Result::FATAL_ERROR;
				this->stat = State::READY;
				co_return;
			}
			Entry &entry = buffer.value<Entry>();

			// check if entry is valid
			if (isEntryValid(entryOffset, dataOffset, entry)) {
				// check if found
				if (entry.id == id) {
					result = Result::OK;
					if (data != nullptr) {
						// determine size
						if (entry.size > size) {
							// data does not fit into the provided buffer
							result = Result::READ_SIZE_EXCEEDED;
						} else {
							size = entry.size;
						}

						// read data
						int offset = sectorOffset + entry.offset;
						uint8_t *d = reinterpret_cast<uint8_t *>(data);
						int s = size;
						while (s > 0) {
							int toRead = std::min(s, buffer.size());
							setOffset(offset, Command::READ);
							co_await buffer.read(toRead);
							int read = buffer.transferred();
							if (read < toRead) {
								// something went wrong
								result = Result::FATAL_ERROR;
								this->stat = State::READY;
								co_return;
							}
							std::copy(buffer.begin(), buffer.begin() + read, d);
							offset += read;
							d += read;
							s -= read;
						}
						this->stat = State::READY;
						co_return;
					} else {
						// data is nullptr: return size of element
						size = entry.size;
						this->stat = State::READY;
						co_return;
					}
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
	size = 0;
	result = Result::OK;
	this->stat = State::READY;
}

AwaitableCoroutine Storage_Buffer::write(int id, const void *data, int size, Result &result) {
	// check state
	if (this->stat != State::READY) {
		assert(false);
		result = Result::NOT_READY;
		co_return;
	}

	// check id
	if (uint32_t(id) > 0xffff) {
		assert(false);
		result = Result::INVALID_ID;
		co_return;
	}

	// check size, must fit into a sector which has at least two entries (one for the single entry and one for closing)
	if (uint32_t(size) > uint32_t(this->info.sectorSize - this->entrySize * 2)) {
		assert(false);
		result = Result::WRITE_SIZE_EXCEEDED;
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
			result = Result::OUT_OF_MEMORY;
			co_return;
		}

		// close current sector and go to next sector (which is erased)
		co_await closeSector();

		co_await gc(this->sectorIndex);
	}

	// write data
	int offset = this->dataWriteOffset - align(size, this->info.blockSize);
	this->dataWriteOffset = offset;
	auto d = reinterpret_cast<const uint8_t *>(data);
	int s = size;
	while (s > 0) {
		int toWrite = std::min(s, buffer.size());
		//buffer.resize(toWrite);
		std::copy(d, d + toWrite, buffer.begin());
		setOffset(this->sectorOffset + offset, Command::WRITE);
		co_await buffer.write(toWrite);
		offset += toWrite;
		d += toWrite;
		s -= toWrite;
	}

	// write entry
	co_await writeEntry(id, size);

	result = Result::OK;
	this->stat = State::READY;
}

void Storage_Buffer::setOffset(uint32_t offset, Command command) {
	offset += this->info.address;
	switch (this->info.type) {
	case Type::MEM_4N:
	case Type::FLASH_4N:
		this->buffer.setHeader(offset);
		break;
	case Type::MEM_1C2B:
	case Type::FLASH_1C2B:
		{
			uint8_t header[3];
			header[0] = this->info.commands[int(command)];
			header[1] = offset >> 8;
			header[2] = offset;
			this->buffer.setHeader(header);
		}
		break;
	}
}

bool Storage_Buffer::isEntryValid(int entryOffset, int dataOffset, const Entry &entry) {
	// check checksum
	if (entry.checksum != calcChecksum(entry))
		return false;

	// check if offset is aligned to block size (which is power of two)
	if ((entry.offset & (this->info.blockSize - 1)) != 0)
		return false;

	// check if data is in valid range
	if (entry.offset < entryOffset + this->entrySize || entry.offset + entry.size > dataOffset)
		return false;

	return true;
}

AwaitableCoroutine Storage_Buffer::detectOffsets(int sectorIndex, std::pair<int, int>& offsets) {
	auto &buffer = this->buffer;
	int sectorOffset = sectorIndex * this->info.sectorSize;
	int entryOffset = this->entrySize;
	int dataOffset = this->info.sectorSize;

	// iterate over entries
	while (entryOffset <= dataOffset) {
		// read next entry
		setOffset(sectorOffset + entryOffset, Command::READ);
		co_await buffer.read(sizeof(Entry));
		if (buffer.size() < sizeof(Entry)) {
			// something went wrong
			break;
		}
		auto &entry = buffer.value<Entry>();
		//flash.readBlocking(sector + entryOffset, &entry, sizeof(entry));

		// end of list is indicated by an empty entry
		if (entry.empty())
			break;

		// check if entry is valid
		if (isEntryValid(entryOffset, dataOffset, entry)) {
			// set new data offset
			dataOffset = entry.offset;
		}
		entryOffset += this->entrySize;
	}

	// check if data is actually empty and does not contain incomplete writes
	// check from entryOffset (behind last entry) to dataOffset (start of data of last entry)
	//uint8_t buffer[BUFFER_SIZE];
	int size = dataOffset - entryOffset;
	int o = entryOffset;
	while (size > 0) {
		int toCheck = std::min(size, buffer.size());

		setOffset(sectorOffset + o, Command::READ);
		co_await buffer.read(toCheck);
		//this->flash.readBlocking(sector + o, buffer, toCheck);

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

AwaitableCoroutine Storage_Buffer::getLastEntry(int sectorOffset, int &entryOffsetResult) {
	auto &buffer = this->buffer;

	// read close entry (assumption is that it is present and valid)
	//Entry entry;
	//flash.readBlocking(sector, &entry, sizeof(entry));
	{
		setOffset(sectorOffset, Command::READ);
		co_await buffer.read(sizeof(Entry));
		if (buffer.size() < sizeof(Entry)) {
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
			entryOffsetResult = entry.offset;
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
		if (buffer.size() < sizeof(Entry)) {
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

			// set new data offset
			dataOffset = entry.offset;
		}
		entryOffset += this->entrySize;
	}

	entryOffsetResult = validOffset;
}

Awaitable<Buffer::State> Storage_Buffer::writeEntry(uint16_t id, uint16_t size) {
	auto &buffer = this->buffer;

	// create entry
	//buffer.resize(sizeof(Entry));
	auto &entry = buffer.value<Entry>();
	entry.id = id;
	entry.size = size;
	entry.offset = this->dataWriteOffset;
	entry.checksum = calcChecksum(entry);

	// get offset and advance entry write offset
	int offset = this->sectorOffset + this->entryWriteOffset;
	this->entryWriteOffset += this->entrySize;

	// write entry
	setOffset(offset, Command::WRITE);
	return buffer.write(sizeof(Entry));
}


Awaitable<Buffer::State> Storage_Buffer::closeSector() {
	auto &buffer = this->buffer;

	// create entry
	//buffer.resize(sizeof(Entry));
	auto &entry = buffer.value<Entry>();
	entry.id = 0xffff;
	entry.size = 0;
	entry.offset = this->entryWriteOffset - this->entrySize;
	entry.checksum = calcChecksum(entry);

	// get offset
	int offset = this->sectorOffset;

	// use next sector
	this->sectorIndex = this->sectorIndex + 1 == this->info.sectorCount ? 0 : this->sectorIndex + 1;
	this->sectorOffset = this->sectorIndex * this->info.sectorSize;
	this->entryWriteOffset = this->entrySize;
	this->dataWriteOffset = this->info.sectorSize;

	// write close entry at end of sector
	setOffset(offset, Command::WRITE);
	return buffer.write(sizeof(Entry));
}

bool Storage_Buffer::isCloseEntryValid(const Entry &entry) {
	// check checksum
	if (entry.checksum != calcChecksum(entry))
		return false;

	// check if index is 0xffff and length is 0
	if (entry.id != 0xffff && entry.size != 0)
		return false;

	// check if offset of last entry is a multiple of entry size (which is power of two)
	if ((entry.offset & (this->entrySize - 1)) != 0)
		return false;

	// check if there is at least one entry and the offset is inside the sector
	if (entry.offset >= this->entrySize && entry.offset < this->info.sectorSize)//this->info.sectorSize - this->entrySize)
		return false;

	return true;
}

AwaitableCoroutine Storage_Buffer::eraseSector(int index) {
	auto &buffer = this->buffer;
	int sectorOffset = index * this->info.sectorSize;

	if (this->info.type < Type::FLASH_4N) {
		// generic memory: explicitly fill with 0xff
		int s = this->info.sectorSize;
		int offset = sectorOffset;
		while (s > 0) {
			int toWrite = std::min(s, buffer.size());
			//buffer.resize(toWrite);
			std::fill(buffer.begin(), buffer.end(), 0xff);
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

AwaitableCoroutine Storage_Buffer::gc(int emptySectorIndex) {
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
		//Entry entry;
		//this->flash.readBlocking(tailSector + entryOffset, &entry, sizeof(entry));
		setOffset(tailSectorOffset + tailEntryOffset, Command::READ);
		co_await buffer.read(sizeof(Entry));
		if (buffer.size() < sizeof(Entry)) {
			// something went wrong
			co_return;
		}
		Entry tailEntry = buffer.value<Entry>();

		if (isEntryValid(tailEntryOffset, tailDataOffset, tailEntry)) {
			// check if the entry is outdated (contains a newer entry with same id)
			int searchSectorIndex = tailSectorIndex;
			int searchEntryOffset = tailEntryOffset + this->entrySize;
			int searchDataOffset = tailEntry.offset;

			// search in all sectors
			for (int i = 0; i < this->info.sectorCount - 1; ++i) {
				int searchSectorOffset = searchSectorIndex * this->info.sectorSize;

				// get offset of last entry in allocation table
				//int lastEntryOffset = getLastEntry(sector);
				int searchLastEntryOffset;
				co_await getLastEntry(searchSectorOffset, searchLastEntryOffset);

				// iterate over entries
				while (searchEntryOffset <= searchLastEntryOffset) {
					//Entry e;
					//this->flash.readBlocking(sector + entryOffset, &e, sizeof(e));
					setOffset(searchSectorOffset + searchEntryOffset, Command::READ);
					co_await buffer.read(sizeof(Entry));
					if (buffer.size() < sizeof(Entry)) {
						// something went wrong
						co_return;
					}
					Entry &searchEntry = buffer.value<Entry>();

					// check if entry is valid
					if (isEntryValid(searchEntryOffset, searchDataOffset, searchEntry)) {
						// check if found
						if (searchEntry.id == tailEntry.id)
							goto found;

						// set new data offset
						searchDataOffset = searchEntry.offset;
					}
					searchEntryOffset += this->entrySize;
				}

				// go to next sector
				searchSectorIndex = searchSectorIndex + 1 == info.sectorCount ? 0 : searchSectorIndex + 1;
				searchEntryOffset = this->entrySize;
				searchDataOffset = this->info.sectorSize;
			}

			// not found: copy entry from tail to head
			{
				int offset = this->dataWriteOffset - align(tailEntry.size, this->info.blockSize);
				this->dataWriteOffset = offset;
				int tailOffset = tailSectorOffset + tailEntry.offset;
				//uint8_t *d = reinterpret_cast<uint8_t *>(data);
				int s = tailEntry.size;
				while (s > 0) {
					int toCopy = std::min(s, buffer.size());
					setOffset(tailOffset, Command::READ);
					co_await buffer.read(toCopy);
					// todo: check if read successful

					setOffset(this->sectorOffset + offset, Command::WRITE);
					co_await buffer.write(toCopy);
					offset += toCopy;
					tailOffset += toCopy;
					s -= toCopy;
				}

				// write entry
				co_await writeEntry(tailEntry.id, tailEntry.size);
			}
found:

			//if (!contains(entry, tailSectorIndex, tailEntryOffset + this->entrySize)) {
			//	// no: copy it from tail to head
			//	copyEntry(tailSector, tailEntry);
			//}

			// set new data offset, only for verification
			tailDataOffset = tailEntry.offset;
		}
		tailEntryOffset += this->entrySize;
	}

	// erase sector at tail
	co_await eraseSector(tailSectorIndex);
}

} // namespace coco
