all: cache_aligned_allocator pool_allocator huge_page_allocator

cache_aligned_allocator:
	g++ -Wall -O3 -std=c++17 -march=native $@.cc -o $@ -lpthread

pool_allocator:
	g++ -Wall -O3 -std=c++17 -march=native $@.cc -o $@ -lpthread

huge_page_allocator:
	g++ -Wall -O3 -std=c++17 -march=native $@.cc -o $@ -lpthread

clean:
	rm -f cache_aligned_allocator pool_allocator huge_page_allocator