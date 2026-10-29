/*
 * compat.c - the few libc functions clang emits calls to in a freestanding build.
 *
 * The upstream build uses aarch64-linux-gnu-gcc, which folds the partition-name
 * comparisons in ufs.c into inline sequences. clang (llvm) instead emits a real
 * call to strcmp() at -O2, and the shim links with -nostdlib, so the symbol was
 * undefined:
 *
 *   ld.lld: error: undefined symbol: strcmp
 *   >>> referenced by ufs.c:189  out/ufs.o:(list_partitions)
 *
 * Compiled with -fno-builtin so the optimiser cannot turn these bodies back into
 * calls to themselves.
 */
#include <stddef.h>

int strcmp(const char *a, const char *b)
{
	while (*a && *a == *b) {
		a++;
		b++;
	}
	return (unsigned char)*a - (unsigned char)*b;
}

int strncmp(const char *a, const char *b, size_t n)
{
	while (n && *a && *a == *b) {
		a++;
		b++;
		n--;
	}
	if (!n)
		return 0;
	return (unsigned char)*a - (unsigned char)*b;
}

size_t strlen(const char *s)
{
	size_t n = 0;

	while (s[n])
		n++;
	return n;
}

void *memset(void *d, int c, size_t n)
{
	unsigned char *p = d;

	while (n--)
		*p++ = (unsigned char)c;
	return d;
}

void *memcpy(void *d, const void *s, size_t n)
{
	unsigned char *dp = d;
	const unsigned char *sp = s;

	while (n--)
		*dp++ = *sp++;
	return d;
}
