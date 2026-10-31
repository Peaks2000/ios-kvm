/* A fixed dependent integer workload, for comparing guest CPU changes. */
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

static uint64_t now(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t)t.tv_sec * 1000000 + t.tv_nsec / 1000;
}
static void *work(void *value)
{
    uint64_t x = (uintptr_t)value + 1;
    for (unsigned i = 0; i < 20000000; i++) {
        x ^= x >> 12;
        x ^= x << 25;
        x ^= x >> 27;
        x *= UINT64_C(2685821657736338717);
    }
    return (void *)(uintptr_t)x;
}
int main(void)
{
    setvbuf(stdout, NULL, _IOLBF, 0);
    for (int count = 1; count <= 4; count *= 4) {
        for (int run = 0; run < 6; run++) {
            pthread_t threads[4];
            uint64_t start = now(), checksum = 0;
            for (int i = 0; i < count; i++)
                if (pthread_create(&threads[i], NULL, work, (void *)(uintptr_t)i)) return 1;
            for (int i = 0; i < count; i++) {
                void *value;
                if (pthread_join(threads[i], &value)) return 1;
                checksum ^= (uintptr_t)value;
            }
            printf("cpu-bench: threads=%d run=%d elapsed_us=%llu checksum=%llx\n",
                   count, run, now()-start, checksum);
        }
    }
    return 0;
}
