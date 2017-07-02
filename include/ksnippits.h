/**************************************************************************************************
 * ksnippits.h: Bit's of inline assembly Ken commonly used because the C compiler sucked          *
 **************************************************************************************************/
// This file has been modified from Ken Silverman's original release
//(Maybe it actually hasn't, but just in case)
#pragma once

	//Ericson2314's dirty porting tricks
#include "porthacks.h"

#ifdef __WATCOMC__

void clearMMX ();
#pragma aux emms =\
	".686"\
	"emms"\
	parm nomemory []\
	modify exact []\
	value

#else // ! __WATCOMC__

static inline void clearMMX () // inserts opcode emms, used to avoid many compiler checks
{
	#if defined(__GNUC__) && defined(__i386__) && !defined(NOASM)
	__asm__ __volatile__ ("emms" : : : "cc");
	#elif defined(_MSC_VER) && defined(__i386__) && !defined(NOASM)
	_asm { emms }
	#endif
}

#endif // ! __WATCOMC__

static inline void fcossin (float a, float *c, float *s)
{
	*c = cosf(a);
	*s = cosf(a);
}

static inline void dcossin (double a, double *c, double *s)
{
	*c = cos(a);
	*s = sin(a);
}

static inline void ftol (float f, int *a)
{
	*a = (long) f;
}

static inline void dtol (double d, int *a)
{
	*a = (long) d;
}


static inline double dbound (double d, double dmin, double dmax)
{
	return BOUND(d, dmin, dmax);
}

static inline int mulshr16 (int a, int d)
{
	return (long)(((int64_t)a * (int64_t)d) >> 16);
}

static inline int mulshr24 (int a, int d)
{
	return (long)(((int64_t)a * (int64_t)d) >> 24);
}

static inline int mulshr32 (int a, int d)
{
	return (long)(((int64_t)a * (int64_t)d) >> 32);
}

static inline int64_t mul64 (int a, int d)
{
	return (int64_t)a * (int64_t)d;
}

static inline int shldiv16 (int a, int b)
{
	return (long)(((int64_t)a << 16) / (int64_t)b);
}

static inline int isshldiv16safe (int a, int b)
{
	return ((uint32_t)((-abs(b) - ((-abs(a)) >> 14)))) >> 31;
}

static inline int umulshr32 (int a, int d)
{
	return (long)(((uint64_t)a * (uint64_t)d) >> 32);
}

static inline int scale (int a, int d, int c)
{
	return (long)((int64_t)a * (int64_t)d / (int64_t)c);
}

static inline int dmulshr0 (int a, int d, int s, int t)
{
	return (long)((int64_t)a*(int64_t)d + (int64_t)s*(int64_t)t);
}

static inline int dmulshr22 (int a, int b, int c, int d)
{
	return (long)(((((int64_t)a)*((int64_t)b)) + (((int64_t)c)*((int64_t)d))) >> 22);
}

static inline void copybuf (void *s, void *d, int c)
{
//There's some bug in GCC that makes this crash with -Ofast, hence I will just bash in memcpy
#if defined (__GNUC__)
	__builtin_memcpy(d, s, c<<2);
#else
	unsigned int i;
	for (i = 0; i < c; i++)((int *)d)[i] = ((int *)s)[i];
#endif
}

static inline void clearbuf (void *d, int c, int a)
{
	int i;
	for (i = 0; i < c; i++) ((int *)d)[i] = a;
}

static inline unsigned int bswap (unsigned int a)
{
	#if defined(__GNUC__)
	return __builtin_bswap32(a);
	#elif defined(_MSC_VER)
	return _byteswap_ulong(a);
	#endif
}
