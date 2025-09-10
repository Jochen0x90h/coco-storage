#include <coco/BufferStorage.hpp>
#include <coco/debug.hpp>
#include <coco/PseudoRandom.hpp>
#include <coco/StreamOperators.hpp>
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
    debug::out << "Capacity: " << dec(capacity) << '\n';

    // measure time
    auto start = loop.now();
#ifndef NATIVE
    // indicate start
    debug::set(debug::BLUE);
#endif

    int result;

    // clear storage
    co_await storage.clear(result);
    if (result != Storage::OK) {
        // fail
        debug::out << "Error: Clear\n";
#ifndef NATIVE
        debug::set(debug::RED);
#endif
        co_return;
    }

    for (int i = 0; i < 10000; ++i) {
        if (i % 100 == 0) {
            debug::out << dec(i) << '\n';
#ifndef NATIVE
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
            debug::out << "Error: Write (" << dec(i) << ")\n";
#ifndef NATIVE
            debug::set(debug::YELLOW);
#endif
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
                debug::out << "Error: Check size (" << dec(i) << '/' << dec(index) << ")\n";
#ifndef NATIVE
                debug::set(debug::MAGENTA);
#endif
                co_return;
            }

            // check data
            for (int j = 0; j < size; ++j) {
                if (buffer[j] != uint8_t(id + j)) {
                    // fail
                    debug::out << "Error: Check data (" << dec(i) << '/' << dec(index) << ")\n";
#ifndef NATIVE
                    debug::set(debug::CYAN);
#endif
                    co_return;
                }
            }
        }

        // mount storage and check again if everything is correctly stored
        co_await storage.mount(result);
        if (result != Storage::OK) {
            // fail
            debug::out << "Error: Mount (" << dec(i) << ")\n";
#ifndef NATIVE
            debug::set(debug::BLUE);
#endif
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
                debug::out << "Error: Check size 2 (" << dec(i) << '/' << dec(index) << ")\n";
#ifndef NATIVE
                debug::set(debug::MAGENTA);
#endif
                co_return;
            }

            // check data
            for (int j = 0; j < size; ++j) {
                if (buffer[j] != uint8_t(id + j)) {
                    // fail
                    debug::out << "Error: Check data 2 (" << dec(i) << '/' << dec(index) << ")\n";
#ifndef NATIVE
                    debug::set(debug::CYAN);
#endif
                    co_return;
                }
            }
        }

        //co_await loop.sleep(200ms);
    }

    // success
    debug::out << "Success!\n";

    // measure duration
    auto end = loop.now();
    debug::out << "Duration: " << dec(int((end - start) / 1s)) << "s\n";

#ifdef NATIVE
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
