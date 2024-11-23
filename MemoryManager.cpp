#include "MemoryManager.h"
#include <algorithm>
#include <fcntl.h>
#include <unistd.h>
#include <cstring>

// Constructor initializes the memory manager with specified word size and allocation strategy
// wordSize: The size of each memory word in bytes
// allocator: A function pointer to the allocation strategy (e.g., bestFit or worstFit)
MemoryManager::MemoryManager(unsigned wordSize, std::function<int(int, void*)> allocator) 
    : unit_size(wordSize), selector(allocator), storage_area(nullptr), total_capacity(0) {}

// Destructor ensures proper cleanup of allocated memory
MemoryManager::~MemoryManager() {
    shutdown();
}

// Combines adjacent free memory regions to prevent fragmentation
// This is called after freeing memory to consolidate available space
void MemoryManager::merge_adjacent_regions() {
    if (memory_regions.size() < 2) return;
    
    auto current = memory_regions.begin();
    while (current != memory_regions.end() - 1) {
        auto next = current + 1;
        // If both current and next regions are available, merge them
        if (current->available && next->available) {
            current->extent += next->extent;  // Add next region's size to current
            memory_regions.erase(next);       // Remove the next region
        } else {
            ++current;
        }
    }
}

// Checks if a given memory address is within the managed memory space
bool MemoryManager::validate_address(void* addr) const {
    uint8_t* ptr = static_cast<uint8_t*>(addr);
    return ptr >= storage_area && ptr < storage_area + (total_capacity * unit_size);
}

// Converts byte count to word count, rounding up if necessary
size_t MemoryManager::convert_to_words(size_t bytes) const {
    return (bytes + unit_size - 1) / unit_size;
}

// Initializes the memory manager with a specified size
// Creates a single free region spanning the entire memory space
void MemoryManager::initialize(size_t sizeInWords) {
    shutdown();  // Clean up any existing allocation
    
    storage_area = new uint8_t[sizeInWords * unit_size];
    total_capacity = sizeInWords;
    memory_regions.clear();
    // Create initial region covering all memory, marked as available
    memory_regions.emplace_back(0, sizeInWords, true);
    allocation_table.clear();
}

// Cleans up all allocated memory and resets the manager state
void MemoryManager::shutdown() {
    delete[] storage_area;
    storage_area = nullptr;
    total_capacity = 0;
    memory_regions.clear();
    allocation_table.clear();
}

// Allocates memory of requested size using the selected allocation strategy
void* MemoryManager::allocate(size_t sizeInBytes) {
    if (!storage_area) return nullptr;
    
    size_t words_required = convert_to_words(sizeInBytes);
    // Get list of available regions and apply allocation strategy
    uint16_t* available_regions = static_cast<uint16_t*>(getList());
    int chosen_offset = selector(words_required, available_regions);
    delete[] available_regions;
    
    if (chosen_offset == -1) return nullptr;  // No suitable region found
    
    // Find the selected region in our internal tracking
    auto region_it = std::find_if(memory_regions.begin(), memory_regions.end(),
        [chosen_offset](const Region& r) { 
            return r.available && r.position == static_cast<size_t>(chosen_offset);
        });
        
    if (region_it == memory_regions.end()) return nullptr;
    
    // If the region is larger than needed, split it
    if (region_it->extent > words_required) {
        memory_regions.emplace_back(
            region_it->position + words_required,
            region_it->extent - words_required,
            true
        );
        region_it->extent = words_required;
    }
    
    // Mark region as allocated and record it
    region_it->available = false;
    void* allocated_memory = storage_area + (chosen_offset * unit_size);
    allocation_table[reinterpret_cast<uintptr_t>(allocated_memory)] = sizeInBytes;
    
    return allocated_memory;
}

// Frees previously allocated memory
void MemoryManager::free(void* address) {
    if (!address || !validate_address(address)) return;
    
    uintptr_t addr_key = reinterpret_cast<uintptr_t>(address);
    auto alloc_it = allocation_table.find(addr_key);
    if (alloc_it == allocation_table.end()) return;  // Address not allocated
    
    // Calculate offset in words from start of storage
    size_t offset = (static_cast<uint8_t*>(address) - storage_area) / unit_size;
    
    // Find the corresponding region
    auto region_it = std::find_if(memory_regions.begin(), memory_regions.end(),
        [offset](const Region& r) { return r.position == offset && !r.available; });
        
    if (region_it != memory_regions.end()) {
        region_it->available = true;
        allocation_table.erase(alloc_it);
        merge_adjacent_regions();  // Combine with any adjacent free regions
    }
}

// Changes the allocation strategy
void MemoryManager::setAllocator(std::function<int(int, void*)> allocator) {
    selector = allocator;
}

// Creates a text file showing the current memory map
// Format: [start, size] for each free region
int MemoryManager::dumpMemoryMap(char* filename) {
    int fd = open(filename, O_RDWR | O_CREAT | O_TRUNC, 0777);
    if (fd == -1) return -1;

    // Collect and sort holes by position
    std::vector<std::pair<size_t, size_t>> holes;
    for (const auto& region : memory_regions) {
        if (region.available) {
            holes.emplace_back(region.position, region.extent);
        }
    }
    std::sort(holes.begin(), holes.end());

    if (holes.empty()) {
        const char* msg = "No holes";
        write(fd, msg, strlen(msg));
    } else {
        char buffer[32];
        bool first = true;

        for (const auto& hole : holes) {
            if (!first) write(fd, " - ", 3);
            
            write(fd, "[", 1);
            int len = snprintf(buffer, sizeof(buffer), "%lu", hole.first);
            write(fd, buffer, len);
            write(fd, ", ", 2);
            len = snprintf(buffer, sizeof(buffer), "%lu", hole.second);
            write(fd, buffer, len);
            write(fd, "]", 1);
            
            first = false;
        }
    }

    close(fd);
    return 0;
}

// Returns a list of available memory regions
// Format: [count, pos1, size1, pos2, size2, ...]
void* MemoryManager::getList() {
    std::vector<std::pair<size_t, size_t>> free_regions;
    for (const auto& region : memory_regions) {
        if (region.available) {
            free_regions.emplace_back(region.position, region.extent);
        }
    }
    
    std::sort(free_regions.begin(), free_regions.end());
    
    // Create array: [count, pos1, size1, pos2, size2, ...]
    uint16_t* region_array = new uint16_t[free_regions.size() * 2 + 1];
    region_array[0] = free_regions.size();
    
    for (size_t i = 0; i < free_regions.size(); ++i) {
        region_array[i * 2 + 1] = free_regions[i].first;   // position
        region_array[i * 2 + 2] = free_regions[i].second;  // extent
    }
    
    return region_array;
}

// Creates a bitmap representation of memory
// 1 = allocated, 0 = free
// First two bytes contain the size of the bitmap
void* MemoryManager::getBitmap() {
    // Calculate required bytes for bitmap
    size_t bytes_needed = total_capacity / 8;
    if(total_capacity % 8 != 0) {
        bytes_needed++; 
    }
    uint8_t* result = new uint8_t[bytes_needed + 2];
    
    // Store size in little-endian
    result[0] = static_cast<uint8_t>(bytes_needed & 0xFF);
    result[1] = static_cast<uint8_t>((bytes_needed >> 8) & 0xFF);

    std::memset(result + 2, 0, bytes_needed);
    
    // Track status of each word
    std::vector<bool> word_status(total_capacity, true);  // Default to allocated
    
    // Mark free regions
    for (const auto& region : memory_regions) {
        if (region.available) {
            for (size_t i = 0; i < region.extent; ++i) {
                word_status[region.position + i] = false;
            }
        }
    }
    
    // Convert to bitmap
    for (size_t word_idx = 0; word_idx < total_capacity; ++word_idx) {
        if (word_status[word_idx]) {
            size_t byte_pos = (word_idx / 8) + 2;
            uint8_t bit_pos = word_idx % 8;
            result[byte_pos] |= (1u << bit_pos);
        }
    }
    
    return result;
}

// Utility functions to get memory manager properties
unsigned MemoryManager::getWordSize() {
    return unit_size;
}

void* MemoryManager::getMemoryStart() {
    return storage_area;
}

unsigned MemoryManager::getMemoryLimit() {
    return total_capacity * unit_size;
}

// Best Fit allocation strategy
// Finds the smallest hole that can fit the requested size
int bestFit(int sizeInWords, void* list) {
    if (!list) return -1;
    
    uint16_t* hole_data = static_cast<uint16_t*>(list);
    size_t num_holes = hole_data[0];
    if (num_holes == 0) return -1;

    struct HoleInfo {     
        size_t start;
        size_t size;
        size_t waste;     // Tracks unused space
    };
    std::vector<HoleInfo> qualified_holes;  

    // Find all holes that can fit the request
    for (size_t i = 0; i < num_holes; i++) {
        size_t position = hole_data[2*i + 1];
        size_t length = hole_data[2*i + 2];
        
        if (length >= static_cast<size_t>(sizeInWords)) {
            qualified_holes.push_back({
                position, 
                length, 
                length - static_cast<size_t>(sizeInWords)
            });
        }
    }

    if (qualified_holes.empty()) return -1;

    // Find hole with minimum waste
    auto best_hole = std::min_element(
        qualified_holes.begin(),
        qualified_holes.end(),
        [](const HoleInfo& a, const HoleInfo& b) {
            return a.waste < b.waste;
        }
    );

    return static_cast<int>(best_hole->start);
}

// Worst Fit allocation strategy
// Finds the largest hole available
int worstFit(int sizeInWords, void* list) {
    uint16_t* holes = static_cast<uint16_t*>(list);
    if (!holes || holes[0] == 0) return -1;
    
    int max_pos = -1;
    size_t biggest_gap = 0;
    size_t word_requirement = static_cast<size_t>(sizeInWords);
    
    // Check each hole
    size_t count = holes[0];
    size_t data_idx = 1;
    
    while (count-- > 0) {
        size_t current_pos = holes[data_idx];
        size_t current_len = holes[data_idx + 1];
        
        // Find largest hole that fits
        if (current_len >= word_requirement) {
            if (current_len > biggest_gap) {
                biggest_gap = current_len;
                max_pos = static_cast<int>(current_pos);
            }
        }
        data_idx += 2;  
    }
    
    return max_pos;
}