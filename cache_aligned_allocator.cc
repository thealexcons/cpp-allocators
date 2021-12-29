#include <iostream>
#include <vector>
#include <thread>
#include <mutex>
#include <random>
#include <atomic>
#include <chrono>
#include <assert.h>

template <typename T, size_t Alignment = 64>
class CacheAlignedAllocator {
public:
    using value_type = T;
    using is_always_equal = std::true_type;

    template <typename U>
    struct rebind {
        using other = CacheAlignedAllocator<U, Alignment>;
    };

    CacheAlignedAllocator() = default;

    template <typename U>
    CacheAlignedAllocator(const CacheAlignedAllocator<U, Alignment>& other) {
        (void) other;
    }

    T* allocate(size_t n) {
        auto ptr = static_cast<T*>(aligned_alloc(Alignment, sizeof(T) * n));
        if (ptr)
            return ptr;

        throw std::bad_alloc();
    }

    void deallocate(T* ptr, size_t n) {
        (void) n;
        free(ptr);
    }
};

// BENCHMARK CODE

void pin_thread(int cpu) {
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(cpu, &cpuset);
    if (pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset) == -1) {
        perror("pthread_setaffinity_np failed");
        exit(EXIT_FAILURE);
    }
}

template <typename T, typename Allocator = std::allocator<T>>
int64_t run_benchmark() {
    const static int kNumThreads = std::thread::hardware_concurrency();
    const static int kNumElems = kNumThreads;

    std::random_device rd;
    std::mt19937 rng(rd());
    std::uniform_int_distribution<int> uni(0, kNumElems-1);

    std::vector<int, Allocator> vec(kNumElems, 0);
    std::vector<std::mutex> mutexes(kNumElems);
    std::vector<std::thread> threads;

    auto start = std::chrono::steady_clock::now();

    // Writing threads - these will invalidate the cache of the reading threads
    for (int i = 0; i < kNumThreads / 2; i++) {
        threads.emplace_back([&] () {
            pin_thread(i);

            for (int j = 0; j < 1000000; j++) {
                for (int k = 0; k < kNumElems; k++) {
                    std::scoped_lock<std::mutex> lock(mutexes[k]);
                    vec[k] = uni(rng);
                }
            }
        });
    }

    // Reading threads
    for (int i = kNumThreads / 2; i < kNumThreads; i++) {
        threads.emplace_back([&] () {
            pin_thread(i);
            
            volatile int r = 0;
            for (int j = 0; j < 1000000; j++) {
                for (int k = 0; k < kNumElems; k++) {
                    std::scoped_lock<std::mutex> lock(mutexes[k]);
                    r += vec[k];
                }
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    auto stop = std::chrono::steady_clock::now();
    return std::chrono::duration_cast<std::chrono::milliseconds>(stop - start).count();
}

int main() {
    constexpr int iterations = 10;
    int64_t std_alloc_mean = 0;
    int64_t custom_alloc_mean = 0;

    for (int i = 0; i < iterations; i++) {
        std_alloc_mean += run_benchmark<int>();
        custom_alloc_mean += run_benchmark<int, CacheAlignedAllocator<int>>();
    }

    std::cout << "std::allocator mean: " << std_alloc_mean / iterations << '\n';
    std::cout << "CachedAlignedAllocator mean: " << custom_alloc_mean / iterations << '\n';

    return 0;
}