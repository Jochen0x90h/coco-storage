#pragma once

#include <coco/Buffer.hpp>
#include <coco/Coroutine.hpp>


namespace coco {

/**
 * Non-volatile storage, can be implemented on top of on-chip flash, external flash or ESP-32/Zephyr NVS
 * ESP-32: https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/storage/nvs_flash.html
 * Zephyr: https://docs.zephyrproject.org/latest/services/storage/nvs/nvs.html
 */
class Storage {
public:
	enum class Status {
		// operation completed successfully, also for partial read where the element is larger than the provided buffer
		OK,

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

	struct ClearParameters {
		Status *status;
	};

	struct ReadParameters {
		int id;
		void *data;
		int *size;
		Status *status;
	};

	struct WriteParameters {
		int index;
		void const *data;
		int size;
		Status *status;
	};


	virtual ~Storage();

	/**
	 * Clear all elements in the non-volatile storage
	 * @param status status of operation
	 * @return use co_await on return value to await completion
	 */
	[[nodiscard]] virtual Awaitable<ClearParameters> clear(Status &status) = 0;

	/**
	 * Read an element from the non-volatile storage into a given data buffer
	 * @param id id of element
	 * @param data data to read into or nullptr to obtain the size of the element
	 * @param size in: size of provided data buffer in bytes, out: size actually read or size of entry if data is nullptr
	 * @param status status of operation
	 * @return use co_await on return value to await completion
	 */
	[[nodiscard]] virtual Awaitable<ReadParameters> read(int id, void *data, int &size, Status &status) = 0;

	template <typename T, int N>
	[[nodiscard]] Awaitable<ReadParameters> read(int id, Buffer<T, N> &buffer, Status &status) {
		buffer.length = N * sizeof(T);
		return read(id, buffer.buffer, buffer.length, status);
	}

	/**
	 * Get the size of an element
	 * @param id id of element
	 * @param size size of element
	 * @param status status of operation
	 * @return use co_await on return value to await completion
	 */
	[[nodiscard]] virtual Awaitable<ReadParameters> getSize(int id, int &size, Status &status) {
		return read(id, nullptr, size, status);
	}

	/**
	 * Write an element to the non-volatile storage
	 * @param id id of element
	 * @param data data to write
	 * @param size size of data to write in bytes
	 * @param status status of operation
	 * @return use co_await on return value to await completion
	 */
	[[nodiscard]] virtual Awaitable<WriteParameters> write(int id, void const *data, int size, Status &status) = 0;

	template <typename T>
	[[nodiscard]] Awaitable<WriteParameters> write(int id, T &array, Status &status) {
		return write(id, std::data(array), std::size(array) * sizeof(*std::data(array)), status);
	}

	/**
	 * Erase an element, equivalent to writing data of length zero
	 * @param id id of element
	 * @param status status of operation
	 * @return use co_await on return value to await completion
	 */
	[[nodiscard]] virtual Awaitable<WriteParameters> erase(int id, Status &status) {
		return write(id, nullptr, 0, status);
	}


	/**
	 * Clear all elements in the non-volatile storage
	 * @return status of operation
	 */
	virtual Status clearBlocking() = 0;

	/**
	 * Read an element from the non-volatile storage into a given data buffer
	 * @param id id of element
	 * @param data data to read into or nullptr to obtain the size of the element
	 * @param size in: size of provided data buffer in bytes, out: size actually read or size of entry if data is nullptr
	 * @return status of operation
	 */
	virtual Status readBlocking(int id, void *data, int &size) = 0;

	template <typename T, int N>
	Status readBlocking(int id, Buffer<T, N> &buffer) {
		buffer.length = N * sizeof(T);
		return readBlocking(id, buffer.buffer, buffer.length);
	}

	/**
	 * Get the size of an element
	 * @param id id of element
	 * @return size of the element in bytes
	 */
	Status getSizeBlocking(int id, int &size) {
		return readBlocking(id, nullptr, size);
	}

	/**
	 * Write an element to the non-volatile storage
	 * @param id id of element
	 * @param data data to write
	 * @param size size of data to write in bytes
	 * @return status of operation
	 */
	virtual Status writeBlocking(int id, void const *data, int size) = 0;

	template <typename T>
	Status writeBlocking(int id, T &array) {
		return writeBlocking(id, std::data(array), std::size(array) * sizeof(*std::data(array)));
	}

	/**
	 * Erase an element, equivalent to writing data of length zero
	 * @param id id of element
	 */
	void eraseBlocking(int id) {
		writeBlocking(id, nullptr, 0);
	}

};

} // namespace coco
