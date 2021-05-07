#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <linux/slab.h> /* kmalloc */

#define MEM_LENGTH 1048576
#define MEM_MAX 800
int main(void) {
    char *mem = NULL;
    int i = 0;
    for (; i < MEM_MAX; i++) {
        mem = (char *)kmalloc(MEM_LENGTH);
        if (mem != NULL) {
            memset(mem, 0, MEM_LENGTH);
        }
        // if (i == 10) { sleep(1); }
    }
    while (1) {
        sleep(1);
    }
    return 1;
}