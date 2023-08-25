#pragma once

#include "Storage.hpp"
#include <coco/Buffer.hpp>


namespace coco {

/**
	Storage implementation working on a buffer with address header such as internal or external flash.

	Inspired by Zephyr
	https://docs.zephyrproject.org/latest/services/storage/nvs/nvs.html
	https://github.com/zephyrproject-rtos/zephyr/blob/main/subsys/fs/nvs/nvs.c
*/
class Storage_Buffer : public Storage {
public:
	/// Memory type
	enum class Type : uint8_t {
		/// Generic memory (file, eeprom, feram) with 4 address bytes in native byte order
		MEM_4N,

		/// Generic memory (file, eeprom, feram) with 1 command byte and 2 address bytes in big endian byte order
		MEM_1C2B,

		/// Flash with 4 address bytes in native byte order
		FLASH_4N,

		/// Flash with 1 command byte and 2 address bytes in big endian byte order
		FLASH_1C2B,
	};

	/// Memory command
	enum class Command {
		READ = 0,
		WRITE = 1,
		ERASE = 2
	};

	/// Memory info
	struct Info {
		/// Size of a page that has to be erased at once
		int pageSize;

		/// Size of a block that has to be written at once, must be power of 2
		int blockSize;

		/// Start address in memory
		uint32_t address;

		/// Size of a sector
		int sectorSize;

		/// Number of sectors, at least 2
		int sectorCount;

		/// Memory type
		Type type;

		/// Commands for serial memory (read, write, erase)
		uint8_t commands[3];
	};

	Storage_Buffer(const Info &info, Buffer &buffer)
		: info(info), buffer(buffer)
	{
		// calculate the size of an allocation table entry
		this->entrySize = int(sizeof(Entry) + info.blockSize - 1) & ~(info.blockSize - 1);
	}

	const State &state() override;
	[[nodiscard]] AwaitableCoroutine mount(Result &result) override;
	[[nodiscard]] AwaitableCoroutine clear(Result &result) override;
	[[nodiscard]] AwaitableCoroutine read(int id, void *data, int &size, Result &result) override;
	[[nodiscard]] AwaitableCoroutine write(int id, const void *data, int size, Result &result) override;
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
		struct {
			// id of entry
			uint16_t id;

			// size of data
			uint16_t size;

			// offset of data in sector
			uint16_t offset;

			// checksum of the entry
			uint16_t checksum;
		};
		uint32_t data[2];

		bool empty() {return (this->data[0] & this->data[1]) == 0xffffffff;}
	};

	static uint16_t calcChecksum(const Entry &entry) {
		return crc16(&entry, offsetof(Entry, checksum));
	}

	void setOffset(uint32_t offset, Command command);

	// check if allocation table entry is valid
	bool isEntryValid(int entryOffset, int dataOffset, const Entry &entry);

	// detect the entry and data offsets for an open sector
	AwaitableCoroutine detectOffsets(int sectorIndex, std::pair<int, int>& offsets);

	// get the offset of the last entry in a closed sector
	AwaitableCoroutine getLastEntry(int sectorOffset, int &entryOffsetResult);

	// write an entry (without data)
	Awaitable<> writeEntry(uint16_t id, uint16_t size);

	// close the current sector
	Awaitable<> closeSector();

	// check if closing allocation table entry is valid
	bool isCloseEntryValid(const Entry &entry);

	// erase a sector
	AwaitableCoroutine eraseSector(int index);

	// copy an entry in the given sector to the current sector
	void copyEntry(int sector, Entry &entry);

	// garbage collect
	AwaitableCoroutine gc(int emptySectorIndex);


	Info info;
	Buffer &buffer;
	int entrySize;

	State stat = State::NOT_MOUNTED;

	// current sector
	int sectorIndex = 0;
	int sectorOffset = 0;

	// write offsets in current sector
	int entryWriteOffset;
	int dataWriteOffset;
};

} // namespace coco
