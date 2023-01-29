#include <coco/loop.hpp>
#include <coco/debug.hpp>
#include <StorageTest.hpp>
#ifdef NATIVE	
#include <iostream>
#endif


using namespace coco;

struct Kiss32Random {
	uint32_t x;
	uint32_t y;
	uint32_t z;
	uint32_t c;

	// seed must be != 0
	explicit Kiss32Random(uint32_t seed = 123456789) {
		x = seed;
		y = 362436000;
		z = 521288629;
		c = 7654321;
	}

	int draw() {
		// Linear congruence generator
		x = 69069 * x + 12345;

		// Xor shift
		y ^= y << 13;
		y ^= y >> 17;
		y ^= y << 5;

		// Multiply-with-carry
		uint64_t t = 698769069ULL * z + c;
		c = t >> 32;
		z = (uint32_t) t;

		return (x + y + z) & 0x7fffffff;
	}
};

void fail() {
	debug::set(debug::MAGENTA);
}

void test(Loop &loop, Flash &flash, Storage &storage) {

	// random generator for random data of random length
	Kiss32Random random;


	// table of currently stored elements
	int sizes[64] = {}; // initialize with zero
	Buffer<uint8_t, 128> buffer;

	// determine capacity
	auto info = flash.getInfo();
	int capacity = std::min(((info.sectorCount - 1) * (info.sectorSize - 8)) / (128 + 8), int(std::size(sizes))) - 1;
#ifdef NATIVE
	std::cout << "capacity: " << capacity << std::endl;

	// measure time
	auto start = loop.now();
#endif

	// clear storage
	storage.clearBlocking();

	for (int i = 0; i < 10000; ++i) {
		if (i % 100 == 0) {
#ifdef NATIVE
			std::cout << i << std::endl;
#else
			debug::set(i / 100);
#endif
		}

		// check if everything is correctly stored
		for (int index = 0; index < capacity; ++index) {
			// get stored size
			int size = sizes[index];
			int id = index + 5;

			// read data
			storage.readBlocking(id, buffer);

			// check data
			if (buffer.size() != size)
				return fail();
			for (int j = 0; j < size; ++j) {
				if (buffer[j] != uint8_t(id + j))
					return fail();
			}
		}

		// random size in range [0, 128]
		int size = random.draw() % 129;

		// generate id in range [5, 68]
		int index = random.draw() % capacity;
		int id = index + 5;
		sizes[index] = size;

		// generate data
		buffer.resize(size);
		for (int j = 0; j < size; ++j) {
			buffer[j] = id + j;
		}

		// store
		if (storage.writeBlocking(id, buffer) != Storage::Status::OK)
			return fail();
	}

#ifdef NATIVE
	auto end = loop.now();
	std::cout << int((end - start) / 1s) << "s" << std::endl;
#endif

	// ok
	debug::set(debug::GREEN);
}

int main() {
	debug::init();
	debug::setBlue();
	Drivers drivers;
	debug::clearBlue();

	test(drivers.loop, drivers.flash, drivers.storage);

	drivers.loop.run();
}
