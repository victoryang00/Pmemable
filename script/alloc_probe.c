#include <emmintrin.h>
#include <fcntl.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <x86intrin.h> /* for rdtscp and clflush */

#define ADDR_POOL_SIZE 15000
#define ACCESS_PRBE_NUM 5000

#define FLAGS (MAP_PRIVATE | MAP_ANONYMOUS | MAP_POPULATE)
#define PROTECTION (PROT_READ | PROT_WRITE)
#define HUGE_PAGE_SIZE 2 * 1024 * 1024

typedef struct {
    uint64_t pfn : 54;
    unsigned int soft_dirty : 1;
    unsigned int file_page : 1;
    unsigned int swapped : 1;
    unsigned int present : 1;
} PagemapEntry;

int pagemap_get_entry(PagemapEntry *entry, int pagemap_fd, uintptr_t vaddr) {
    size_t nread;
    ssize_t ret;
    uint64_t data;

    nread = 0;
    while (nread < sizeof(data)) {
        ret = pread(pagemap_fd, &data, sizeof(data), (vaddr / sysconf(_SC_PAGESIZE)) * sizeof(data) + nread);
        nread += ret;
        if (ret <= 0) {
            return 1;
        }
    }
    entry->pfn = data & (((uint64_t)1 << 54) - 1);
    entry->soft_dirty = (data >> 54) & 1;
    entry->file_page = (data >> 61) & 1;
    entry->swapped = (data >> 62) & 1;
    entry->present = (data >> 63) & 1;
    return 0;
}

int virt_to_phys_user(uintptr_t *paddr, uintptr_t vaddr) {
    int pagemap_fd;

    pagemap_fd = open("/proc/self/pagemap", O_RDONLY);
    if (pagemap_fd < 0) {
        printf("RET 1\n");
        return 1;
    }
    PagemapEntry entry;
    if (pagemap_get_entry(&entry, pagemap_fd, vaddr)) {
        printf("RET 2\n");
        return 1;
    }
    close(pagemap_fd);
    long pg_size = sysconf(_SC_PAGESIZE);
    // printf("entry.pfn: %lu\t vaddr: %p\n", entry.pfn, vaddr);
    *paddr = (entry.pfn * pg_size) + (vaddr % pg_size);
    return 0;
}

char *check_if_memory_continous(char *original_mem_addr) {
    uintptr_t pfn_prev;
    virt_to_phys_user(&pfn_prev, (uintptr_t)original_mem_addr);
    printf("+ Checking if memory is continous\n");

    size_t num_consequtive_pfn = 0;
    /* I check each 4KB page up to 512MB */
    size_t i = 1;
    for (; i < 512 * 256; i++) {
        uintptr_t pfn_tmp;
        uintptr_t addrv = (((uintptr_t)original_mem_addr)) + i * 1024 * 4; // 4KB page
        virt_to_phys_user(&pfn_tmp, addrv);
        // printf("Physical addr: %p, Virt: %lu\n", (char*)pfn_tmp, addrv);

        if (pfn_tmp != pfn_prev + 4 * 1024) { // if pfn is not consecutive
            num_consequtive_pfn = 0;
            printf("pfn_tmp: %llu != %llu\n", pfn_tmp, pfn_prev);
            pfn_prev = pfn_tmp;
            continue;
        }
        pfn_prev = pfn_tmp;
        num_consequtive_pfn += 1;
        // printf("num_consequtive_pfn: %d", num_consequtive_pfn);
        if (num_consequtive_pfn == 512) { //  2MB/4KB
            break;
        }
    }
    // if(num_consequtive_pfn != 512){
    //     printf("!!! The memory is not continous!\n");
    //     exit(1);
    // }
    uintptr_t continous_mem_adr = ((unsigned long)original_mem_addr) + (i - 512) * 1024 * 4;
    printf("+ The memory chunk is continous for the next 2MB starting at: %p\n", (char *)continous_mem_adr);
    return (char *)continous_mem_adr;
}

unsigned int arg_save_to_csv_flag = 1;
unsigned int arg_find_bits_flag = 0;
unsigned int arg_find_bits_threshold = 0;

void check_arguments(int argc, char *argv[]) {

    for (size_t i = 1; i < argc; i++) {
        char *arg = argv[i];
        if (!strcmp(arg, "-e")) {
            arg_save_to_csv_flag = 1;
        } else if (!strcmp(arg, "-t")) {
            arg_find_bits_flag = 1;
            arg_find_bits_threshold = atoi(argv[i + 1]);
            i += 1;
        }
    }
}

uint64_t rdtsc2() {
    uint64_t a, d;
    asm volatile("rdtscp" : "=a"(a), "=d"(d) : : "rcx");
    asm volatile("cpuid" ::: "rax", "rbx", "rcx", "rdx");
    a = (d << 32) | a;
    return a;
}

/** Memory polling first and record the counter and time
 * should be L3 -> Pmem or Mem
 * Some problem: fc65ae could be with 2 latency. It's rank 7
 */
uint64_t get_timing(char *first, char *second) {
    size_t min_res = (-1ull);
    for (int i = 0; i < 4; i++) {
        size_t number_of_reads = ACCESS_PRBE_NUM;
        volatile size_t *base_addr = (volatile size_t *)first;
        volatile size_t *probe_addr = (volatile size_t *)second;
        size_t t0 = rdtsc2();
        while (number_of_reads-- > 0) {
            *base_addr;
            *base_addr;
            *probe_addr;
            *probe_addr;
            // cmd.system(sudo python3 uevent.py -m CHA.LLC_LOOKUP)
            // cmd.system(sudo python3 uevent.py -m iMC.RD_CAS_RANK1.BANK8)
            asm volatile("clflush (%0)" : : "r"(base_addr) : "memory");
            asm volatile("clflush (%0)" : : "r"(probe_addr) : "memory");
            if (min_res > 600) {
                int get_time;
                get_time = get_timing_cont(base_addr, probe_addr);
            }
        }
        uint64_t res = (rdtsc2() - t0) / (ACCESS_PRBE_NUM);
        if (res < min_res)
            min_res = res;
    }
    return min_res;
}

static __attribute__((always_inline)) inline void memmove_small_avx_noflush(char *probe_addr, const char *base_addr) {

    __m256i ymm0 = _mm256_loadu_si256((__m256i *)base_addr);
    __m256i ymm1 = _mm256_loadu_si256((__m256i *)(base_addr + 64));

    _mm256_storeu_si256((__m256i *)probe_addr, ymm0);
    _mm256_storeu_si256((__m256i *)(probe_addr + 64), ymm1);
    sleep(2);
}

static __attribute__((always_inline)) inline void memmove_small_avx_nt_noflush(char *dest, const char *src) {
    // __m256i ymm0 = mm256_loadu_si256(src, 0);
    __m256i ymm0 = _mm256_loadu_si256((const __m256i *)src + 0);
    // __m256i ymm1 = mm256_loadu_si256(src, 1);
    __m256i ymm1 = _mm256_loadu_si256((const __m256i *)src + 1);

    // mm256_stream_si256(dest, 0, ymm0);
    _mm256_stream_si256((__m256i *)dest + 0, ymm0);
    asm volatile("" ::: "memory");
    // mm256_stream_si256(dest, 1, ymm1);
    _mm256_stream_si256((__m256i *)dest + 1, ymm1);
    asm volatile("" ::: "memory");

    sleep(2);
}

/** During movnt The iMC counter info will be recorded
 * memory -> avx reg -> pmem
 */
uint64_t get_timing_cont(char *first, char *second) {
    size_t min_res = (-1ull);
    for (int i = 0; i < 4; i++) {
        size_t number_of_reads = ACCESS_PRBE_NUM;
        volatile size_t *base_addr = (volatile size_t *)first;
        volatile size_t *probe_addr = (volatile size_t *)second;
        size_t t0 = rdtsc2();

        while (number_of_reads-- > 0) {
            asm volatile("movnt (%0)" : : "r"(base_addr) : "memory");
            asm volatile("movnt (%0)" : : "r"(probe_addr) : "memory");
            //cmd.system(uevent.py iMC.RD_CAS_RANK1.BANK8 -I 200)
            //char* out = cmd.out();
            //cmd.system(uevent.py iMC.RD_CAS_RANK1.BANK4 -I 200)
            //cmd.system(uevent.py iMC.WR_CAS_RANK1.BANK8 -I 200)
            //cmd.system(uevent.py iMC.WR_CAS_RANK1.BANK4 -I 200)
            //cmd.system(uevent.py iMC.WPQ_READ_HIT -I 200)

            // No such event?
            //   memmove_small_avx_noflush(probe_addr,base_addr);
            memmove_small_avx_nt_noflush(probe_addr, base_addr);
            //   fprintf(out, "%lu\n", i);
        }

        uint64_t res = (rdtsc2() - t0) / (ACCESS_PRBE_NUM);

        if (res < min_res)
            min_res = res;
    }
    return min_res;
}

/** During movnt The iMC counter info will be recorded
 * Can not know
 */
// uint64_t get_timing(char *first, char *second) {
//     size_t min_res = (-1ull);
//     for (int i = 0; i < 4; i++) {
//         size_t number_of_reads = ACCESS_PRBE_NUM;
//         volatile size_t *base_addr = (volatile size_t *)first;
//         volatile size_t *probe_addr = (volatile size_t *)second;
//         size_t t0 = rdtsc2();

//         while (number_of_reads-- > 0) {
//             asm volatile("movnt (%0)" : : "r"(base_addr) : "memory");
//             asm volatile("movnt (%0)" : : "r"(probe_addr) : "memory");
// //             cmd.system(sudo python3 uevent.py -m iMC.RD_CAS_RANK1.BANK8 -I 200ns)
// //             cmd.system(sudo python3 uevent.py -m iMC.RD_CAS_RANK1.BANK4 -I 200ns)
// //             cmd.system(sudo python3 uevent.py -m iMC.WR_CAS_RANK1.BANK8 -I 200ns)
// //             cmd.system(sudo python3 uevent.py -m iMC.WR_CAS_RANK1.BANK4 -I 200ns)
// //             cmd.system(sudo python3 uevent.py -m iMC.WPQ_READ_HIT -I 200ns)

// // No such event?

//             __m256i ymm0 = _mm256_loadu_si256((__m256i *)base_addr);
//             __m256i ymm1 = _mm256_loadu_si256((__m256i *)(base_addr + 64));

//             _mm256_storeu_si256((__m256i *)probe_addr, ymm0);
//             _mm256_storeu_si256((__m256i *)(probe_addr + 64), ymm1);
//             sleep(2);
//         }

//         uint64_t res = (rdtsc2() - t0) / (ACCESS_PRBE_NUM);

//         if (res < min_res)
//             min_res = res;
//     }
//     return min_res;
// }

char *change_bit(void *addr, int bit) { return (char *)((uint64_t)addr ^ (1 << bit)); }

void find_bits(char **conflict_pool, size_t conflict_pool_size, char *base_addr) {
    unsigned int bits_arr[20];
    for (size_t i = 0; i < 20; i++) {
        bits_arr[i] = 0;
    }

    for (size_t i = 0; i < conflict_pool_size; i++) {
        char *probe_addr = conflict_pool[i];

        for (size_t bit_i = 0; bit_i <= 20; bit_i++) {
            char *probe_addr_shifted = change_bit(probe_addr, bit_i);
            uint64_t time = get_timing(base_addr, probe_addr_shifted);
            if (time < arg_find_bits_threshold) {
                // printf("bit:%lu\n", bit_i);
                bits_arr[bit_i] = bits_arr[bit_i] + 1;
            }
        }
    }
    printf("Bits histogram:\n");
    FILE *file_bits_csv = fopen("jsi510-bits.csv", "w");

    unsigned int max_val = 0;
    // find max value
    for (size_t i = 0; i < 20; i++) {
        if (max_val < bits_arr[i])
            max_val = bits_arr[i];
    }

    for (size_t i = 0; i < 20; i++) {
        printf("bit[%lu]: %u\n", i, bits_arr[i]);
        if (bits_arr[i] > (max_val * 0.8)) { // TODO: max value from bins * 80%
            fprintf(file_bits_csv, "%lu\n", i);
        }
    }
    fclose(file_bits_csv);
}

int main(int argc, char *argv[]) {
    check_arguments(argc, argv);
    if (arg_find_bits_flag) {
        printf("+ Chosen threshold = %u\n", arg_find_bits_threshold);
    }
    printf("+ Allocating 512MB...\n");
    // char *continous_mem_addr = calloc(1024*1024*512, 1); // 512 MB
    long long x = 1024ull * 1024 * 512;
    char *continous_mem_addr = mmap(NULL, x, PROTECTION, FLAGS, -1, 0);
    if (continous_mem_addr == MAP_FAILED) {
        printf("map failed\n");
        perror("sdf");
        return -1;
    }

    if (continous_mem_addr == NULL) {
        printf("! Cannot alloc 512MB!\n");
        exit(1);
    }
    printf("+ Allocated 512MB at %p\n", continous_mem_addr);

    /* Update continous mem address */
    continous_mem_addr = check_if_memory_continous(continous_mem_addr);

    unsigned int random_constraint = HUGE_PAGE_SIZE >> 6;

    /* Create address pool */
    char **addr_pool = malloc(sizeof(char *) * ADDR_POOL_SIZE);
    for (size_t i = 0; i < ADDR_POOL_SIZE; i++) {
        char *addr_i = ((rand() % (random_constraint + 1)) << 6) + continous_mem_addr;
        // printf("rand addr: %p\n", addr_i);
        addr_pool[i] = addr_i;
        // printf("saved to pool: %p", addr_pool[i]);
    }

    /* Create pool for conflicts */
    char **conflict_addr_pool = malloc(sizeof(char *) * ADDR_POOL_SIZE);
    unsigned int conflict_addr_pool_size = 0;
    // sleep(60);
    uintptr_t base_phys;
    virt_to_phys_user(&base_phys, (uintptr_t)addr_pool[0]);

    int junk = 0;
    FILE *file_csv = NULL;
    if (arg_save_to_csv_flag) {
        file_csv = fopen("jsi510-time.csv", "w");
        fprintf(file_csv, "baseVirt,probeVirt,basePhys,probePhys,time\n");
    }

    char *base_addr = addr_pool[0];
    for (size_t probe_addr_i = 1; probe_addr_i < ADDR_POOL_SIZE; probe_addr_i++) {
        uint64_t time_sum = get_timing(base_addr, addr_pool[probe_addr_i]);
        uintptr_t probe_phys;
        virt_to_phys_user(&probe_phys, (uintptr_t)addr_pool[probe_addr_i]);
        if (time_sum > arg_find_bits_threshold) {
            conflict_addr_pool[conflict_addr_pool_size++] = addr_pool[probe_addr_i];
        }
        if (arg_save_to_csv_flag) {
            fprintf(file_csv, "%p,%p,%p,%p,%lu\n", base_addr, addr_pool[probe_addr_i], (char *)base_phys,
                    (char *)probe_phys, time_sum);
        }
    }

    if (arg_find_bits_flag) {
        find_bits(conflict_addr_pool, conflict_addr_pool_size, base_addr);
    }

    if (arg_save_to_csv_flag) {
        fclose(file_csv);
    }
    return 0;
}