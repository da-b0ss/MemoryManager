#ifndef MEMORY_MANAGER_H
#define MEMORY_MANAGER_H

#include <functional>
#include <deque>
#include <unordered_map>

class MemoryManager {
private:
    struct Region {
        size_t position;  // Position in words
        size_t extent;    // Size in words
        bool available;
        Region(size_t p, size_t e, bool a) : position(p), extent(e), available(a) {}
    };

    unsigned unit_size;
    std::function<int(int, void*)> selector;
    uint8_t* storage_area;
    size_t total_capacity;
    std::deque<Region> memory_regions;
    std::unordered_map<uintptr_t, size_t> allocation_table;

    void merge_adjacent_regions();
    bool validate_address(void* addr) const;
    size_t convert_to_words(size_t bytes) const;

public:
    MemoryManager(unsigned wordSize, std::function<int(int, void*)> allocator);
    ~MemoryManager();

    void initialize(size_t sizeInWords);
    void shutdown();
    void* allocate(size_t sizeInBytes);
    void free(void* address);
    void setAllocator(std::function<int(int, void*)> allocator);
    int dumpMemoryMap(char* filename);
    void* getList();
    void* getBitmap();
    unsigned getWordSize();
    void* getMemoryStart();
    unsigned getMemoryLimit();
};

int bestFit(int sizeInWords, void* list);
int worstFit(int sizeInWords, void* list);

#endif