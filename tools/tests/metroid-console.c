/* Compile with -DHOST_TEST -I<patched xnu>/osfmk/arm64. No device accesses. */
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "metroid_console.c"
unsigned char iso_font[256 * 16];
static uint32_t *pixels;
#define W 1260u
#define H 2800u
#define SENTINEL 0x12345678u
uint32_t metroid_test_read(uintptr_t a)
{
    if (a == 0xae160f4) return 2;
    if (a == 0xae16014) return 1u << 17;
    if (a == 0xae1612c) return (1u << 16) | (1u << 17);
    for (unsigned i = 0; i < 2; i++) {
        uintptr_t pipe = 0xae05000 + i * 0x2000;
        if (a == pipe) return (H << 16) | 630;
        if (a == pipe + 8) return i * 630;
        if (a == pipe + 0x14) return 0xfc800000;
        if (a == pipe + 0x24) return W * 4;
        if (a == pipe + 0x30) return 0xff | (3u << 9) | (3u << 12) | (1u << 17);
        if (a == pipe + 0x34) return 0x03020100;
    }
    return 0;
}
volatile uint32_t *metroid_test_fb(uint64_t a)
{
    assert(a == 0xfc800000);
    return pixels + 1;
}
int main(void)
{
    const size_t n = W * H;
    pixels = malloc((n + 2) * sizeof(*pixels));
    assert(pixels);
    for (size_t i = 0; i < n + 2; i++) pixels[i] = SENTINEL;
    memset(iso_font + 'A' * 16, 255, 16);
    assert(metroid_console_init() && count == 2 && cols == 76);
    /* Screen clear must cover both source rectangles, including the right half. */
    for (size_t i = 1; i <= n; i++) assert(pixels[i] == 0xff000000);
    for (unsigned i = 0; i < cols; i++) metroid_console_putc('A');
    metroid_console_string("\r\n");
    /* CR must not lose buffered characters; a glyph crosses the x=630 seam. */
    for (unsigned y = 160; y < 192; y++)
        for (unsigned x = 0; x < W; x++)
            assert(pixels[1 + y * W + x] ==
                ((x >= 16 && x < 1232) ? 0xffffffff : 0xff000000));
    uint64_t base; uint32_t width, height, stride;
    assert(metroid_console_framebuffer(&base, &width, &height, &stride));
    assert(base == 0xfc800000 && width == W && height == H && stride == W * 4);
    for (unsigned i = 0; i < 80; i++) metroid_console_string("WRAP\r\n");
    assert(pixels[0] == SENTINEL && pixels[n + 1] == SENTINEL);
    free(pixels);
    puts("PASS: split-buffer clear, seam glyph, CRLF, handoff geometry, row wrap bounds");
}
