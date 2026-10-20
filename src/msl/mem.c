#include "ansi_prefix.Win32.h"
#ifdef __MWERKS__
#include <mem.c>
#else
/* clang: MSL's x86 memory functions (lib/msl/MSL_X86/mem.x86.h, MWCC inline assembler) in C, with
 * the same results: memcpy copies forwards, aligning the destination and then moving longs as
 * "rep movsd" does (which a caller that copies between overlapping buffers sees), and memcmp
 * returns the difference of the first bytes that differ. MSL's portable versions (mem.c without
 * __INTEL__) take the big-endian mem_funcs.c. Each is no_builtin, so that clang does not make
 * its loop a call to itself. */
#include <string.h>

#define MSL_NO_BUILTIN __attribute__((no_builtin))

typedef unsigned long __attribute__((may_alias, aligned(1))) mem_long;

MSL_NO_BUILTIN void * memmove(void * dest, const void * src, size_t n)
{
	unsigned char *		q = (unsigned char *) dest;
	const unsigned char *	p = (const unsigned char *) src;

	if (q <= p || q >= p + n)
	{
		while (n--)
			*q++ = *p++;
	}
	else
	{
		q += n;
		p += n;
		while (n--)
			*--q = *--p;
	}
	return dest;
}

MSL_NO_BUILTIN void * memcpy(void * dest, const void * src, size_t n)
{
	unsigned char *		q = (unsigned char *) dest;
	const unsigned char *	p = (const unsigned char *) src;
	size_t				longs;

	if (n >= 16)
	{
		while ((unsigned long) q & 3)
		{
			*q++ = *p++;
			n--;
		}
		for (longs = n >> 2; longs; longs--)
		{
			*(mem_long *) q = *(const mem_long *) p;
			q += 4;
			p += 4;
		}
		n &= 3;
	}
	while (n--)
		*q++ = *p++;
	return dest;
}

MSL_NO_BUILTIN void * memset(void * dest, int c, size_t n)
{
	unsigned char * q = (unsigned char *) dest;

	while (n--)
		*q++ = (unsigned char) c;
	return dest;
}

MSL_NO_BUILTIN void * memchr(const void * src, int c, size_t n)
{
	const unsigned char * p = (const unsigned char *) src;

	for (; n; n--, p++)
		if (*p == (unsigned char) c)
			return (void *) p;
	return NULL;
}

MSL_NO_BUILTIN void * __memrchr(const void * src, int c, size_t n)
{
	const unsigned char * p = (const unsigned char *) src + n;

	while (n--)
		if (*--p == (unsigned char) c)
			return (void *) p;
	return NULL;
}

MSL_NO_BUILTIN int memcmp(const void * s1, const void * s2, size_t n)
{
	const unsigned char * p1 = (const unsigned char *) s1;
	const unsigned char * p2 = (const unsigned char *) s2;

	for (; n; n--, p1++, p2++)
		if (*p1 != *p2)
			return *p1 - *p2;
	return 0;
}
#endif
