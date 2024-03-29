#include <coco/Storage_Buffer.hpp>
#include <coco/debug.hpp>
#include <coco/PseudoRandom.hpp>
#include <StorageTest.hpp>
#ifdef NATIVE
#include <iostream>
#endif


using namespace coco;

Coroutine test(Loop &loop, Buffer &buffer2) {
	Storage_Buffer storage(storageInfo, buffer2);

	// random generator for random data
	KissRandom random;


	// table of currently stored elements
	int sizes[64] = {}; // initialize with zero
	ArrayBuffer<uint8_t, 128> buffer;

	// determine capacity (number of entries of size 128 that fit into the storage)
	int capacity = std::min(((storageInfo.sectorCount - 1) * (storageInfo.sectorSize - 8)) / (128 + 8), int(std::size(sizes))) - 1;
#ifdef NATIVE
	std::cout << "capacity: " << capacity << std::endl;

	// measure time
	auto start = loop.now();
#else
	// indicate start
	debug::setBlue();
#endif

	Storage::Result result;

	// clear storage
	co_await storage.clear(result);

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
			co_await storage.read(id, buffer, result);

			// check data
			if (buffer.size() != size) {
				// fail
				debug::set(debug::MAGENTA);
				co_return;
			}
			for (int j = 0; j < size; ++j) {
				if (buffer[j] != uint8_t(id + j)) {
					// fail
					debug::set(debug::CYAN);
					co_return;
				}
			}
		}

		// random size in range [0, 128]
		int size = random.draw() % 129;

		// generate id in range [5, capacity + 4]
		int index = random.draw() % capacity;
		int id = index + 5;
		sizes[index] = size;

		// generate data
		buffer.resize(size);
		for (int j = 0; j < size; ++j) {
			buffer[j] = id + j;
		}

		// store
		co_await storage.write(id, buffer, result);
		if (result != Storage::Result::OK) {
			// fail
			debug::set(debug::YELLOW);
			co_return;
		}

		//co_await loop.sleep(200ms);
	}

	// ok
	debug::set(debug::GREEN);

#ifdef NATIVE
	// measure time
	auto end = loop.now();
	std::cout << int((end - start) / 1s) << "s" << std::endl;

	co_await loop.yield();
	loop.exit();
#else
	// indicate success
	while (true) {
		debug::set(debug::WHITE);
		co_await loop.sleep(200ms);
		debug::set(debug::BLACK);
		co_await loop.sleep(200ms);
	}
#endif
}

int main() {
	debug::init();
	Drivers drivers;

	test(drivers.loop, drivers.buffer);

	drivers.loop.run();
}
