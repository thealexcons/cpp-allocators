#include <stack>
#include <memory>
#include <list>
#include <chrono>
#include <iostream>

template <size_t BlockSize, size_t ReservedBlocks = 0>
class Pool {
private:
    size_t size_;
    std::stack<void *> addrs_;
    std::stack<std::unique_ptr<uint8_t[]>> blocks_;

public:
    explicit Pool(size_t size) : size_(size) {
        for (size_t i = 0; i < ReservedBlocks; i++) {
            add_more_addresses();
        }
    }

    void* allocate() {
        if (addrs_.empty()) {
            add_more_addresses();
        }

        auto ptr = addrs_.top();
        addrs_.pop();
        return ptr;
    }

    void deallocate(void *ptr) {
        addrs_.push(ptr);
    }

    /* Rebind should only be called by STL containers when they need to create
       an allocator for an internal node-like structure from the value_type allocator.
       This means that the original allocator must not have been used yet, so we
       are free to reassign the size_ field safely. */
    void rebind(size_t size) {
        if (!(addrs_.empty() && blocks_.empty())) {
            std::cerr << "Cannot call Pool::rebind() after an allocation\n";
            abort();
        }

        size_ = size;
    }

private:
    // Refill the address stack by allocating another block of memory
    void add_more_addresses() {
        auto block = std::make_unique<uint8_t[]>(BlockSize);
        auto total_size = BlockSize % size_ == 0 ? BlockSize : BlockSize - size_;

        // Divide the allocated block into chunks of size_ bytes, and add their address
        for (size_t i = 0; i < total_size; i += size_) {
            addrs_.push(&block.get()[i]);
        }

        // Keep the memory of the block alive by adding it to our stack
        blocks_.push(std::move(block));
    }
};

template <typename T, size_t BlockSize = 4096, size_t ReservedBlocks = 0>
class PoolAllocator {
private:
    using PoolType = Pool<BlockSize, ReservedBlocks>;
    std::shared_ptr<PoolType> pool_;

public:
    using value_type = T;
    using is_always_equal = std::false_type;

    PoolAllocator() : pool_(std::make_shared<PoolType>(sizeof(T))) {}

    // Rebind copy constructor
    template <typename U>
    PoolAllocator(const PoolAllocator<U>& other) : pool_{other.pool_} {
        pool_->rebind(sizeof(T));
    }

    template <typename U>
    struct rebind {
        using other = PoolAllocator<U, BlockSize, ReservedBlocks>;
    };

    PoolAllocator(const PoolAllocator& other) = default;
    PoolAllocator(PoolAllocator&& other) = default;
    PoolAllocator& operator=(const PoolAllocator& other) = default;
    PoolAllocator& operator=(PoolAllocator&& other) = default;

    T* allocate(size_t n) {
        if (n > 1) {
            // For n > 1, resort to using malloc
            return static_cast<T*>(malloc(sizeof(T) * n));
        }

        return static_cast<T*>(pool_->allocate());
    }

    void deallocate(T* ptr, size_t n) {
        if (n > 1) {
            free(ptr);
            return;
        }

        pool_->deallocate(ptr);
    }
};


// BENCHMARK CODE

template <typename T, typename Allocator = std::allocator<T>>
int64_t run_benchmark() {
    constexpr const int kNumElems = 1000000;
    std::list<int, Allocator> l;

    auto start = std::chrono::steady_clock::now();
    for (int i = 0; i < kNumElems; i++) {
        l.emplace_back(i);
    }

    auto stop = std::chrono::steady_clock::now();
    return std::chrono::duration_cast<std::chrono::microseconds>(stop - start).count();
}



int main() {
    constexpr int iterations = 10;
    int64_t std_alloc_mean = 0;
    int64_t custom_alloc_no_reserved_mean = 0;
    int64_t custom_alloc_100_reserved_mean = 0;
    int64_t custom_alloc_1000_reserved_mean = 0;

    for (int i = 0; i < iterations; i++) {
        std_alloc_mean += run_benchmark<int>();
        custom_alloc_no_reserved_mean += run_benchmark<int, PoolAllocator<int>>();
        custom_alloc_100_reserved_mean += run_benchmark<int, PoolAllocator<int, 4096, 100>>();
        custom_alloc_1000_reserved_mean += run_benchmark<int, PoolAllocator<int, 4096, 1000>>();
    }

    std::cout << "std::allocator            mean: "  << std_alloc_mean / iterations << " μs\n";
    std::cout << "PoolAllocator<4096, 0>    mean: "  << custom_alloc_no_reserved_mean / iterations << " μs\n";
    std::cout << "PoolAllocator<4096, 100>  mean: "  << custom_alloc_100_reserved_mean / iterations << " μs\n";
    std::cout << "PoolAllocator<4096, 1000> mean: " << custom_alloc_1000_reserved_mean / iterations << " μs\n";

    return 0;
}