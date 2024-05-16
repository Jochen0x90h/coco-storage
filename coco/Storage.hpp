#pragma once

#include <coco/ArrayBuffer.hpp>
#include <coco/Coroutine.hpp>
#include <coco/ContainerConcept.hpp>


namespace coco {

/**
	Interface for Non-volatile storage, can be implemented on top of flash or other memory types.

	Inspired by ESP-32 and Zephyr
	ESP-32: https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/storage/nvs_flash.html
	Zephyr: https://docs.zephyrproject.org/latest/services/storage/nvs/nvs.html
*/
class Storage {
public:
	/// Storage state
	enum class State {
		NOT_MOUNTED,
		READY,
		BUSY
	};

	enum Result {
		OK = 0,

		/// The storate was not in READY state
		NOT_READY = -1,

		/// Element was read as zero length because of checksum error
		CHECKSUM_ERROR = -2,

		/// Element was not read or written because the id is invalid (> 65535)
		INVALID_ID = -3,

		/// Element was not written because the maximum data size was exceeded
		WRITE_SIZE_EXCEEDED = -4,

		/// Element was not written because storage is full
		OUT_OF_MEMORY = -5,

		/// Memory is not usable, e.g. not connected or end of life of flash memory
		FATAL_ERROR = -6
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
	[[nodiscard]] virtual AwaitableCoroutine mount(int &result) = 0;


	/**
		Clear all elements in the non-volatile storage. Calling mount() is not necessary after clear.
		@return use co_await on return value to await completion
	*/
	[[nodiscard]] virtual AwaitableCoroutine clear(int &result) = 0;

	/**
		Read an element from the non-volatile storage into a given data buffer
		@param id id of element
		@param data data to read into or nullptr to obtain the size of the element
		@param size mumber of bytes to read
		@param result number of bytes actually read or negative on error (see error codes)
		@return use co_await on return value to await completion
	*/
	[[nodiscard]] virtual AwaitableCoroutine read(int id, void *data, int size, int &result) = 0;

	/// Convenience wrapper for values
	template <typename T>
	[[nodiscard]] AwaitableCoroutine read(int id, T &value, int &result) {
		return read(id, &value, sizeof(T), result);
	}

	/// Convenience wrapper for arrays
	template <typename T> requires (ArrayConcept<T>)
	[[nodiscard]] AwaitableCoroutine read(int id, T &array, int &result) {
		return read(id, std::data(array), std::size(array) * sizeof(*std::data(array)), result);
	}

	/// It is not possible to directly read into containers
	template <typename T> requires (ContainerConcept<T> && !ArrayConcept<T>)
	[[nodiscard]] AwaitableCoroutine read(int id, T &container, int &result) = delete;

	/**
		Get the size of an element
		@param id id of element
		@param size size of element
		@return use co_await on return value to await completion
	*/
	[[nodiscard]] virtual AwaitableCoroutine size(int id, int &result) {
		return read(id, nullptr, 0, result);
	}

	/**
		Write an element to the non-volatile storage
		@param id id of element
		@param data data to write
		@param size size of data to write in bytes
		@return use co_await on return value to await completion
	*/
	[[nodiscard]] virtual AwaitableCoroutine write(int id, void const *data, int size, int &result) = 0;

	/// Convenience wrapper for values
	template <typename T>
	[[nodiscard]] AwaitableCoroutine write(int id, const T &value, int &result) {
		return write(id, &value, sizeof(T), result);
	}

	/// Convenience wrapper for strings
	template <typename T> requires (CStringConcept<T>)
	[[nodiscard]] AwaitableCoroutine write(int id, const T &str, int &result) {
		String s(str);
		return write(id, s.data(), s.size(), result);
	}

	/// Convenience wrapper for arrays
	template <typename T> requires (ArrayConcept<T> && !CStringConcept<T>)
	[[nodiscard]] AwaitableCoroutine write(int id, const T &array, int &result) {
		return write(id, std::data(array), std::size(array) * sizeof(*std::data(array)), result);
	}

	/// It is not possible to directly write containers
	template <typename T> requires (ContainerConcept<T> && !ArrayConcept<T>)
	[[nodiscard]] AwaitableCoroutine write(int id, const T &container, int &result) = delete;

	/**
		Erase an element, equivalent to writing data of length zero
		@param id id of element
		@return use co_await on return value to await completion
	*/
	[[nodiscard]] virtual AwaitableCoroutine erase(int id, int &result) {
		return write(id, nullptr, 0, result);
	}
};

} // namespace coco
