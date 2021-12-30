#include <cstdlib>
#include <limits>
#include <iostream>
#include <vector>
#include <new>
#include <chrono>
#include <sys/mman.h>

template <typename T, size_t HugePageSize = 1 << 21> 
class THPAllocator {
public:
    using is_always_equal = std::true_type;
    using value_type = T;

    template <typename U>
    struct rebind {
        using other = THPAllocator<U, HugePageSize>;
    };

    THPAllocator() = default;

    template <class U>
    constexpr THPAllocator(const THPAllocator<U>& other) {
        (void) other;
    }

    T *allocate(size_t n) {
        if (n > std::numeric_limits<std::size_t>::max() / sizeof(T)) {
            throw std::bad_alloc();
        }
        const auto total_size = n * sizeof(T);
        void *p = nullptr;
        if (posix_memalign(&p, HugePageSize, total_size) != 0) {
            throw std::bad_alloc();
        }

        madvise(p, total_size, MADV_HUGEPAGE);
        if (p == nullptr) {
            throw std::bad_alloc();
        }

        return static_cast<T *>(p);
    }

    void deallocate(T *p, size_t n) { 
        (void) n;
        free(p); 
    }
};


// BENCHMARK CODE

template <typename T, typename Allocator = std::allocator<T>>
int64_t run_benchmark() {
    // We allocate 8 MB worth of integers
    constexpr const int kNumElems = (1 << 23) / sizeof(int);
    std::vector<int, Allocator> l;

    auto start = std::chrono::steady_clock::now();
    for (int i = 0; i < kNumElems; i++) {
        l.emplace_back(i);
    }

    auto stop = std::chrono::steady_clock::now();
    return std::chrono::duration_cast<std::chrono::microseconds>(stop - start).count();
}
 

int main() {

    constexpr int iterations = 100;
    int64_t std_alloc_mean = 0;
    int64_t custom_alloc_mean = 0;

    for (int i = 0; i < iterations; i++) {
        std_alloc_mean += run_benchmark<int>();
        custom_alloc_mean += run_benchmark<int, THPAllocator<int>>();
    }

    std::cout << "std::allocator mean: " << std_alloc_mean / iterations << " μs\n";
    std::cout << "THPAllocator   mean: " << custom_alloc_mean / iterations << " μs\n";

    return 0;
}