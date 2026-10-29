/*
 * probe.c - self-contained on-screen readout for the boot shim.
 *
 * Written because the console layer (console.c) does not paint on this device
 * even though its code is in the image, while raw stores to 0x9d400000
 * demonstrably do (STAGE1 filled the panel red). So this renderer uses nothing
 * from that layer:
 *
 *   - no global state (no .data, no .bss)
 *   - no font array (no .rodata table): digits are drawn as seven segments
 *     whose on/off pattern comes from a switch, i.e. immediates in code
 *   - the caller passes the framebuffer pointer in, nothing is remembered
 *
 * Hex digits 0-9a-f only, because every value we need to read out is a number.
 * Each line gets a row of coloured pips on the left so the reader can tell
 * which line is which without any lettering.
 */
#include <stdint.h>

#define PW 1080
#define PH 1920
#define DIG_W 84
#define DIG_H 150
#define DIG_GAP 22
#define SEG_T 18			/* segment thickness */

static void rect(volatile uint32_t *fb, int x, int y, int w, int h, uint32_t c)
{
	int i, j;
	if (x < 0 || y < 0 || x + w > PW || y + h > PH)
		return;
	for (j = 0; j < h; j++)
		for (i = 0; i < w; i++)
			fb[(y + j) * PW + (x + i)] = c;
}

/* Segment mask for one hex digit: bit0 a(top) b(tr) c(br) d(bottom) e(bl) f(tl) g(mid) */
static unsigned mask7(int d)
{
	switch (d) {
	case 0x0: return 0x3f;			/* a b c d e f   */
	case 0x1: return 0x06;			/*   b c         */
	case 0x2: return 0x5b;			/* a b   d e   g */
	case 0x3: return 0x4f;			/* a b c d     g */
	case 0x4: return 0x66;			/*   b c   f   g */
	case 0x5: return 0x6d;			/* a   c d   f g */
	case 0x6: return 0x7d;			/* a   c d e f g */
	case 0x7: return 0x07;			/* a b c         */
	case 0x8: return 0x7f;
	case 0x9: return 0x6f;			/* a b c d   f g */
	case 0xa: return 0x77;			/* a b c   e f g */
	case 0xb: return 0x7c;			/*   b c d e f g (b) */
	case 0xc: return 0x39;			/* a     d e f   */
	case 0xd: return 0x5e;			/*   b c d e   g (d) */
	case 0xe: return 0x79;			/* a     d e f g */
	case 0xf: return 0x71;			/* a       e f g */
	default:  return 0x00;
	}
}

static void digit(volatile uint32_t *fb, int x, int y, int d, uint32_t fg)
{
	unsigned m = mask7(d);
	int hh = DIG_H / 2;

	if (m & 0x01) rect(fb, x + SEG_T, y, DIG_W - 2 * SEG_T, SEG_T, fg);			/* a */
	if (m & 0x02) rect(fb, x + DIG_W - SEG_T, y + SEG_T, SEG_T, hh - SEG_T, fg);		/* b */
	if (m & 0x04) rect(fb, x + DIG_W - SEG_T, y + hh, SEG_T, hh, fg);			/* c */
	if (m & 0x08) rect(fb, x + SEG_T, y + DIG_H - SEG_T, DIG_W - 2 * SEG_T, SEG_T, fg);	/* d */
	if (m & 0x10) rect(fb, x, y + hh, SEG_T, hh, fg);					/* e */
	if (m & 0x20) rect(fb, x, y + SEG_T, SEG_T, hh - SEG_T, fg);				/* f */
	if (m & 0x40) rect(fb, x + SEG_T, y + hh - SEG_T / 2, DIG_W - 2 * SEG_T, SEG_T, fg);	/* g */
}

/*
 * Draw one line: `pips` coloured squares on the left (line number), then
 * `nd` hex digits of `v`, most significant first.
 */
void probe_line(volatile uint32_t *fb, int line, uint64_t v, int nd, uint32_t pip)
{
	int i, x;
	int y = 90 + line * (DIG_H + 90);

	if (y + DIG_H > PH)
		return;
	for (i = 0; i < line + 1 && i < 6; i++)
		rect(fb, 12, y + i * 26, 40, 20, pip);

	x = 80;
	for (i = 0; i < nd; i++) {
		int shift = (nd - 1 - i) * 4;
		digit(fb, x + i * (DIG_W + DIG_GAP), y, (int)((v >> shift) & 0xf), 0xffffffffU);
	}
}

void probe_fill(volatile uint32_t *fb, uint32_t c)
{
	rect(fb, 0, 0, PW, PH, c);
}
