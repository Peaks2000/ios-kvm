/* Regression checks for framing: empty writes and descriptor reuse must not corrupt a stream. */
#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint8_t written[128], incoming[128];
static int write_calls, written_size, incoming_size, read_at;
static int virgl_raw_write(int fd, void *data, int size)
{
    (void)fd;
    assert(size >= 0 && size <= sizeof(written));
    memcpy(written, data, size);
    write_calls++; written_size = size;
    return size;
}
static int virgl_raw_read(int fd, void *data, int size)
{
    (void)fd;
    if (size > incoming_size-read_at) return -EIO;
    memcpy(data, incoming+read_at, size);
    read_at += size;
    return size;
}
#include "gpu-transport.h"

static void packet(const char *text)
{
    uint32_t header[] = {(uint32_t)strlen(text), 0};
    memcpy(incoming, header, sizeof(header));
    memcpy(incoming+8, text, header[0]);
    incoming_size = 8 + header[0]; read_at = 0;
}
int main(void)
{
    assert(virgl_block_write(4, NULL, 0) == 0 && write_calls == 0);
    assert(virgl_block_write(4, NULL, -1) == -EINVAL && write_calls == 0);
    assert(virgl_block_write(4, "abc", 3) == 3 && write_calls == 1);
    assert(written_size == 11 && !memcmp(written+8, "abc", 3));
    uint32_t header[2]; memcpy(header, written, 8);
    assert(header[0] == 3 && header[1] == 0);
    char data[8] = {0};
    packet("abcdefg");
    assert(virgl_block_read(4, data, 3) == 3 && !memcmp(data, "abc", 3));
    assert(virgl_block_read(4, data+3, 4) == 4 && !memcmp(data, "abcdefg", 7));
    ios_kvm_reset_frames(4);
    packet("abcdefg");
    assert(virgl_block_read(4, data, 3) == 3);
    ios_kvm_reset_frames(4); /* A new connection reused the descriptor with a partial old frame. */
    packet("hi");
    assert(virgl_block_read(4, data, 2) == 2 && !memcmp(data, "hi", 2));
    ios_kvm_reset_frames(4);
    puts("GPU transport: empty writes, framing, partial reads and descriptor reuse passed");
}
