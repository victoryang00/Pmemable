#include <fcntl.h>
#include <immintrin.h>
#include <iostream>
#include <libpmem.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <unistd.h>
#include <xmmintrin.h>

#define BUFFER_SIZE 10000
#define ADDR_POOL_SIZE 15000

struct Buffer{
    int eles[128];
    int next;
};

inline void PMemPersist(void *dst) {
    // asm volatile(".byte 0x66; xsaveopt %0" : "+m"(*(volatile char *)(dst)));
    // _mm_clwb(dst);
    asm volatile("clwb (%0)" : : "r"(dst) : "memory");
    _mm_sfence();
}


void append(Buffer * buf,int ele){
    buf->eles[buf->next]=ele;
    PMemPersist(&buf->eles[buf->next]);
    buf->next ++;
    PMemPersist(&buf->next);
}

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

inline void PMemRead(void *src) {  src; }

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
    int first_val,second_val;
    char buf[BUFFER_SIZE];
    char *pmemaddr;
    size_t mapped_len;
    int is_pmem;

    // /* open src-file */
    // first_val = open("test0", O_RDONLY);

    /* create a pmem file and memory map it */
    if ((pmemaddr = reinterpret_cast<char *>(
             pmem_map_file("/root/Pmemable/src/test1", BUFFER_SIZE, PMEM_FILE_CREATE | PMEM_FILE_EXCL, 0666, &mapped_len, &is_pmem))) == NULL) {
        perror("pmem map file");
        exit(1);
    }

    std::cout << "exporiment 1\n";
    int x = 1;
    PMemCopy(pmemaddr,&x,sizeof(x));
    PMemPersist(&pmemaddr);
    PMemRead(&pmemaddr);
    x = 1024;
    PMemCopy(pmemaddr,&x,sizeof(x));
    PMemPersist(&pmemaddr);
    // PMemRead(&pmemaddr);

    std::cout << "exporiment 2\n";


    

    // std::cout << "exporiment 3\n";
    // char **first = reinterpret_cast<char **>(malloc(sizeof(char *) * ADDR_POOL_SIZE));
    // char **second = reinterpret_cast<char **>(malloc(sizeof(char *) * ADDR_POOL_SIZE));
    // char **third = reinterpret_cast<char **>(malloc(sizeof(char *) * ADDR_POOL_SIZE));
    // int number_of_reads = 5000;
    // volatile size_t *base_addr = (volatile size_t *)first;
    // volatile size_t *probe_addr = (volatile size_t *)second;
    // while (number_of_reads-- > 0) {
    //     *base_addr;
    //     *base_addr;
    //     *probe_addr;
    //     *probe_addr;

    //     PMemPersist((void *)base_addr);
    //     PMemPersist((void *)probe_addr);
    // }
    // PMemCopy((void *)third, (void *)first, sizeof(char *) * ADDR_POOL_SIZE);

    return 0;
}