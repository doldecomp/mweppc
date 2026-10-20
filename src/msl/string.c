#include "ansi_prefix.Win32.h"
#ifdef __MWERKS__
#include <string.c>
#else
#define _STRING_WIN32_H
#include <string.c>

#define MSL_NO_BUILTIN __attribute__((no_builtin))

MSL_NO_BUILTIN size_t strlen(const char * s)
{
	const char * p = s;

	while (*p)
		p++;
	return p - s;
}

MSL_NO_BUILTIN char * strcpy(char * dest, const char * src)
{
	char * q = dest;

	while ((*q++ = *src++) != 0)
		;
	return dest;
}

MSL_NO_BUILTIN char * strncpy(char * dest, const char * src, size_t max)
{
	char * q = dest;

	for (; max; max--)
		if ((*q++ = *src++) == 0)
			break;
	while (max > 1)
	{
		*q++ = 0;
		max--;
	}
	return dest;
}

MSL_NO_BUILTIN char * strcat(char * dest, const char * src)
{
	strcpy(dest + strlen(dest), src);
	return dest;
}

MSL_NO_BUILTIN char * strncat(char * dest, const char * src, size_t max)
{
	char * q;

	if (!max)
		return dest;
	q = dest + strlen(dest);
	for (; max; max--)
		if ((*q++ = *src++) == 0)
			return dest;
	*q = 0;
	return dest;
}

MSL_NO_BUILTIN int strcmp(const char * s1, const char * s2)
{
	const unsigned char * p1 = (const unsigned char *) s1;
	const unsigned char * p2 = (const unsigned char *) s2;

	for (; *p1 == *p2; p1++, p2++)
		if (!*p1)
			return 0;
	return *p1 - *p2;
}

MSL_NO_BUILTIN int strncmp(const char * s1, const char * s2, size_t n)
{
	const unsigned char * p1 = (const unsigned char *) s1;
	const unsigned char * p2 = (const unsigned char *) s2;

	for (; n; n--, p1++, p2++)
		if (*p1 != *p2 || !*p1)
			return *p1 - *p2;
	return 0;
}

MSL_NO_BUILTIN char * strchr(const char * s, int c)
{
	for (;; s++)
	{
		if (*s == (char) c)
			return (char *) s;
		if (!*s)
			return NULL;
	}
}
#endif
