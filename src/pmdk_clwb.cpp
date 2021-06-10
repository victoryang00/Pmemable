#include <fcntl.h>
#include <immintrin.h>
#include <iostream>
#include <libpmem.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <unistd.h>
#include <xmmintrin.h>

#define PMEM_FILE_CREATE 0
#define PMEM_F_MEM_NONTEMPORAL 0
#define ADDR_POOL_SIZE 15000


inline void PMemCopy(void *dst, const void *src, size_t n) {
    constexpr unsigned int kGranularity = 256;
    if (n == 0)
        return;
    char *ptr = reinterpret_cast<char *>(dst);
    const char *begin = reinterpret_cast<const char *>(src);
    size_t remainder = reinterpret_cast<uintptr_t>(dst) % kGranularity;
    if (remainder != 0) {
        size_t unaligned_bytes = kGranularity - remainder;
        if (unaligned_bytes < n) {
            pmem_memcpy(ptr, begin, unaligned_bytes, PMEM_F_MEM_NONTEMPORAL);
            ptr += unaligned_bytes;
            begin += unaligned_bytes;
            n -= unaligned_bytes;
        }
    }
    const char *end = begin + n;
    for (; begin + kGranularity < end; ptr += kGranularity, begin += kGranularity) {
        pmem_memcpy(ptr, begin, kGranularity, PMEM_F_MEM_NONTEMPORAL);
    }
    pmem_memcpy(ptr, begin, end - begin, PMEM_F_MEM_NONTEMPORAL);
}

inline void PMemPersist(void *dst) {
    // asm volatile(".byte 0x66; xsaveopt %0" : "+m"(*(volatile char *)(dst)));
    // _mm_clwb(dst);
    asm volatile("clwb (%0)" : : "r"(dst) : "memory");
    // _mm_sfence();
}

inline void PMemRead(void *src) { asm volatile("movnt (%0)" : : "r"(src) : "memory"); }

inline void PMemPersistRange(void *addr, size_t len) {
    uintptr_t uptr;
    /*
     * Loop through cache-line-size (typically 64B) aligned chunks
     * covering the given range.
     */
    for (uptr = (uintptr_t)addr & ~(64 - 1); uptr < (uintptr_t)addr + len; uptr += 64) {
        asm volatile(".byte 0x66; xsaveopt %0" : "+m"(*(volatile char *)(uptr)));
    }
    _mm_sfence();
}

int main() {

    std::cout << "exporiment 1 & 2\n";
    int x = 1;
    PMemPersist(&x);
    // PMemRead(&x);
    x = 1024;
    PMemPersist(&x);
    PMemRead(&x);

    std::cout << "exporiment 3\n";
    char **first = malloc(sizeof(char *) * ADDR_POOL_SIZE);
    char **second = malloc(sizeof(char *) * ADDR_POOL_SIZE);
    char **third = malloc(sizeof(char *) * ADDR_POOL_SIZE);
    int number_of_reads = 5000;
    volatile size_t *base_addr = (volatile size_t *)first;
    volatile size_t *probe_addr = (volatile size_t *)second;
    while (number_of_reads-- > 0) {
        *base_addr;
        *base_addr;
        *probe_addr;
        *probe_addr;

        PMemPersist((void *)base_addr);
        PMemPersist((void *)probe_addr);
    }
    PMemCopy((void *)third, (void *)first, sizeof(char *) * ADDR_POOL_SIZE);

    return 0;
}