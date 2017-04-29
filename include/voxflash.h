	//Color arithemtic functions (used by voxlap itself and voxed)
// This file has been modified from Ken Silverman's original release
//(Maybe it actually hasn't, but just in case)

#include "voxlap5.h"

EXTERN_VOXLAP int64_t flashbrival;

#ifdef __WATCOMC__

void mmxcoloradd (int *);
#pragma aux mmxcoloradd =\
	".686"\
	"movd mm0, [eax]"\
	"paddusb mm0, flashbrival"\
	"movd [eax], mm0"\
	parm nomemory [eax]\
	modify exact \
	value

void mmxcolorsub (int *);
#pragma aux mmxcolorsub =\
	".686"\
	"movd mm0, [eax]"\
	"psubusb mm0, flashbrival"\
	"movd [eax], mm0"\
	parm nomemory [eax]\
	modify exact \
	value

#else

static inline void mmxcoloradd (int *a)
{
	((uint8_t *)a)[0] += ((uint8_t *)(&flashbrival))[0];
	((uint8_t *)a)[1] += ((uint8_t *)(&flashbrival))[1];
	((uint8_t *)a)[2] += ((uint8_t *)(&flashbrival))[2];
	((uint8_t *)a)[3] += ((uint8_t *)(&flashbrival))[3];
}

static inline void mmxcolorsub (int *a)
{
	((uint8_t *)a)[0] -= ((uint8_t *)((unsigned int)flashbrival))[0];
	((uint8_t *)a)[1] -= ((uint8_t *)((unsigned int)flashbrival))[1];
	((uint8_t *)a)[2] -= ((uint8_t *)((unsigned int)flashbrival))[2];
	((uint8_t *)a)[3] -= ((uint8_t *)((unsigned int)flashbrival))[3];
}

#endif
