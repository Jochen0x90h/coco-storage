#pragma once

#include <coco/ArrayBuffer.hpp>
#include <coco/Coroutine.hpp>


namespace coco {

/**
	Non-volatile storage, can be implemented on top of on-chip flash, external flash or ESP-32/Zephyr NVS
	ESP-32: https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/storage/nvs_flash.html
	Zephyr: https://docs.zephyrproject.org/latest/services/storage/nvs/nvs.html
*/
class Storage {
public:
	enum class State {
		NOT_MOUNTED,
		READY,
		BUSY
	};

	enum class Result {
		// operation completed successfully, also for partial read where the element is larger than the provided buffer
		OK,

		// the storate was not in READY state
		NOT_READY,

		// element was read as zero length because of checksum error
		CHECKSUM_ERROR,

		// element was not read or written because the id is invalid
		INVALID_ID,

		// element was partially read because the buffer size was exceeded
		READ_SIZE_EXCEEDED,

		// element was not written because the maximum data size was exceeded
		WRITE_SIZE_EXCEEDED,

		// element was not written because storage is full
		OUT_OF_MEMORY,

		// memory is not usable, e.g. not connected or end of life of flash memory
		FATAL_ERROR
	};

	virtual ~Storage();

	/**
		Current state of the storage.
		Can be used for synchronous waiting using loop.run(storage.state());
	*/
	virtual const State &state() = 0;

	/**
		Mount the file system using the parameters given to the constructor of the implementation
	*/
	[[nodiscard]] virtual AwaitableCoroutine mount(Result &result) = 0;


	/**
		Clear all elements in the non-volatile storage
		@return use co_await on return value to await completion
	*/
	[[nodiscard]] virtual AwaitableCoroutine clear(Result &result) = 0;

	/**
		Read an element from the non-volatile storage into a given data buffer
		@param id id of element
		@param data data to read into or nullptr to obtain the size of the element
		@param size in: size of provided data buffer in bytes, out: size actually read or size of entry if data is nullptr
		@return use co_await on return value to await completion
	*/
	[[nodiscard]] virtual AwaitableCoroutine read(int id, void *data, int &size, Result &result) = 0;

	template <typename T, int N>
	[[nodiscard]] AwaitableCoroutine read(int id, ArrayBuffer<T, N> &buffer, Result &result) {
		buffer.length = N * sizeof(T);
		return read(id, buffer.buffer, buffer.length, result);
	}

	/**
		Get the size of an element
		@param id id of element
		@param size size of element
		@return use co_await on return value to await completion
	*/
	[[nodiscard]] virtual AwaitableCoroutine getSize(int id, int &size, Result &result) {
		return read(id, nullptr, size, result);
	}

	/**
		Write an element to the non-volatile storage
		@param id id of element
		@param data data to write
		@param size size of data to write in bytes
		@return use co_await on return value to await completion
	*/
	[[nodiscard]] virtual AwaitableCoroutine write(int id, void const *data, int size, Result &result) = 0;

	template <typename T>
	[[nodiscard]] AwaitableCoroutine write(int id, T &array, Result &result) {
		return write(id, std::data(array), std::size(array) * sizeof(*std::data(array)), result);
	}

	/**
		Erase an element, equivalent to writing data of length zero
		@param id id of element
		@return use co_await on return value to await completion
	*/
	[[nodiscard]] virtual AwaitableCoroutine erase(int id, Result &result) {
		return write(id, nullptr, 0, result);
	}
};

} // namespace coco
