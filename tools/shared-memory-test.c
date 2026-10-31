/* Verify host writes reach a buffer explicitly pinned by the guest. */
#include <IOKit/IOKitLib.h>
#include <mach/mach.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <sys/stat.h>

int main(int argc, char **argv)
{
    int upload = argc == 3;
    size_t bytes = upload ? strtoull(argv[2], NULL, 10) + 1 : 3 * 1024 * 1024 + 17;
    if (bytes < 2 || bytes > (64u << 20) - 64) return 1;
    if (upload) {
        if (!argv[1][0] || !strcmp(argv[1], ".") || !strcmp(argv[1], "..")) return 1;
        for (const char *p = argv[1]; *p; p++)
            if (!isalnum((unsigned char)*p) && *p != '.' && *p != '_' && *p != '-') return 1;
    }
    uint8_t *allocation = calloc(1, bytes + 64);
    volatile uint8_t *pixels = allocation + 37; /* Exercise unaligned first/last segments. */
    uint64_t segments[8192], input[] = {(uintptr_t)pixels, bytes};
    size_t count = sizeof(segments);
    io_connect_t connection = IO_OBJECT_NULL;
    io_service_t service = IOServiceGetMatchingService(kIOMainPortDefault,
        IOServiceNameMatching("IOCoreSurfaceRoot"));
    kern_return_t ret = service ? IOServiceOpen(service, mach_task_self(), 0, &connection) : KERN_FAILURE;
    if (service) IOObjectRelease(service);
    if (!allocation || ret) { fprintf(stderr, "open failed: %#x\n", ret); return 1; }
    ret = IOConnectCallMethod(connection, 0x100, input, 2, NULL, 0, NULL, NULL, segments, &count);
    if (ret || !count || count % 16) {
        fprintf(stderr, "pin failed: %#x (%zu bytes)\n", ret, count);
        IOServiceClose(connection); free(allocation); return 1;
    }
    uint64_t duplicate_segments[2];
    size_t duplicate_count = sizeof(duplicate_segments);
    kern_return_t duplicate = IOConnectCallMethod(connection, 0x100, input, 2, NULL, 0,
        NULL, NULL, duplicate_segments, &duplicate_count);
    if (!duplicate) { fprintf(stderr, "duplicate pin unexpectedly succeeded\n"); return 1; }
    printf("{\"bytes\":%zu,\"segments\":[", bytes);
    for (size_t i = 0; i < count / 16; i++)
        printf("%s[%llu,%llu]", i ? "," : "", segments[i*2], segments[i*2+1]);
    puts("]}"); fflush(stdout);
    /* Host sets the last byte only after writing the rest. */
    unsigned wait;
    for (wait = 0; wait < 600 && pixels[bytes-1] != 0xa5; wait++) usleep(100000);
    size_t bad = 0;
    if (upload) {
        char path[1024];
        snprintf(path, sizeof(path), "/var/tmp/ios-kvm-gpu/%s", argv[1]);
        FILE *file = wait < 600 ? fopen(path, "wb") : NULL;
        if (!file) bad++;
        else {
            if (fwrite((const void *)pixels, 1, bytes-1, file) != bytes-1) bad++;
            if (fclose(file)) bad++;
            if (chmod(path, 0755)) bad++;
        }
    } else {
        for (size_t i = 0; i < bytes-1; i++)
            if (pixels[i] != (uint8_t)(i * 17 + 43)) bad++;
    }
    ret = IOConnectCallMethod(connection, 0x101, NULL, 0, NULL, 0, NULL, NULL, NULL, NULL);
    uint64_t invalid[] = {UINT64_MAX - 8, 16};
    size_t invalid_count = sizeof(segments);
    kern_return_t invalid_ret = IOConnectCallMethod(connection, 0x100, invalid, 2, NULL, 0,
        NULL, NULL, segments, &invalid_count);
    if (!invalid_ret) bad++;
    /* A second pin proves the previous pin was released. Closing must release this one too. */
    input[1] = 16384; count = sizeof(segments);
    kern_return_t again = IOConnectCallMethod(connection, 0x100, input, 2, NULL, 0,
        NULL, NULL, segments, &count);
    IOServiceClose(connection);
    free(allocation);
    printf("shared-memory: %zu bytes checked, %zu mismatches, unpin=%#x repin=%#x\n", bytes, bad, ret, again);
    return wait == 600 || bad || ret || again ? 1 : 0;
}
