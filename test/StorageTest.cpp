#include <coco/BufferStorage.hpp>
#include <coco/debug.hpp>
#include <coco/PseudoRandom.hpp>
#include <StorageTest.hpp>
#ifdef NATIVE
#include <iostream>
#endif


using namespace coco;

Coroutine test(Loop &loop, Buffer &flashBuffer) {
	BufferStorage storage(storageInfo, flashBuffer);

	// random generator for random data
	KissRandom random;


	// table of currently stored elements
	int sizes[64] = {}; // initialize with zero
	uint8_t buffer[128];

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

	int result;

	// clear storage
	co_await storage.clear(result);
	if (result != Storage::OK) {
		// fail
		debug::set(debug::RED);
		co_return;
	}

	for (int i = 0; i < 10000; ++i) {
		if (i % 100 == 0) {
#ifdef NATIVE
			std::cout << i << std::endl;
#else
			debug::set(i / 100);
#endif
		}


		// generate random size in range [0, 128]
		int size = random.draw() % 129;

		// generate random index range [0, capacity -1]
		int index = random.draw() % capacity;
		sizes[index] = size;

		// generate id from index
		int id = index + 5;

		// generate data
		for (int j = 0; j < size; ++j) {
			buffer[j] = id + j;
		}

		// store
		co_await storage.write(id, buffer, size, result);
		if (result != size) {
			// fail
			debug::set(debug::YELLOW);
			co_return;
		}


		// check if everything is correctly stored
		for (int index = 0; index < capacity; ++index) {
			// get stored size
			int size = sizes[index];
			int id = index + 5;

			// read data (reads as zero length if id does not exist)
			co_await storage.read(id, buffer, result);

			// check size
			if (result != size) {
				// fail
				debug::set(debug::MAGENTA);
				co_return;
			}

			// check data
			for (int j = 0; j < size; ++j) {
				if (buffer[j] != uint8_t(id + j)) {
					// fail
					debug::set(debug::CYAN);
					co_return;
				}
			}
		}

		// mount storage and check again if everything is correctly stored
		co_await storage.mount(result);
		if (result != Storage::OK) {
			// fail
			debug::set(debug::BLUE);
			co_return;
		}
		for (int index = 0; index < capacity; ++index) {
			// get stored size
			int size = sizes[index];
			int id = index + 5;

			// read data
			co_await storage.read(id, buffer, result);

			// check size
			if (result != size) {
				// fail
				debug::set(debug::MAGENTA);
				co_return;
			}

			// check data
			for (int j = 0; j < size; ++j) {
				if (buffer[j] != uint8_t(id + j)) {
					// fail
					debug::set(debug::CYAN);
					co_return;
				}
			}
		}

		//co_await loop.sleep(200ms);
	}

#ifdef NATIVE
	// success
	std::cout << "Success!" << std::endl;

	// measure duration
	auto end = loop.now();
	std::cout << "Duration: " << int((end - start) / 1s) << "s" << std::endl;

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
