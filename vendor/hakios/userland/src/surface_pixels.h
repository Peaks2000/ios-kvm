/* Convert opacity/blue/green/red bytes without imposing pointer alignment. */
#ifndef IOS_KVM_SURFACE_PIXELS_H
#define IOS_KVM_SURFACE_PIXELS_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
static inline void surface_alpha_first_row(void *destination, const void *source, size_t pixels, bool bgra)
{
    unsigned char *to=destination;
    const unsigned char *from=source;
    for (size_t i=0;i<pixels;i++) {
        uint32_t word;
        __builtin_memcpy(&word,from+i*4,4);
        if (bgra) {
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
            word=(word>>8)|(word<<24);
#else
            word=(word<<8)|(word>>24);
#endif
        } else {
            word=__builtin_bswap32(word);
        }
        __builtin_memcpy(to+i*4,&word,4);
    }
}
#endif
