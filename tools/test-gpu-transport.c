/* Regression checks for framing: empty writes and descriptor reuse must not corrupt a stream. */
#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint8_t written[(8 << 20) + 64], incoming[128];
static int write_calls, written_size, incoming_size, read_at, fail_next;
static int virgl_raw_write(int fd, void *data, int size)
{
    (void)fd;
    if (fail_next) { fail_next = 0; return -EPIPE; }
    assert(size >= 0 && (size_t)size <= sizeof(written) - written_size);
    memcpy(written + written_size, data, size);
    write_calls++; written_size += size;
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
    uint32_t command[] = {2, 7}, body[] = {0x11223344, 0xaabbccdd};
    uint8_t expected[16]; memcpy(expected, command, 8); memcpy(expected+8, body, 8);
    unsetenv("IOS_KVM_COMMAND_PACKETS");
    pthread_once(&ios_kvm_packets_once, ios_kvm_packets_init);
    assert(ios_kvm_packets_mode == 1);
    assert(ios_kvm_packets_default_enabled(NULL));
    assert(ios_kvm_packets_default_enabled("auto"));
    assert(ios_kvm_packets_default_enabled("1"));
    assert(!ios_kvm_packets_default_enabled("0"));
    assert(!ios_kvm_packets_default_enabled("marker"));
    assert(!ios_kvm_packets_default_enabled(""));
    assert(!ios_kvm_packets_default_enabled("invalid"));
    for (int packed=0;packed<2;packed++) {
        ios_kvm_packets_mode=packed; written_size=write_calls=0;
        assert(ios_kvm_write_command(4, command, 8, body, 8)==16);
        assert(write_calls==(packed?1:2));
        uint8_t flat[16]; unsigned at=0, out=0;
        while (at<(unsigned)written_size) {
            uint32_t frame[2]; memcpy(frame,written+at,8);
            assert(!frame[1] && frame[0] && frame[0]<=sizeof(flat)-out);
            memcpy(flat+out,written+at+8,frame[0]); out+=frame[0]; at+=8+frame[0];
        }
        assert(out==16 && at==(unsigned)written_size && !memcmp(flat,expected,16));
    }
    ios_kvm_packets_mode=0; volatile uint32_t control=0; ios_kvm_packets_control=&control;
    assert(!ios_kvm_packets_enabled()); control=1; assert(ios_kvm_packets_enabled());
    control=2; assert(!ios_kvm_packets_enabled()); ios_kvm_packets_control=NULL;
    ios_kvm_packets_mode=1; written_size=write_calls=0;
    assert(ios_kvm_write_command(4,command,8,NULL,0)==8 && write_calls==1 && written_size==16);
    assert(ios_kvm_write_command(4,command,8,NULL,1)==-EINVAL);
    assert(ios_kvm_write_command(4,NULL,8,body,8)==-EINVAL);
    assert(ios_kvm_write_command(4,command,8,body,-1)==-EINVAL);
    assert(ios_kvm_write_command(4,command,8,body,(64<<20)+1)==-EMSGSIZE);
    written_size=write_calls=0; fail_next=1;
    assert(ios_kvm_write_command(4,command,8,body,8)==-EPIPE && !write_calls && !written_size);
    ios_kvm_packets_mode=0; fail_next=1;
    assert(ios_kvm_write_command(4,command,8,body,8)==-EPIPE && !write_calls && !written_size);
    uint8_t *large=malloc(4<<20); assert(large); memset(large,0x7f,4<<20);
    ios_kvm_packets_mode=1;
    assert(ios_kvm_write_command(4,command,8,large,4<<20)==(4<<20)+8 && write_calls==2);
    memcpy(header,written,8); assert(header[0]==8 && !memcmp(written+8,command,8));
    memcpy(header,written+16,8); assert(header[0]==(4<<20) && written[written_size-1]==0x7f);
    free(large);
    puts("GPU transport: equivalent command bytes, 2-to-1 writes, live control, bounds/errors, framing and descriptor reuse passed");
}
