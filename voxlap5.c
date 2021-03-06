﻿/**************************************************************************************************
 * VOXLAP engine                                                                                  *
 * by Ken Silverman (http://advsys.net/ken)                                                       *
 ***********************************************************************************************/


/***********************************************************************************************
Edited by LeCom
***********************************************************************************************/

	//This file has been modified from Ken Silverman's original release

	//C Standard Library includes
#include <math.h>
#include <stdio.h>
#include <stdlib.h> //was below system-specific includes before

	//Ericson2314's dirty porting tricks
#include "include/porthacks.h"

	//Ken's short, general-purpose to-be-inlined functions mainly consisting of inline assembly are now here
#include "include/ksnippits.h"

	//SYSMAIN Preprocessor stuff
//#define SYSMAIN_C //if sysmain is compiled as C
#include "include/sysmain.h"

	//Basic System Specific Stuff
#ifndef _WIN32 //Windows (hypothetically 6 64-bit too)
	#include <stdarg.h> //Moved from #if _DOS, included via windows.h for Windows
	#include <string.h> //Moved from #if _DOS, included via windows.h for Windows
	#ifdef _DOS //MS-DOS
		#include <conio.h>
		#include <dos.h>
		#ifndef MAX_PATH
			#define MAX_PATH 260
		#endif
	#else //POSIX
		#ifndef MAX_PATH
			#define MAX_PATH PATH_MAX //is a bad variable, should not be used for filename related things
		#endif
	#endif
#endif

/*Misc "fixes" compared to the old Voxlap port:
	Any optimization flags won't make the binary crash anymore
	loadvxl now loads the "old" AoS .vxl format instead of some weird "new" one, which starts with a header
	This can be compiled to a DLL and used as such
	100% assembler-independent
*/

/*To do:
	learn ASM and add inline assembler some day or add SIMD intrinsics
*/

/*Additional targets: Make all commands start with Vox_, because Voxlap uses some common names for its functions*/

/*I really recommend compiling this with optimization flags enabled.
Many things are implemented in a way that is simple for the compiler to optimize,
however, that code is really slow if unoptimized. Using SIMD instruction compiler flags
won't improve the performance a lot*/

/*____________________________________________________________________________
LeCom's STUFF THAT IS NOT IN THE ORIGINAL VOXLAP											*/
#define __PER_VOXEL_SHADING__ 0
#define __PER_PIXEL_SHADING__ 0

#define __PREDICT_DMULRUNS__ 0

#define __USE_MMX__ 0

#define __VOXEL_BRIGHTNESS__ 275
#define __LSHADE_FACTOR__ 0

/*I had to use a macro instead of making it decide dynamically, because of bottlenecks in gline.
I also won't add several versions of gline since I would have to update all versions after a change etc.*/
#define __DRAW_FOG__ 1

/*This sets how big one kv6 voxel is on the screen*/
/*Better don't change this*/
#define __PIXELS_PER_KVVXL__ 8

/*A hack that fills sky data using 64 bit transfers, since sky data is all the same*/
#define __FAST_SKY_FILL__ 0

/*When digging buried blocks out, Voxlap changes their color a bit by default.
This disables it*/
#define __VOXEL_COLOR_NOISE__ 0

#define __KVDARKEN__(c,d)( (((((c>>16)&255)*d)>>8)<<16) | (((((c>>8)&255)*d)>>8)<<8) | (((c&255)*d)>>8) )

/*mlight=minimum brightness, fshade=maximum shade-mshade, should not be higher than 255 together*/
#define __KV6_MLIGHT__ 92
#define __KV6_FSHADE__ (255-__KV6_MLIGHT__)
/*The higher this value, the more the whole sprite is shaded and the more
light is drawn on the few places that are bright*/
#define __KV6_SHADE_FACTOR__ 1.5
/*Same for experimental fog shading*/
#define __KV6_FOG_FACTOR__ 0.0

//#define __FORCE_INLINE__ __attribute__((always_inline))
#define __FORCE_INLINE__ 

/*Includes the height in the Z buffer*/
#define __USE_ACCURATE_ZBUFFER__ 1
#define __MIN_VOXEL_DIST__ 0.0

/*In case it should compile without registers*/
#define __REGISTER register

#define __ASM_DMULRETHIGH__ 0

/*I don't want to use the setsideshades values.
Right now, CubeShadeRight, Front and Back are ignored and CubeShadeLeft is used
for all these faces of the cube*/
unsigned char HorizontalCubeShades[4]={0, 0, 0, 0};
unsigned char CubeShadeBottom=0, CubeShadeTop=0;

#if 0
#define __GLINE_PROCESS_COLOR__(col, d_int) \
	{ \
		unsigned int col_r=((col&0x000000ff)*d_int)>>8; \
		unsigned int col_g=((col&0x0000ff00)*d_int)>>16; \
		unsigned int col_b=(((col&0x00ff0000)>>8)*d_int)>>16; \
		col_r|=(-((col_r&0x0000ff00)!=0) & 0x000000ff); \
		col_g|=(-((col_g&0x0000ff00)!=0) & 0x000000ff); \
		col_b|=(-((col_b&0x0000ff00)!=0) & 0x000000ff); \
		unsigned int col_comp1=(((col_r | (col_b<<16))&0x00ff00ff)*fog_alpha+fog_ccomp1*(255-fog_alpha))>>8; \
		unsigned int col_comp2=(((col_g<<8)&0x0000ff00)*fog_alpha+fog_ccomp2*(255-fog_alpha))>>8; \
		col=0xff000000 | (col_comp1&0x00ff00ff) | (col_comp2&0x0000ff00); \
	}
#else
#define __GLINE_PROCESS_COLOR__(col, d_int) \
	d_int*=fog_alpha; \
	{ \
		unsigned int col_r=((col&0x000000ff)*d_int+rfog)>>16; \
		unsigned int col_g=(((col&0x0000ff00)>>8)*d_int+gfog)>>16; \
		unsigned int col_b=(((col&0x00ff0000)>>16)*d_int+bfog)>>16; \
		col_r|=(-((col_r&0x0000ff00)!=0) & 0x000000ff); \
		col_g|=(-((col_g&0x0000ff00)!=0) & 0x000000ff); \
		col_b|=(-((col_b&0x0000ff00)!=0) & 0x000000ff); \
		col=0xff000000 | (col_b<<16) | (col_g<<8) | col_r; \
	}
#endif

/*____________________________________________________________________________
																			*/
																			
#if (__USE_MMX__!=0)
#include <emmintrin.h>
#include <mmintrin.h>
#include <xmmintrin.h>
#endif

#if (__ASM_DMULRETHIGH__==0)
#define __BDMLRS(b,c,a,d) \
	((int_fast64_t)(b)*(int_fast64_t)(c) < (int_fast64_t)(a)*(int_fast64_t)(d))

#define __BDMLREM(b,c,a,d) \
	((int_fast64_t)(b)*(int_fast64_t)(c) >= (int_fast64_t)(a)*(int_fast64_t)(d))
#else
static __inline int dmulrethigh (int b, int c, int a, int d)
{
	int ret;
	/*asm("mov eax, a"
		"imul d"
		"mov ecx, eax"
		"push edx"
		"mov eax, b"
		"imul c"
		"sub eax, ecx"
		"pop ecx"
		"sbb edx, ecx"
		"mov eax, edx");*/
	//ecx=a*d-b*c
	//
	//ret=0 a=1 b=2 c=3 d=4
	__asm__ volatile ("mov %1, %%eax;"
		"imull %4;"
		"mov %%eax, %%ecx;"
		"push %%edx;"
		"mov %2, %%eax;"
		"imull %3;"
		"sub %%ecx, %%eax;"
		"pop %%ecx;"
		"sbb %%ecx, %%edx;"
		"mov %%edx, %0;" : "=m"(ret) :"r"(a), "r"(b), "m"(c), "m"(d) : "%eax", "%ecx", "%edx");
	return ret;
}
#define __BDMLRS(b,c,a,d) (dmulrethigh(b,c,a,d)<0)
#define __BDMLREM(b,c,a,d) (dmulrethigh(b,c,a,d)>=0)
#endif

	//Voxlap Preprocessor stuff
#define VOXLAP5
				//We never want to define C bindings if this is compiled as C++.
#undef VOXLAP_C	//Putting this here just in case.
#include "include/voxlap5.h"

	//mmxcolor* now defined here

	//KPlib Preprocessor stuff
//#define KPLIB_C  //if kplib is compiled as C
//#include "include/kplib.h"

#define USEZBUFFER 1                 //Should a Z-Buffer be Used?
#define PREC (256*4096)
#define PREC_DIV(x) ((x)>>20)
#define CMPPREC (256*4096)
#define FPREC (256*4096)
#define USEV5ASM 0
#define SCISDIST 1.0
#define GOLDRAT 0.3819660112501052 //Golden Ratio: 1 - 1/((sqrt(5)+1)/2)
#define ESTNORMRAD 2               //Specially optimized for 2: DON'T CHANGE unless testing!

EXTERN_SYSMAIN void breath ();
EXTERN_SYSMAIN void evilquit (const char *);

#define VOXSIZ VSID*VSID*128
#ifdef __cplusplus
extern "C" {
#endif
char *sptr[(VSID*VSID*4)/3]={NULL};
#ifdef __cplusplus
}
#endif
static int *vbuf = 0, *vbit = 0, vbiti;
	//WARNING: loaddta uses last 2MB of vbuf; vbuf:[VOXSIZ>>2], vbit:[VOXSIZ>>7]
	//WARNING: loadpng uses last 4MB of vbuf; vbuf:[VOXSIZ>>2], vbit:[VOXSIZ>>7]

//                     ÚÄÄÄÄÄÄÄÄÂÄÄÄÄÄÄÄÄÂÄÄÄÄÄÄÄÄÂÄÄÄÄÄÄÄÄ¿
//        vbuf format: ³   0:   ³   1:   ³   2:   ³   3:   ³
//ÚÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÅÄÄÄÄÄÄÄÄÅÄÄÄÄÄÄÄÄÅÄÄÄÄÄÄÄÄÅÄÄÄÄÄÄÄÄ´
//³      First header: ³ nextptr³   z1   ³   z1c  ³  dummy ³
//³           Color 1: ³    b   ³    g   ³    r   ³ intens ³
//³           Color 2: ³    b   ³    g   ³    r   ³ intens ³
//³             ...    ³    b   ³    g   ³    r   ³ intens ³
//³           Color n: ³    b   ³    g   ³    r   ³ intens ³
//³ Additional header: ³ nextptr³   z1   ³   z1c  ³   z0   ³
//ÀÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÁÄÄÄÄÄÄÄÄÁÄÄÄÄÄÄÄÄÁÄÄÄÄÄÄÄÄÁÄÄÄÄÄÄÄÄÙ
//  nextptr: add this # <<2 to index to get to next header (0 if none)
//       z1: z floor (top of floor color list)
//      z1c: z bottom of floor color list MINUS 1! - needed to calculate
//             slab size with slng() and used as a separator for fcol/ccol
//       z0: z ceiling (bottom of ceiling color list)

	//Memory management variables:
#define MAXCSIZ 1028
char tbuf[MAXCSIZ]={0};
size_t tbuf2[MAXZDIM*3]={0};
int templongbuf[MAXZDIM]={0};

static char nullst = 0; //nullst always NULL string

#define SETSPHMAXRAD 256
static double logint[SETSPHMAXRAD];
static float tempfloatbuf[SETSPHMAXRAD];
static int factr[SETSPHMAXRAD][2];

#pragma pack(push,1)
	//Rendering variables:
#if (USEZBUFFER == 0)
typedef struct { int col; } castdat;
#else
typedef struct {
	unsigned int col;
	unsigned int dist;
} castdat;
#endif
typedef struct { castdat *i0, *i1; int z0, z1, cx0, cy0, cx1, cy1; } cftype;
#pragma pack(pop)

#undef EXTERN_C
#define EXTERN_C
	//Screen related variables:
static int xres_voxlap, yres_voxlap, bytesperline, xres4_voxlap;
static unsigned char *frameplace;
int ylookup[MAXYDIM+1]={0};

static lpoint3d glipos;
__CLANG_GLOBALVAR__ point3d gipos, gistr, gihei, gifor;
static point3d gixs, giys, gizs, giadd;
static float gihx, gihy, gihz, gposxfrac[2], gposyfrac[2], grd;
static int gposz, giforzsgn, gstartz0, gstartz1, gixyi[2];
static char *gstartv;

static float optistrx, optistry, optiheix, optiheiy, optiaddx, optiaddy;

int backtag=0, backedup = -1, bacx0=0, bacy0=0, bacx1=0, bacy1=0;
char *bacsptr[262144]={NULL};

	//Flash variables
#define LOGFLASHVANG 9
static lpoint2d gfc[(1<<LOGFLASHVANG)*8];
static int gfclookup[8] = {4,7,2,5,0,3,6,1}, flashcnt = 0;
int64_t flashbrival=0;

	//Norm flash variables
#define GSIZ 512  //NOTE: GSIZ should be 1<<x, and must be <= 65536
static int bbuf[GSIZ][GSIZ>>5], p2c[32], p2m[32];      //bbuf: 2.0K
static uspoint2d ffx[((GSIZ>>1)+2)*(GSIZ>>1)], *ffxptr; // ffx:16.5K
static int xbsox = -17, xbsoy, xbsof;
static int64_t xbsbuf[25*5+1]; //need few bits before&after for protection

	//Look tables for expandbitstack256:
static int xbsceil[32], xbsflor[32]; //disabling mangling for inline asm

	//float detection & falling code variables...
	//WARNING: VLSTSIZ,FSTKSIZ,FLCHKSIZ can all have bounds errors! :(
#define VLSTSIZ 65536 //Theoretically should be at least: VOXSIZ\8
#define LOGHASHEAD 12
#define FSTKSIZ 8192
typedef struct { size_t v, b; } vlstyp;
static vlstyp vlst[VLSTSIZ];
static int hhead[1<<LOGHASHEAD], vlstcnt = 0x7fffffff;
typedef struct{
	long x, y;
	size_t z;
} fstk_entry_t;
static fstk_entry_t fstk[FSTKSIZ]; //Note .z is actually used as a pointer, not z!
#define FLCHKSIZ 4096
static lpoint3d flchk[FLCHKSIZ]; int flchkcnt = 0;

	//Opticast global variables:
	//radar: 320x200 requires  419560*2 bytes (area * 6.56*2)
	//radar: 400x300 requires  751836*2 bytes (area * 6.27*2)
	//radar: 640x480 requires 1917568*2 bytes (area * 6.24*2)
#define SCPITCH 256
int *radar = 0, *radarmem = 0;
#if (USEZBUFFER == 1)
static int *zbuffermem = 0, zbuffersiz = 0;
#endif
#define CMPRECIPSIZ MAXXDIM+32
static float cmprecip[CMPRECIPSIZ];
__CLANG_GLOBALVAR__ point3d gcorn[4];
__CLANG_GLOBALVAR__ point3d ginor[4]={0}; //Should be static, but... necessary for stupid pingball hack :/

void mat0(point3d *, point3d *, point3d *, point3d *, point3d *, point3d *, point3d *, point3d *, point3d *, point3d *, point3d *, point3d *);
void mat1(point3d *, point3d *, point3d *, point3d *, point3d *, point3d *, point3d *, point3d *, point3d *, point3d *, point3d *, point3d *);
void mat2(point3d *, point3d *, point3d *, point3d *, point3d *, point3d *, point3d *, point3d *, point3d *, point3d *, point3d *, point3d *);

	//Parallaxing sky variables:
static int skybpl, skyysiz, skycurlng, skycurdir;
static size_t skypic=0, nskypic=0;
static float skylngmul;
static point2d *skylng = 0;

#ifdef __cplusplus
extern "C" {
#endif

	//Parallaxing sky variables (accessed by assembly code)
int skyoff = 0, skyxsiz=0, *skylat = 0;

int64_t gi=0, gcsub[8] =
{
	0xff00ff00ff00ff,0xff00ff00ff00ff,0xff00ff00ff00ff,0xff00ff00ff00ff,
	0xff00ff00ff00ff,0xff00ff00ff00ff,0xff00ff00ff00ff,0xff00ff00ff00ff
};
int gylookup[512+36]={0}, gmipnum = 0; //256+4+128+4+64+4+...
static int gxmip;
static char **gpixy;
static int gmaxscandist;
//int reax, rebx, recx, redx, resi, redi, rebp, resp, remm[16];

#if (defined(USEV5ASM) && (USEV5ASM != 0)) //if true
EXTERN_C void dep_protect_start();
EXTERN_C void dep_protect_end();
#endif

uint64_t __udivmoddi4(uint64_t num, uint64_t den, uint64_t *rem_p){
	uint64_t quot = 0, qbit = 1;
	if(den==0){
		return 1/((unsigned)den);
	}
	while((int64_t)den>=0){
		den<<=1;
		qbit<<=1;
	}
	while(qbit){
		if(den<=num){
			num-=den;
			quot+=qbit;
		}
		den>>=1;
		qbit>>=1;
	}
	if(rem_p)
		*rem_p=num;
	return quot;
}

int64_t __divdi3(int64_t num, int64_t den){
	int minus=0;
	int64_t v;
	if(num<0){
		num=-num;
		minus=1;
	}
	if(den<0){
		den=-den;
		minus^=1;
	}
	v=__udivmoddi4(num, den, NULL);
	if(minus)
		v=-v;
	return v;
}

void grouscanasm (int);
/*#if (USEZBUFFER == 1)*/
#if 1
int zbufoff=0;
#endif
#ifdef __cplusplus
}
#endif
#define gi0 (((int *)&gi)[0])
#define gi1 (((int *)&gi)[1])

struct vx5_interface *Vox_GetVX5(){return &vx5;}

#if (__ASM_DMULRETHIGH__==0)
static inline int dmulrethigh (int b, int c, int a, int d)
{
	return (int)(((int64_t)b*(int64_t)c - (int64_t)a*(int64_t)d) >> 32);
}
#endif

	//if (a < 0) return(0); else if (a > b) return(b); else return(a);
static inline int lbound0 (int a, int b) //b MUST be >= 0
{
	if ((unsigned int)a <= b) return(a);
	return((~(a>>31))&b);
}

	//if (a < b) return(b); else if (a > c) return(c); else return(a);
static inline int lbound (int a, int b, int c) //c MUST be >= b
{
	c -= b;
	if ((unsigned int)(a-b) <= c) return(a);
	return((((b-a)>>31)&c) + b);
}

static inline void mmxcoloradd (int *a)
{
	((uint8_t *)a)[0] += ((uint8_t *)(&flashbrival))[0];
	((uint8_t *)a)[1] += ((uint8_t *)(&flashbrival))[1];
	((uint8_t *)a)[2] += ((uint8_t *)(&flashbrival))[2];
	((uint8_t *)a)[3] += ((uint8_t *)(&flashbrival))[3];
}

#define LSINSIZ 8 //Must be >= 2!
static point2d usintab[(1<<LSINSIZ)+(1<<(LSINSIZ-2))];
static void ucossininit ()
{
	int i, j;
	double a, ai, s, si, m;

	j = 0; usintab[0].y = 0.0;
	i = (1<<LSINSIZ)-1;
	ai = PI*(-2)/((float)(1<<LSINSIZ)); a = ((float)(-i))*ai;
	ai *= .5; m = sin(ai)*2; s = sin(a); si = cos(a+ai)*m; m = -m*m;
	for(;i>=0;i--)
	{
		usintab[i].y = s; s += si; si += s*m; //MUCH faster than next line :)
		//usintab[i].y = sin(i*PI*2/((float)(1<<LSINSIZ)));
		usintab[i].x = (usintab[j].y-usintab[i].y)/((float)(1<<(32-LSINSIZ)));
		j = i;
	}
	for(i=(1<<(LSINSIZ-2))-1;i>=0;i--) usintab[i+(1<<LSINSIZ)] = usintab[i];
}

	//Calculates cos & sin of 32-bit unsigned int angle in ~15 clock cycles
	//  Accuracy is approximately +/-.0001
static inline void ucossin (unsigned int a, float *cosin)
{
	float f = ((float)(a&((1<<(32-LSINSIZ))-1))); a >>= (32-LSINSIZ);
	cosin[0] = usintab[a+(1<<(LSINSIZ-2))].x*f+usintab[a+(1<<(LSINSIZ-2))].y;
	cosin[1] = usintab[a                 ].x*f+usintab[a                 ].y;
}

static const uint32_t font4x6[] = //256 DOS chars, from Ken's Build SMALLFNT
{
	0x000000,0x6f9f60,0x69f960,0xaffe40,0x4efe40,0x6ff6f0,0x66f6f0,0x000000,
	0xeeaee0,0x000000,0x000000,0x000000,0x000000,0x000000,0x7755c0,0x96f690,
	0x8cec80,0x26e620,0x4e4e40,0xaaa0a0,0x7dd550,0x7ca6c0,0x000ee0,0x4e4ee0,
	0x4e4440,0x444e40,0x02f200,0x04f400,0x000000,0x000000,0x000000,0x000000,
	0x000000,0x444040,0xaa0000,0xafafa0,0x6c46c0,0xa248a0,0x4a4ce0,0x240000,
	0x488840,0x422240,0x0a4a00,0x04e400,0x000224,0x00e000,0x000040,0x224480,
	0xeaaae0,0x444440,0xe2e8e0,0xe2e2e0,0xaae220,0xe8e2e0,0xe8eae0,0xe22220,
	0xeaeae0,0xeae220,0x040400,0x040480,0x248420,0x0e0e00,0x842480,0xc24040,
	0xeaece0,0x4aeaa0,0xcacac0,0x688860,0xcaaac0,0xe8c8e0,0xe8c880,0xe8aae0,
	0xaaeaa0,0xe444e0,0xe22a60,0xaacaa0,0x8888e0,0xaeeaa0,0xaeeea0,0xeaaae0,
	0xeae880,0xeaae60,0xeacaa0,0xe8e2e0,0xe44440,0xaaaae0,0xaaa440,0xaaeea0,
	0xaa4aa0,0xaae440,0xe248e0,0xc888c0,0x844220,0x622260,0x4a0000,0x0000e0,
	0x420000,0x006a60,0x88eae0,0x00e8e0,0x22eae0,0x006e60,0x24e440,0x06a62c,
	0x88eaa0,0x040440,0x040448,0x88aca0,0x444440,0x08eee0,0x00caa0,0x00eae0,
	0x00eae8,0x00eae2,0x00e880,0x0064c0,0x04e440,0x00aa60,0x00aa40,0x00eee0,
	0x00a4a0,0x00aa6c,0x00c460,0x648460,0x440440,0xc424c0,0x6c0000,0x04ae00,
	0x68886c,0xa0aa60,0x606e60,0xe06a60,0xa06a60,0xc06a60,0x046a60,0x00e8e4,
	0xe06e60,0xa06e60,0xc06e60,0x0a0440,0x0e0440,0x0c0440,0xa4aea0,0x404ea0,
	0x60ece0,0x007a70,0x7afab0,0xe0eae0,0xa0eae0,0xc0eae0,0xe0aa60,0xc0aa60,
	0xa0aa6c,0xa0eae0,0xa0aae0,0x4e8e40,0x65c4f0,0xa4ee40,0xcafab0,0x64e4c0,
	0x606a60,0x060440,0x60eae0,0x60aa60,0xc0caa0,0xe0aea0,0x6a60e0,0xeae0e0,
	0x404860,0x007400,0x00c400,0x8a4e60,0x8a6e20,0x404440,0x05a500,0x0a5a00,
	0x282828,0x5a5a5a,0xd7d7d7,0x444444,0x44c444,0x44cc44,0x66e666,0x00e666,
	0x00cc44,0x66ee66,0x666666,0x00ee66,0x66ee00,0x66e000,0x44cc00,0x00c444,
	0x447000,0x44f000,0x00f444,0x447444,0x00f000,0x44f444,0x447744,0x667666,
	0x667700,0x007766,0x66ff00,0x00ff66,0x667766,0x00ff00,0x66ff66,0x44ff00,
	0x66f000,0x00ff44,0x00f666,0x667000,0x447700,0x007744,0x007666,0x66f666,
	0x44ff44,0x44c000,0x007444,0xffffff,0x000fff,0xcccccc,0x333333,0xfff000,
	0x00dad0,0xcacac8,0xea8880,0x00f660,0xe848e0,0x007a40,0x0aac80,0x05a220,
	0xe4a4e0,0x4aea40,0x6996f0,0x646ae0,0x06f600,0x16f680,0x68c860,0x4aaaa0,
	0xe0e0e0,0x4e40e0,0x4240e0,0x4840e0,0x4a8880,0x222a40,0x40e040,0x06ec00,
	0xeae000,0x0cc000,0x00c000,0x644c40,0xcaa000,0xc4e000,0x0eee00,0x000000,
};

/**
 * Draws 4x6 font on screen (very fast!)
 * @param x x of top-left corner
 * @param y y of top-left corner
 * @param fcol foreground color (32-bit RGB format)
 * @param bcol background color (32-bit RGB format) or -1 for transparent
 * @param fmt string - same syntax as printf
 */
 //ONLY WORKS FOR 32 BIT (see cast from frameplace pointer to int)
void VOXLAP_DLL_FUNC print4x6 (int xpos, int ypos, int fcol, int bcol, const char *fmt, ...)
{
	va_list arglist;
	char st[280], *c;
	int i, j;

	if (!fmt) return;
	va_start(arglist,fmt);
	vsprintf(st,fmt,arglist);
	va_end(arglist);
	size_t x=xpos;
	size_t y = ypos*bytesperline+(x<<2)+(size_t)frameplace;
	if (bcol < 0)
	{
		for(j=20;j>=0;y+=bytesperline,j-=4)
			for(c=st,x=y;*c;c++,x+=16)
			{
				i = (font4x6[*c]>>j);
				if (i&8) *(int *)(x   ) = fcol;
				if (i&4) *(int *)(x+ 4) = fcol;
				if (i&2) *(int *)(x+ 8) = fcol;
				if (i&1) *(int *)(x+12) = fcol;
				if ((*c) == 9) x += 32;
			}
		return;
	}
	fcol -= bcol;
	for(j=20;j>=0;y+=bytesperline,j-=4)
		for(c=st,x=y;*c;c++,x+=16)
		{
			i = (font4x6[*c]>>j);
			*(int *)(x   ) = (((i<<28)>>31)&fcol)+bcol;
			*(int *)(x+ 4) = (((i<<29)>>31)&fcol)+bcol;
			*(int *)(x+ 8) = (((i<<30)>>31)&fcol)+bcol;
			*(int *)(x+12) = (((i<<31)>>31)&fcol)+bcol;
			if ((*c) == 9) { for(i=16;i<48;i+=4) *(int *)(x+i) = bcol; x += 32; }
		}
}

	//NOTE: font is stored vertically first! (like .ART files)
static const uint64_t font6x8[] = //256 DOS chars, from: DOSAPP.FON (tab blank)
{
	0x3E00000000000000,0x6F6B3E003E455145,0x1C3E7C3E1C003E6B,0x3000183C7E3C1800,
	0x7E5C180030367F36,0x000018180000185C,0x0000FFFFE7E7FFFF,0xDBDBC3FF00000000,
	0x0E364A483000FFC3,0x6000062979290600,0x0A7E600004023F70,0x2A1C361C2A003F35,
	0x0800081C3E7F0000,0x7F361400007F3E1C,0x005F005F00001436,0x22007F017F090600,
	0x606060002259554D,0x14B6FFB614000060,0x100004067F060400,0x3E08080010307F30,
	0x08083E1C0800081C,0x0800404040407800,0x3F3C3000083E083E,0x030F3F0F0300303C,
	0x0000000000000000,0x0003070000065F06,0x247E247E24000307,0x630000126A2B2400,
	0x5649360063640813,0x0000030700005020,0x00000000413E0000,0x1C3E080000003E41,
	0x08083E080800083E,0x0800000060E00000,0x6060000008080808,0x0204081020000000,
	0x00003E4549513E00,0x4951620000407F42,0x3649494922004649,0x2F00107F12141800,
	0x494A3C0031494949,0x0305097101003049,0x0600364949493600,0x6C6C00001E294949,
	0x00006CEC00000000,0x2400004122140800,0x2241000024242424,0x0609590102000814,
	0x7E001E555D413E00,0x49497F007E111111,0x224141413E003649,0x7F003E4141417F00,
	0x09097F0041494949,0x7A4949413E000109,0x00007F0808087F00,0x4040300000417F41,
	0x412214087F003F40,0x7F00404040407F00,0x04027F007F020402,0x3E4141413E007F08,
	0x3E00060909097F00,0x09097F005E215141,0x3249494926006619,0x3F0001017F010100,
	0x40201F003F404040,0x3F403C403F001F20,0x0700631408146300,0x4549710007087008,
	0x0041417F00000043,0x0000201008040200,0x01020400007F4141,0x8080808080800402,
	0x2000000007030000,0x44447F0078545454,0x2844444438003844,0x38007F4444443800,
	0x097E080008545454,0x7CA4A4A418000009,0x0000007804047F00,0x8480400000407D00,
	0x004428107F00007D,0x7C0000407F000000,0x04047C0078041804,0x3844444438000078,
	0x380038444444FC00,0x44784400FC444444,0x2054545408000804,0x3C000024443E0400,
	0x40201C00007C2040,0x3C6030603C001C20,0x9C00006C10106C00,0x54546400003C60A0,
	0x0041413E0800004C,0x0000000077000000,0x02010200083E4141,0x3C2623263C000001,
	0x3D001221E1A11E00,0x54543800007D2040,0x7855555520000955,0x2000785554552000,
	0x5557200078545555,0x1422E2A21C007857,0x3800085555553800,0x5555380008555455,
	0x00417C0100000854,0x0000004279020000,0x2429700000407C01,0x782F252F78007029,
	0x3400455554547C00,0x7F097E0058547C54,0x0039454538004949,0x3900003944453800,
	0x21413C0000384445,0x007C20413D00007D,0x3D00003D60A19C00,0x40413C00003D4242,
	0x002466241800003D,0x29006249493E4800,0x16097F00292A7C2A,0x02097E8840001078,
	0x0000785555542000,0x4544380000417D00,0x007D21403C000039,0x7A0000710A097A00,
	0x5555080000792211,0x004E51514E005E55,0x3C0020404D483000,0x0404040404040404,
	0x506A4C0817001C04,0x0000782A34081700,0x0014080000307D30,0x0814000814001408,
	0x55AA114411441144,0xEEBBEEBB55AA55AA,0x0000FF000000EEBB,0x0A0A0000FF080808,
	0xFF00FF080000FF0A,0x0000F808F8080000,0xFB0A0000FE0A0A0A,0xFF00FF000000FF00,
	0x0000FE02FA0A0000,0x0F0800000F080B0A,0x0F0A0A0A00000F08,0x0000F80808080000,
	0x080808080F000000,0xF808080808080F08,0x0808FF0000000808,0x0808080808080808,
	0xFF0000000808FF08,0x0808FF00FF000A0A,0xFE000A0A0B080F00,0x0B080B0A0A0AFA02,
	0x0A0AFA02FA0A0A0A,0x0A0A0A0AFB00FF00,0xFB00FB0A0A0A0A0A,0x0A0A0B0A0A0A0A0A,
	0x0A0A08080F080F08,0xF808F8080A0AFA0A,0x08080F080F000808,0x00000A0A0F000000,
	0xF808F8000A0AFE00,0x0808FF00FF080808,0x08080A0AFB0A0A0A,0xF800000000000F08,
	0xFFFFFFFFFFFF0808,0xFFFFF0F0F0F0F0F0,0xFF000000000000FF,0x0F0F0F0F0F0FFFFF,
	0xFE00241824241800,0x01017F0000344A4A,0x027E027E02000003,0x1800006349556300,
	0x2020FC00041C2424,0x000478040800001C,0x3E00085577550800,0x02724C00003E4949,
	0x0030595522004C72,0x1800182418241800,0x2A2A1C0018247E24,0x003C02023C00002A,
	0x0000002A2A2A2A00,0x4A4A510000242E24,0x00514A4A44000044,0x20000402FC000000,
	0x2A08080000003F40,0x0012241224000808,0x0000000609090600,0x0008000000001818,
	0x02023E4030000000,0x0900000E010E0100,0x3C3C3C0000000A0D,0x000000000000003C,
};



/**
 * Draws 6x8 font on screen (very fast!)
 * @param x x of top-left corner
 * @param y y of top-left corner
 * @param fcol foreground color (32-bit RGB format)
 * @param bcol background color (32-bit RGB format) or -1 for transparent
 * @param fmt string - same syntax as printf
 */
 //ONLY WORKS FOR 32 BIT (see cast from frameplace pointer to int)
void VOXLAP_DLL_FUNC print6x8 (int xpos, int ypos, int fcol, int bcol, const char *fmt, ...)
{
	va_list arglist;
	char st[280], *c, *v;
	int i, j;

	if (!fmt) return;
	va_start(arglist,fmt);
	vsprintf(st,fmt,arglist);
	va_end(arglist);
	size_t x=xpos, y=ypos;

	y = y*bytesperline+(x<<2)+(size_t)frameplace;
	if (bcol < 0)
	{
		for(j=1;j<256;y+=bytesperline,j<<=1)
			for(c=st,x=y;*c;c++,x+=24)
			{
				v = (char *)(((size_t)font6x8) + ((int)c[0])*6);
				if (v[0]&j) *(int *)(x   ) = fcol;
				if (v[1]&j) *(int *)(x+ 4) = fcol;
				if (v[2]&j) *(int *)(x+ 8) = fcol;
				if (v[3]&j) *(int *)(x+12) = fcol;
				if (v[4]&j) *(int *)(x+16) = fcol;
				if (v[5]&j) *(int *)(x+20) = fcol;
				if ((*c) == 9) x += ((2*6)<<2);
			}
		return;
	}
	fcol -= bcol;
	for(j=1;j<256;y+=bytesperline,j<<=1)
		for(c=st,x=y;*c;c++,x+=24)
		{
			v = (char *)(((size_t)font6x8) + ((int)c[0])*6);
			*(int *)(x   ) = (((-(v[0]&j))>>31)&fcol)+bcol;
			*(int *)(x+ 4) = (((-(v[1]&j))>>31)&fcol)+bcol;
			*(int *)(x+ 8) = (((-(v[2]&j))>>31)&fcol)+bcol;
			*(int *)(x+12) = (((-(v[3]&j))>>31)&fcol)+bcol;
			*(int *)(x+16) = (((-(v[4]&j))>>31)&fcol)+bcol;
			*(int *)(x+20) = (((-(v[5]&j))>>31)&fcol)+bcol;
			if ((*c) == 9) { for(i=24;i<72;i+=4) *(int *)(x+i) = bcol; x += ((2*6)<<2); }
		}
}

static int gkrand = 0;
#if (__VOXEL_COLOR_NOISE__!=0)
VOXLAP_DLL_FUNC int colorjit (int i, int jitamount)
{
	gkrand = (gkrand*27584621)+1;
	return((gkrand&jitamount)^i);
}
#else
#define colorjit(i,jitamount) (i)
#endif

VOXLAP_DLL_FUNC int lightvox (int i)
{
	int r, g, b;

	b = ((unsigned int)i>>24);
	r = MIN((((i>>16)&255)*b)>>7,255);
	g = MIN((((i>>8 )&255)*b)>>7,255);
	b = MIN((((i    )&255)*b)>>7,255);
	return((r<<16)+(g<<8)+b);
}

	//Note: ebx = 512 is no change
	//If PENTIUM III:1.Replace punpcklwd&punpckldq with: pshufw mm1, mm1, 0
	//               2.Use pmulhuw, shift by 8 & mul by 256
	//  :(  Can't mix with floating point
//#pragma aux colormul =
//   "movd mm0, eax"
//   "pxor mm1, mm1"
//   "punpcklbw mm0, mm1"
//   "psllw mm0, 7"
//   "movd mm1, ebx"
//   "punpcklwd mm1, mm1"
//   "punpckldq mm1, mm1"
//   "pmulhw mm0, mm1"
//   "packsswb mm0, mm0"
//   "movd eax, mm0"
//   parm [eax][ebx]
//   modify exact [eax]
//   value [eax]

VOXLAP_DLL_FUNC int colormul (int i, int mulup8)
{
	int r, g, b;

	r = ((((i>>16)&255)*mulup8)>>8); if (r > 255) r = 255;
	g = ((((i>>8 )&255)*mulup8)>>8); if (g > 255) g = 255;
	b = ((((i    )&255)*mulup8)>>8); if (b > 255) b = 255;
	return((i&0xff000000)+(r<<16)+(g<<8)+b);
}

VOXLAP_DLL_FUNC int curcolfunc (lpoint3d *p) { return(vx5.curcol); }

VOXLAP_DLL_FUNC int floorcolfunc (lpoint3d *p)
{
	char *v;
	for(v=sptr[p->y*VSID+p->x];(p->z>v[2]) && (v[0]);v+=v[0]*4);
	return(*(int *)&v[4]);
}

VOXLAP_DLL_FUNC int jitcolfunc (lpoint3d *p) { return(colorjit(vx5.curcol,vx5.amount)); }

static int manycolukup[64] =
{
	  0,  1,  2,  5, 10, 15, 21, 29, 37, 47, 57, 67, 79, 90,103,115,
	127,140,152,165,176,188,198,208,218,226,234,240,245,250,253,254,
	255,254,253,250,245,240,234,226,218,208,198,188,176,165,152,140,
	128,115,103, 90, 79, 67, 57, 47, 37, 29, 21, 15, 10,  5,  2,  1
};
VOXLAP_DLL_FUNC int manycolfunc (lpoint3d *p)
{
	return((manycolukup[p->x&63]<<16)+(manycolukup[p->y&63]<<8)+manycolukup[p->z&63]+0x80000000);
}

VOXLAP_DLL_FUNC int sphcolfunc (lpoint3d *p)
{
	int i;
	ftol(sin((p->x+p->y+p->z-vx5.cen)*vx5.daf)*-96,&i);
	return(((i+128)<<24)|(vx5.curcol&0xffffff));
}

#define WOODXSIZ 46
#define WOODYSIZ 24
#define WOODZSIZ 24
static float wx[256], wy[256], wz[256], vx[256], vy[256], vz[256];
VOXLAP_DLL_FUNC int woodcolfunc (lpoint3d *p)
{
	float col, u, a, f, dx, dy, dz;
	int i, c, xof, yof, tx, ty, xoff;

	if (*(int *)&wx[0] == 0)
	{
		for(i=0;i<256;i++)
		{
			wx[i] = WOODXSIZ * ((float)rand()/32768.0f-.5f) * .5f;
			wy[i] = WOODXSIZ * ((float)rand()/32768.0f-.5f) * .5f;
			wz[i] = WOODXSIZ * ((float)rand()/32768.0f-.5f) * .5f;

				//UNIFORM spherical randomization (see spherand.c)
			dz = 1.0f-(float)rand()/32768.0f*.04f;
			a = (float)rand()/32768.0f*PI*2.0f; fcossin(a,&dx,&dy);
			f = sqrt(1.0f-dz*dz); dx *= f; dy *= f;
				//??z: rings,  ?z?: vertical,  z??: horizontal (nice)
			vx[i] = dz; vy[i] = fabs(dy); vz[i] = dx;
		}
	}

		//(tx&,ty&) = top-left corner of current panel
	ty = p->y - (p->y%WOODYSIZ);
	xoff = ((ty/WOODYSIZ)*(ty/WOODYSIZ)*51721 + (p->z/WOODZSIZ)*357) % WOODXSIZ;
	tx = ((p->x+xoff) - (p->x+xoff)%WOODXSIZ) - xoff;

	xof = p->x - (tx + (WOODXSIZ>>1));
	yof = p->y - (ty + (WOODYSIZ>>1));

	c = ((((tx*429 + 4695) ^ (ty*341 + 4355) ^ 13643) * 2797) & 255);
	dx = xof - wx[c];
	dy = yof - wy[c];
	dz = (p->z%WOODZSIZ) - wz[c];

		//u = distance to center of randomly oriented cylinder
	u = vx[c]*dx + vy[c]*dy + vz[c]*dz;
	u = sqrt(dx*dx + dy*dy + dz*dz - u*u);

		//ring randomness
	u += sin((float)xof*.12 + (float)yof*.15) * .5;
	u *= (sin(u)*.05 + 1);

		//Ring function: smooth saw-tooth wave
	col = sin(u*2)*24;
	col *= pow(1.f-vx[c],.3f);

		//Thin shaded borders
	if ((p->x-tx == 0) || (p->y-ty == 0)) col -= 5;
	if ((p->x-tx == WOODXSIZ-1) || (p->y-ty == WOODYSIZ-1)) col -= 3;

	//f = col+c*.12+72; i = ftolp3(&f);
	  ftol(col+c*.12f+72.0f,&i);

	return(colormul(vx5.curcol,i<<1));
}

int gxsizcache = 0, gysizcache = 0;
VOXLAP_DLL_FUNC int pngcolfunc (lpoint3d *p)
{
	int x, y, z, u, v;
	float fx, fy, fz, rx, ry, rz;

	if (!vx5.pic) return(vx5.curcol);
	switch(vx5.picmode)
	{
		case 0:
			x = p->x-vx5.pico.x; y = p->y-vx5.pico.y; z = p->z-vx5.pico.z;
			u = (((x&vx5.picu.x) + (y&vx5.picu.y) + (z&vx5.picu.z))^vx5.xoru);
			v = (((x&vx5.picv.x) + (y&vx5.picv.y) + (z&vx5.picv.z))^vx5.xorv);
			break;
		case 1: case 2:
			fx = (float)p->x-vx5.fpico.x;
			fy = (float)p->y-vx5.fpico.y;
			fz = (float)p->z-vx5.fpico.z;
			rx = vx5.fpicu.x*fx + vx5.fpicu.y*fy + vx5.fpicu.z*fz;
			ry = vx5.fpicv.x*fx + vx5.fpicv.y*fy + vx5.fpicv.z*fz;
			rz = vx5.fpicw.x*fx + vx5.fpicw.y*fy + vx5.fpicw.z*fz;
			ftol(atan2(ry,rx)*vx5.xoru/(PI*2),&u);
			if (vx5.picmode == 1) ftol(rz,&v);
			else ftol((atan2(rz,sqrt(rx*rx+ry*ry))/PI+.5)*vx5.ysiz,&v);
			break;
		default: //case 3:
			fx = (float)p->x-vx5.fpico.x;
			fy = (float)p->y-vx5.fpico.y;
			fz = (float)p->z-vx5.fpico.z;
			ftol(vx5.fpicu.x*fx + vx5.fpicu.y*fy + vx5.fpicu.z*fz,&u);
			ftol(vx5.fpicv.x*fx + vx5.fpicv.y*fy + vx5.fpicv.z*fz,&v);
			break;
	}
	if (((unsigned int)u-gxsizcache) >= ((unsigned int)vx5.xsiz))
	{
		if (u < 0) gxsizcache = u-(u+1)%vx5.xsiz-vx5.xsiz+1;
		else gxsizcache = u-(u%vx5.xsiz);
	}
	if (((unsigned int)v-gysizcache) >= ((unsigned int)vx5.ysiz))
	{
		if (v < 0) gysizcache = v-(v+1)%vx5.ysiz-vx5.ysiz+1;
		else gysizcache = v-(v%vx5.ysiz);
	}
	return((vx5.pic[(v-gysizcache)*(vx5.bpl>>2)+(u-gxsizcache)]&0xffffff)|0x80000000);
}

	//Special case for SETSEC & SETCEI bumpmapping (vx5.picmode == 3)
	//no safety checks, returns alpha as signed char in range: (-128 to 127)
VOXLAP_DLL_FUNC int hpngcolfunc (point3d *p)
{
	int u, v;
	float fx, fy, fz;

	fx = p->x-vx5.fpico.x;
	fy = p->y-vx5.fpico.y;
	fz = p->z-vx5.fpico.z;
	ftol(vx5.fpicu.x*fx + vx5.fpicu.y*fy + vx5.fpicu.z*fz,&u);
	ftol(vx5.fpicv.x*fx + vx5.fpicv.y*fy + vx5.fpicv.z*fz,&v);

	if (((unsigned int)u-gxsizcache) >= ((unsigned int)vx5.xsiz))
	{
		if (u < 0) gxsizcache = u-(u+1)%vx5.xsiz-vx5.xsiz+1;
		else gxsizcache = u-(u%vx5.xsiz);
	}
	if (((unsigned int)v-gysizcache) >= ((unsigned int)vx5.ysiz))
	{
		if (v < 0) gysizcache = v-(v+1)%vx5.ysiz-vx5.ysiz+1;
		else gysizcache = v-(v%vx5.ysiz);
	}
	return(vx5.pic[(v-gysizcache)*(vx5.bpl>>2)+(u-gxsizcache)]>>24);
}

static int slng (const char *s)
{
	const char *v;

	for(v=s;v[0];v+=v[0]*4);
	return((ptrdiff_t)v-(ptrdiff_t)s+(v[2]-v[1]+1)*4+4); //64 BIT DANGER: (used to be "return((size_t)v-(size_t)s+(v[2]-v[1]+1)*4+4);")
}

VOXLAP_DLL_FUNC void voxdealloc (const char *v)
{
	size_t i, j;
	i = (((ptrdiff_t)v-(ptrdiff_t)vbuf)>>2); j = (slng(v)>>2)+i; //64 BIT DANGER: used to be ("i = (((int)v-(int)vbuf)>>2); j = (slng(v)>>2)+i;")
#if 0
	while (i < j) { vbit[i>>5] &= ~(1<<i); i++; }
#else
	if (!((j^i)&~31))
		vbit[i>>5] &= ~(p2m[j&31]^p2m[i&31]);
	else
	{
		vbit[i>>5] &=   p2m[i&31];  i >>= 5;
		vbit[j>>5] &= (~p2m[j&31]); j >>= 5;
		for(j--;j>i;j--) vbit[j] = 0;
	}
#endif
}

	//Note: danum MUST be a multiple of 4!
VOXLAP_DLL_FUNC char *voxalloc (int danum)
{
	int i, badcnt, p0, p1, vend;

	badcnt = 0; danum >>= 2; vend = (VOXSIZ>>2)-danum;
	do
	{
		for(;vbiti<vend;vbiti+=danum)
		{
			if (vbit[vbiti>>5]&(1<<vbiti)) continue;
			for(p0=vbiti;(!(vbit[(p0-1)>>5]&(1<<(p0-1))));p0--);
			for(p1=p0+danum-1;p1>vbiti;p1--)
				if (vbit[p1>>5]&(1<<p1)) goto allocnothere;

			vbiti = p0+danum;
			for(i=p0;i<vbiti;i++) vbit[i>>5] |= (1<<i);
			return((char *)(&vbuf[p0]));
allocnothere:;
		}
		vbiti = 0; badcnt++;
	} while (badcnt < 2);
	evilquit("voxalloc: vbuf full"); return(0);
}

int isvoxelsolid (int x, int y, int z)
{
	char *v;

	if ((unsigned int)(x|y) >= VSID) return(0);
	v = sptr[y*VSID+x];
	while (1)
	{
		if (z < v[1]) return(0);
		if (!v[0]) return(1);
		v += v[0]*4;
		if (z < v[3]) return(1);
	}
}

	//Returns 1 if any voxels in range (x,y,z0) to (x,y,z1-1) are solid, else 0
int anyvoxelsolid (int x, int y, int z0, int z1)
{
	char *v;

		//         v1.....v3   v1.....v3    v1.......................>
		//                z0.........z1
	if ((unsigned int)(x|y) >= VSID) return(0);
	v = sptr[y*VSID+x];
	while (1)
	{
		if (z1 <= v[1]) return(0);
		if (!v[0]) return(1);
		v += v[0]*4;
		if (z0 < v[3]) return(1);
	}
}

	//Returns 1 if any voxels in range (x,y,z0) to (x,y,z1-1) are empty, else 0
int anyvoxelempty (int x, int y, int z0, int z1)
{
	char *v;

		//         v1.....v3   v1.....v3    v1.......................>
		//                z0.........z1
	if ((unsigned int)(x|y) >= VSID) return(1);
	v = sptr[y*VSID+x];
	while (1)
	{
		if (z0 < v[1]) return(1);
		if (!v[0]) return(0);
		v += v[0]*4;
		if (z1 <= v[3]) return(0);
	}
}

	//Returns z of first solid voxel under (x,y,z). Returns z if in solid.
int getfloorz (int x, int y, int z)
{
	__REGISTER char *v;

	if ((unsigned int)(x|y) >= VSID) return(z);
	v = sptr[y*VSID+x];
	while (1)
	{
		if (z <= v[1]) return(v[1]);
		if (!*v) break;
		v += (*v)<<2;
		if (z < v[3]) break;
	}
	return(z);
}

//Like getfloorz, but instead of looking for a solid voxel in an empty space, it looks for the first empty voxel in a solid space
int getnextfloorz(int x, int y, int z){
	__REGISTER char *v;

	if ((unsigned int)(x|y) >= VSID) return(z);
	v = sptr[y*VSID+x];
	while (1)
	{
		if(z<=v[1])return v[1];
		v += (v[0])<<2;
		if(!v[0])return z;
	}
	return(z);
}

	//Returns:
	//   0: air
	//   1: unexposed solid
	//else: address to color in vbuf (this can never be 0 or 1)
int* getcube (int x, int y, int z)
{
	__REGISTER int ceilnum;
	__REGISTER char *v;

	if ((unsigned int)(x|y) >= VSID) return NULL;
	v = sptr[y*VSID+x];	
	while (1)
	{
		if (z <= v[2])
		{
			if (z < v[1]) return((int*)0);
			return((int*)&v[((z-v[1])<<2)+4]);
		}
		ceilnum = v[2]-v[1]-*v+2;

		if (!*v) return((int*)1);
		v += *v<<2;

		if (z < v[3])
		{
			if (z-v[3] < ceilnum) return((int*)1);
			return((int*)&v[(z-v[3])<<2]);
		}
	}
}

	// Inputs: uind[MAXZDIM]: uncompressed 32-bit color buffer (-1: air)
	//         nind?[MAXZDIM]: neighbor buf:
	//            -2: unexposed solid
	//            -1: air
	//    0-16777215: exposed solid (color)
	//         px,py: parameters for setting unexposed voxel colors
	//Outputs: cbuf[MAXCSIZ]: compressed output buffer
	//Returns: n: length of compressed buffer (in bytes)
int compilestack (int *uind, int *n0, int *n1, int *n2, int *n3, char *cbuf, int px, int py)
{
	int oz, onext, n, cp2, cp1, cp0, rp1, rp0;
	lpoint3d p;

	p.x = px; p.y = py;

		//Do top slab (sky)
	oz = -1;
	p.z = -1; while (uind[p.z+1] == -1) p.z++;
	onext = 0;
	cbuf[1] = p.z+1;
	cbuf[2] = p.z+1;
	cbuf[3] = 0;  //Top z0 (filler, not used yet)
	n = 4;
	cp1 = 1; cp0 = 0;
	rp1 = -1; rp0 = -1;

	do
	{
			//cp2 = state at p.z-1 (0 = air, 1 = next2air, 2 = solid)
			//cp1 = state at p.z   (0 = air, 1 = next2air, 2 = solid)
			//cp0 = state at p.z+1 (0 = air, 1 = next2air, 2 = solid)
		cp2 = cp1; cp1 = cp0; cp0 = 2;
		if (p.z < MAXZDIM-2)  //Bottom must be solid!
		{
			if (uind[p.z+1] == -1)
				cp0 = 0;
			else if ((n0[p.z+1] == -1) || (n1[p.z+1] == -1) ||
						(n2[p.z+1] == -1) || (n3[p.z+1] == -1))
				cp0 = 1;
		}

			//Add slab
		if (cp1 != rp0)
		{
			if ((!cp1) && (rp0 > 0)) { oz = p.z; }
			else if ((rp0 < cp1) && (rp0 < rp1))
			{
				if (oz < 0) oz = p.z;
				cbuf[onext] = ((n-onext)>>2); onext = n;
				cbuf[n+1] = p.z;
				cbuf[n+2] = p.z-1;
				cbuf[n+3] = oz;
				n += 4; oz = -1;
			}
			rp1 = rp0; rp0 = cp1;
		}

			//Add color
		if ((cp1 == 1) || ((cp1 == 2) && ((!cp0) || (!cp2))))
		{
			if (cbuf[onext+2] == p.z-1) cbuf[onext+2] = p.z;
			if (uind[p.z] == -2) *(int *)&cbuf[n] = vx5.colfunc(&p);
								 else *(int *)&cbuf[n] = uind[p.z];
			n += 4;
		}

		p.z++;
	} while (p.z < MAXZDIM);
	cbuf[onext] = 0;
	return(n);
}

static inline void expandbit256 (void *s, void *d)
{
	int32_t eax;
	int32_t ecx = 32; //current bit index
	uint32_t edx = 0; //value of current 32-bit bits

	goto in2it;

	while (eax != 0)
	{
		s += eax * 4;
		eax = ((uint8_t*)s)[3];

		if ((eax -= ecx) >= 0) //xor mask [eax] for ceiling begins
		{
			do
			{
				*(uint32_t*)d = edx;
				d += 4;
				edx = -1;
				ecx += 32;
			}
			while ((eax -= 32) >= 0);
		}

		edx &= xbsceil[32+eax];
		//no jump

	in2it:
		eax = ((uint8_t*)s)[1];

		if ((eax -= ecx) > 0) //xor mask [eax] for floor begins
		{
			do
			{
				*(uint32_t*)d = edx;
				d += 4;
				edx = 0;
				ecx += 32;
			}
			while ((eax -= 32) >= 0);
		}

		edx |= xbsflor[32+eax];
		eax = *(uint8_t*)s;
	}

	if ((ecx -= 256) <= 0)
	{
		do
		{
			*(uint32_t*)d = edx;
			d += 4;
			edx = -1;
		}
		while ((ecx += 32) <= 0);
	}
}

void expandbitstack (int x, int y, int64_t *bind)
 {
	if ((x|y)&(~(VSID-1))) { clearbuf((void *)bind,8,0L); return; }
	expandbit256(sptr[y*VSID+x],(void *)bind);
}

void expandstack (int x, int y, int *uind)
{
	int z, topz;
	char *v, *v2;

	if ((x|y)&(~(VSID-1))) { clearbuf((void *)uind,MAXZDIM,0); return; }

		//Expands compiled voxel info to 32-bit uind[?]
	v = sptr[y*VSID+x]; z = 0;
	while (1)
	{
		while (z < v[1]) { uind[z] = -1; z++; }
		while (z <= v[2]) { uind[z] = (*(int *)&v[(z-v[1])*4+4]); z++; }
		v2 = &v[(v[2]-v[1]+1)*4+4];

		if (!v[0]) break;
		v += v[0]*4;

		topz = v[3]+(((ptrdiff_t)v2-(ptrdiff_t)v)>>2); //64 BIT DANGER: used to be ("topz = v[3]+(((int)v2-(int)v)>>2);")
		while (z < topz) { uind[z] = -2; z++; }
		while (z < v[3]) { uind[z] = *(int *)v2; z++; v2 += 4; }
	}
	while (z < MAXZDIM) { uind[z] = -2; z++; }
}

//static unsigned int single_run_count, multi_run_count;

__FORCE_INLINE__ unsigned int Predict_Dmulrhs(int_fast64_t x, int_fast64_t a, int_fast64_t b, int_fast64_t y, int_fast64_t f, int_fast64_t g){
	int_fast64_t div=-y*g+x*f, res;
	if(!div)
		return 0;
	res=x*a-y*b;
	if(res>=0)
		return 0;
	res/=div;
	if(res<0)
		return 0;
	++res;
	//single_run_count+=res<3;
	//multi_run_count+=res>=3;
	return res;
}

void gline(int leng, float x0, float y0, float x1, float y1, castdat *gscanptr)
{
	uint64_t q;
	float f, f1, f2, vd0, vd1, vz0, vx1, vy1, vz1;
	signed int j;
	cftype *c;
#if (!defined(USEV5ASM) || (USEV5ASM == 0)) /*if false*/
	signed int gx, dax, day;
	cftype *c2, *ce;
	char *v;
#endif
#if (defined(USEV5ASM) && (USEV5ASM != 0)) //if true
	EXTERN_C cftype cfasm[256]={0};
	EXTERN_C castdat skycast;
#else
	cftype cfasm[256]={0};
#endif
	/*Some checks for stuff that only happens when having certain maps*/
	x0=MIN(xres_voxlap,MAX(0,x0));
	x1=MIN(xres_voxlap,MAX(0,x1));
	y0=MIN(yres_voxlap,MAX(0,y0));
	y1=MIN(yres_voxlap,MAX(0,y1));

	vd0 = x0*gistr.x + y0*gihei.x + gcorn[0].x;
	vd1 = x0*gistr.y + y0*gihei.y + gcorn[0].y;
	vz0 = x0*gistr.z + y0*gihei.z + gcorn[0].z;
	vx1 = x1*gistr.x + y1*gihei.x + gcorn[0].x;
	vy1 = x1*gistr.y + y1*gihei.y + gcorn[0].y;
	vz1 = x1*gistr.z + y1*gihei.z + gcorn[0].z;

	f = sqrt(vx1*vx1 + vy1*vy1);
	f1 = f / vx1;
	f2 = f / vy1;
	if (fabs(vx1) > fabs(vy1)) vd0 = vd0*f1; else vd0 = vd1*f2;
	if (*(int *)&vd0 < 0) vd0 = 0; //vd0 MUST NOT be negative: bad for asm
	vd1 = f;
	signed int ray_dir[2]={fabs(f1)*PREC, fabs(f2)*PREC};
	//ftol(fabs(f1)*PREC,&gdz[0]);
	//ftol(fabs(f2)*PREC,&gdz[1]);
	int gxmax;
	ptrdiff_t gixy[2];

	//gixy[0] = (((*(signed int *)&vx1)>>31)<<3)+4; //=sgn(vx1)*4
	//gixy[1] = gixyi[(*(unsigned int *)&vy1)>>31]; //=sgn(vy1)*4*VSID
	gixy[0]=(vx1>=0.0 ? 1 : -1)*sizeof(uintptr_t);
	gixy[1]=(vy1>=0.0 ? 1 : -1)*VSID*sizeof(uintptr_t);
	signed int ray_pos[2];
	if (ray_dir[0] <= 0) { ray_pos[0]=gposxfrac[(*(unsigned int *)&vx1)>>31]*fabs(f1)*PREC;
		if (ray_pos[0] <= 0) ray_pos[0] = 0x7fffffff; ray_dir[0] = 0x7fffffff-ray_pos[0]; } //Hack for divide overflow
	else ray_pos[0]=gposxfrac[(*(unsigned int *)&vx1)>>31]*(float)ray_dir[0];
	if (ray_dir[1] <= 0) { ray_pos[1]=gposyfrac[(*(unsigned int *)&vy1)>>31]*fabs(f2)*PREC;
		if (ray_pos[1] <= 0) ray_pos[1] = 0x7fffffff; ray_dir[1] = 0x7fffffff-ray_pos[1]; } //Hack for divide overflow
	else ray_pos[1]=gposyfrac[(*(unsigned int *)&vy1)>>31]*(float)ray_dir[1];
	

	c = &cfasm[128];
	c->i0 = gscanptr; c->i1 = &gscanptr[leng];
	c->z0 = gstartz0; c->z1 = gstartz1;
	if (giforzsgn < 0)
	{
		ftol((vd1-vd0)*cmprecip[leng],&gi0); ftol(vd0*CMPPREC,&c->cx0);
		ftol((vz1-vz0)*cmprecip[leng],&gi1); ftol(vz0*CMPPREC,&c->cy0);
	}
	else
	{
		ftol((vd0-vd1)*cmprecip[leng],&gi0); ftol(vd1*CMPPREC,&c->cx0);
		ftol((vz0-vz1)*cmprecip[leng],&gi1); ftol(vz1*CMPPREC,&c->cy0);
	}
	c->cx1 = leng*gi0 + c->cx0;
	c->cy1 = leng*gi1 + c->cy0;

	gxmax = gmaxscandist;

		/*Hack for early-out case when looking up towards sky
		DOESN'T WORK WITH LOWER MIPS!
		(works without mip-mapping)
		*/
#if 1
	if (c->cy1 < 0){
		if (gposz > 0)
		{
			if (dmulrethigh(-gposz,c->cx1,c->cy1,gxmax) >= 0)
			{
				j = scale(-gposz,c->cx1,c->cy1)+PREC; //+PREC for good luck
				if ((unsigned int)j < (unsigned int)gxmax) gxmax = j;
			}
		}
		else{
			gxmax = 0;
		}
	}
#endif

		//Clip borders safely (MUST use integers!) - don't wrap around
#if ((USEZBUFFER == 1) && (defined(USEV5ASM) && (USEV5ASM != 0))) //if USEV5ASM is true
	skycast.dist = gxmax;
#endif
	if (gixy[0] < 0) j = glipos.x; else j = VSID-1-glipos.x;
	q = mul64(ray_dir[0],j); q += (uint64_t)ray_pos[0];
	if (q < (uint64_t)gxmax)
	{
		gxmax = (int)q;
#if ((USEZBUFFER == 1) && (defined(USEV5ASM) && (USEV5ASM != 0)))
		skycast.dist = 0x7fffffff;
#endif
	}
	if (gixy[1] < 0) j = glipos.y; else j = VSID-1-glipos.y;
	q = mul64(ray_dir[1],j); q += (uint64_t)ray_pos[1];
	if (q < (uint64_t)gxmax)
	{
		gxmax = (int)q;
#if ((USEZBUFFER == 1) && (defined(USEV5ASM) && (USEV5ASM != 0)))
		skycast.dist = 0x7fffffff;
#endif
	}

	if (vx5.sideshademode)
	{
		gcsub[0] = gcsub[(((unsigned int)gixy[0])>>31)+4];
		gcsub[1] = gcsub[(((unsigned int)gixy[1])>>31)+6];
	}

#if (defined(USEV5ASM) && (USEV5ASM != 0)) //if true
	if (nskypic)
	{
		if (skycurlng < 0)
		{
			ftol((atan2(vy1,vx1)+PI)*skylngmul-.5,&skycurlng);
			if ((unsigned int)skycurlng >= skyysiz)
				skycurlng = ((skyysiz-1)&(j>>31));
		}
		else if (skycurdir < 0)
		{
			j = skycurlng+1; if (j >= skyysiz) j = 0;
			while (skylng[j].x*vy1 > skylng[j].y*vx1)
				{ skycurlng = j++; if (j >= skyysiz) j = 0; }
		}
		else
		{
			while (skylng[skycurlng].x*vy1 < skylng[skycurlng].y*vx1)
				{ skycurlng--; if (skycurlng < 0) skycurlng = skyysiz-1; }
		}
		skyoff = skycurlng*skybpl + nskypic;
	}

	//resp = 0;
	grouscanasm((int)gstartv);
	//if (resp)
	//{
	//   static char tempbuf[2048], tempbuf2[256];
	//   sprintf(tempbuf,"eax:%08x\tmm0:%08x%08x\nebx:%08x\tmm1:%08x%08x\necx:%08x\tmm2:%08x%08x\nedx:%08x\tmm3:%08x%08x\nesi:%08x\tmm4:%08x%08x\nedi:%08x\tmm5:%08x%08x\nebp:%08x\tmm6:%08x%08x\nesp:%08x\tmm7:%08x%08x\n",
	//      reax,remm[ 1],remm[ 0], rebx,remm[ 3],remm[ 2],
	//      recx,remm[ 5],remm[ 4], redx,remm[ 7],remm[ 6],
	//      resi,remm[ 9],remm[ 8], redi,remm[11],remm[10],
	//      rebp,remm[13],remm[12], resp,remm[15],remm[14]);
	//
	//   for(j=0;j<3;j++
	//   {
	//      sprintf(tempbuf2,"%d i0:%d i1:%d z0:%ld z1:%ld cx0:%08x cy0:%08x cx1:%08x cy1:%08x\n",
	//         j,(int)cfasm[j].i0-(int)gscanptr,(int)cfasm[j].i1-(int)gscanptr,cfasm[j].z0,cfasm[j].z1,cfasm[j].cx0,cfasm[j].cy0,cfasm[j].cx1,cfasm[j].cy1);
	//      strcat(tempbuf,tempbuf2);
	//   }
	//   evilquit(tempbuf);
	//}
#else
//------------------------------------------------------------------------
	ce = c; v = gstartv;
	j = (((unsigned int)(ray_pos[1]-ray_pos[0]))>>31);
	gx = ray_pos[j];
	uintptr_t ixy = (uintptr_t)gpixy;
	unsigned int d_int, prev_j;
	unsigned int maxdmulruns, runs;
	unsigned int sqr_dist=1;
	unsigned char fog_alpha=255, nfog_alpha=0;
	unsigned int orfog, ogfog, obfog;
	unsigned int rfog, gfog, bfog, mm_fog;
	unsigned int colreg;
	signed int old_gx;
	const float invpwr_maxscandist=1.0/(vx5.maxscandist*vx5.maxscandist/255.0);
	const unsigned int pow_maxscandist=vx5.maxscandist*vx5.maxscandist;
	const unsigned int floor_z_inc=((unsigned int)(.5*(1<<14)/pow_maxscandist))<<16;
	const unsigned int min_z_dist=((unsigned int)(.5*(1<<14)/pow_maxscandist))<<16;
	castdat fill_dat={0, (((unsigned int)(2.0*(1<<14)/pow_maxscandist))<<16)+floor_z_inc+min_z_dist};
	orfog=vx5.fogcol&255, ogfog=(vx5.fogcol>>8)&255, obfog=(vx5.fogcol>>16)&255;
	unsigned int fog_ccomp1=vx5.fogcol&0x00ff00ff, fog_ccomp2=vx5.fogcol&0x0000ff00;
	orfog<<=8; ogfog<<=8; obfog<<=8;
	rfog=orfog*nfog_alpha; gfog=ogfog*nfog_alpha; bfog=obfog*nfog_alpha; mm_fog=rfog | (gfog<<16);

	if (v == *gpixy) goto drawflor; goto drawceil; 
	while (1)
	{

drawfwall:;
		rfog=orfog*nfog_alpha; gfog=ogfog*nfog_alpha; bfog=obfog*nfog_alpha; mm_fog=rfog | (gfog<<16);
		const char v1=v[1];
		if (v1 != c->z1)
		{
			if (v1 > c->z1){
				c->z1 = v1; 
				fill_dat.dist=(((sqr_dist+(unsigned int)((c->z1-gipos.z)*(c->z1-gipos.z)))<<16)/pow_maxscandist)<<14;
			}
			else {
#if (__USE_MMX__!=0)
				__m64 *cfpos=(__m64*)&c->cx1, mmgi=_mm_set_pi32(gi1, gi0);
#endif
			do{
				--c->z1; fill_dat.col = *(int *)&v[((c->z1-v1)<<2)+4];
#if (__USE_ACCURATE_ZBUFFER__!=0)
				//fill_dat.dist=sqr_dist+(c->z1-gipos.z)*(c->z1-gipos.z);
				fill_dat.dist=(((sqr_dist+(unsigned int)((c->z1-gipos.z)*(c->z1-gipos.z)))<<16)/pow_maxscandist)<<14;
#endif
				//int fill_dat_inc=
				//((((unsigned int)(1.0/Predict_Dmulrhs(gylookup[c->z1], c->cx1, c->cy1, old_gx, gi0, gi1)))<<14)/pow_maxscandist)<<16;
				d_int=(((unsigned char*)v)[((c->z1-v1)<<2)+7]);
				d_int+=HorizontalCubeShades[(prev_j<<1)+(gixy[prev_j]<0)];
				__GLINE_PROCESS_COLOR__(fill_dat.col, d_int);
				/*This is still faster than predicting dmulrethigh :/*/
				/*Replacing the multiplications with additions inside the loop
				doesn't increase performance either...*/
				const int_fast64_t gyz=gylookup[c->z1];
#if (__USE_MMX__!=0)
				__m64 reg=*(__m64*)&fill_dat;
#endif
				while (__BDMLRS(gyz,c->cx1,c->cy1,old_gx))
				{
#if (__USE_MMX__!=0)
					*((__m64*)c->i1--)=reg;
#else
					*c->i1--=fill_dat;
#endif
					if (c->i0 > c->i1) goto deletez;
#if (__USE_MMX__!=0)
					*cfpos-=mmgi;
#else
					c->cx1-=gi0; c->cy1-=gi1;
#endif
				}
			} while (v1 != c->z1);
			}
		}
		//fill_dat.dist=sqr_dist+(c->z1-gipos.z)*(c->z1-gipos.z);
		fill_dat.dist=(((sqr_dist+(unsigned int)((c->z1-gipos.z)*(c->z1-gipos.z)))<<16)/pow_maxscandist)<<14;

		if (v == (char *)*(uintptr_t *)ixy) goto drawflor;

//drawcwall:;
		const char v3=v[3];
		if (v3 != c->z0){
			if (v3 < c->z0) c->z0 = v3;
			else {
			do{
				++c->z0;
#if (__USE_ACCURATE_ZBUFFER__!=0)
				fill_dat.dist=(((sqr_dist+(unsigned int)((c->z0-gipos.z)*(c->z0-gipos.z)))<<16)/pow_maxscandist)<<14;
#endif
				fill_dat.col = *(int *)&v[((c->z0-v3)<<2)-4];
				d_int=(((unsigned char*)v)[((c->z0-v3)<<2)-1]>>__LSHADE_FACTOR__);
				d_int+=HorizontalCubeShades[(prev_j<<1)+(gixy[prev_j]>>31)];
				__GLINE_PROCESS_COLOR__(fill_dat.col, d_int);
				const int_fast64_t gyz=gylookup[c->z0];
				while (__BDMLREM(gyz,c->cx0,c->cy0,old_gx))
				{
					*c->i0++=fill_dat;
					if (c->i0 > c->i1) goto deletez;
					c->cx0 += gi0; c->cy0 += gi1;
				}
			} while (v3 != c->z0);
			}
		}
		if (v == (char *)*(uintptr_t *)ixy) goto drawflor;

drawceil:;
		fill_dat.col=(*(int *)&v[-4]);
#if (__USE_ACCURATE_ZBUFFER__!=0)
#endif
		d_int=(((unsigned char*)v)[-1]>>__LSHADE_FACTOR__)+CubeShadeBottom;
		__GLINE_PROCESS_COLOR__(fill_dat.col, d_int);
		while (__BDMLREM(gylookup[c->z0],c->cx0,c->cy0,gx))
		{
			*c->i0++=fill_dat;
			if (c->i0 > c->i1) goto deletez;
			c->cx0 += gi0; c->cy0 += gi1;
		}
drawflor:;
		fill_dat.col=(*(int *)&v[4]);
		fill_dat.dist+=floor_z_inc;
		d_int=(((unsigned char*)v)[7]>>__LSHADE_FACTOR__)+CubeShadeTop;
		__GLINE_PROCESS_COLOR__(fill_dat.col, d_int);
#if (__USE_ACCURATE_ZBUFFER__!=0)
#endif
#if (__USE_MMX__!=0)
		__m64 reg=*(__m64*)&fill_dat;
#endif
		{
			const int_fast64_t gyz=gylookup[c->z1];
			while (__BDMLRS(gyz,c->cx1,c->cy1,gx))
			{
#if (__USE_MMX__!=0)
				*(__m64*)c->i1--=reg;
#else
				*c->i1--=fill_dat;
#endif
				if (c->i0 > c->i1) goto deletez;
				c->cx1-=gi0; c->cy1-=gi1;
			}
		}
		fill_dat.dist-=floor_z_inc;
#if (__USE_MMX__!=0)
		_mm_empty();
#endif
afterdelete:;
		--c;
		/*Advance ray by 1 voxel*/
		if (c < &cfasm[128])
		{
			/*ixy is the position(index) on sptr, gixy is some kind of direction vector for pointer usage*/
			ixy += gixy[j];
			ray_pos[j] += ray_dir[j];
			/*j indicates whether ray.x+=x or ray.y+=y should be done (at some point, you'll always end up doing something like this in DDA) */
			/*gpz is the ray position, fixed point (x>>16)*/
			prev_j=j; j=ray_pos[1]<=ray_pos[0];	
			/*ogx is used in the loops that fill voxels into cfdata or angstart*/
			/*probably for controlling voxel size*/
			old_gx = gx; gx = ray_pos[j];
			/*gx is something like distance, gxmax is like max distance*/
			/*all are multiplied by PREC*/
			if (gx > gxmax) break;
			//Sorry, I have changed this to use proper pointers, but then it just broke on 64 bit
			v = (char *)*(uintptr_t *)ixy; c = ce;
#if (USEZBUFFER!=0)
			float dist_sqrt=PREC_DIV(gx);
			sqr_dist=dist_sqrt*dist_sqrt;
			//fill_dat.dist=sqr_dist+(c->z1-gipos.z)*(c->z1-gipos.z);
			//fill_dat.dist=((((sqr_dist+(unsigned int)((c->z1-gipos.z)*(c->z1-gipos.z)))<<16)/pow_maxscandist)<<14)+min_z_dist;
#endif
#if (__DRAW_FOG__!=0)
			nfog_alpha=sqr_dist*invpwr_maxscandist; fog_alpha=255-nfog_alpha;
#endif
		}
			//Find highest intersecting vbuf slab
		while (1)
		{
			if(!v)
				return;
			if (!*v) goto drawfwall;
			if (__BDMLREM(gylookup[v[2]+1],c->cx0,c->cy0,old_gx)) break;
			v += (*v)<<2;
		}
			//If next slab ALSO intersects, split cfasm!
		{
			const signed int gy = gylookup[v[(v[0]<<2)+3]];
			if (__BDMLRS(gy,c->cx1,c->cy1,old_gx))
			{
				castdat *col = c->i1; dax = c->cx1; day = c->cy1;
				while (__BDMLRS(gylookup[v[2]+1],dax,day,old_gx))
					{ col--; dax -= gi0; day -= gi1; }
				++ce; if (ce >= &cfasm[192]) return; //Give it max=64 entries like ASM
				for(c2=ce;c2>c;--c2) *c2 = c2[-1];
				++c;
	
				c->i1 = col; c[-1].i0 = col+1;
				c->cx1 = dax; c[-1].cx0 = dax+gi0;
				c->cy1 = day; c[-1].cy0 = day+gi1;
				c->z1 = c[-1].z0 = v[(v[0]<<2)+3];
			}
		}
	}
//------------------------------------------------------------------------
	const castdat skydat={vx5.fogcol, 1u<<30u};
	castdat *end_ptr;
#if (__USE_MMX__!=0)
	__m64 reg=*(__m64*)&skydat;
#endif
	for(c=ce;c>=&cfasm[128];--c){
		for(;c->i0<=c->i1;)
#if (__USE_MMX__!=0)
			*(__m64*)c->i0++=reg;
#else
			*c->i0++=skydat;
#endif
	}
	return;

deletez:;
	--ce; if (ce < &cfasm[128]) return;
	for(c2=c;c2<=ce;++c2) *c2 = c2[1];
	goto afterdelete;
#endif
}

static inline void addusb (char *a, int b)
{
	(*a) += b; if ((*a) < b) (*a) = 255;
}

	// (cone diameter vs. % 3D angular area) or: (a vs. 2/(1-cos(a*.5*PI/180)))
	// ÚÄÄÄÄÄÄÄÄÄÄÄÄÄÂÄÄÄÄÄÄÄÄÄÄÄÂÄÄÄÄÄÄÄÄÄÄÄÂÄÄÄÄÄÄÄÄÄÄÂÄÄÄÄÄÄÄÄÄÄÄÂÄÄÄÄÄÄÄÄÄ¿
	// ³  0: inf     ³ 25: 84.37 ³ 50: 21.35 ³ 75: 9.68 ³ 100: 5.60 ³ 180: 2  ³
	// ³  5: 2101.33 ³ 30: 58.70 ³ 55: 17.70 ³ 80: 8.55 ³ 105: 5.11 ³ 360: 1  ³
	// ³ 10:  525.58 ³ 35: 43.21 ³ 60: 14.93 ³ 85: 7.61 ³ 110: 4.69 ÃÄÄÄÄÄÄÄÄÄÙ
	// ³ 15:  233.78 ³ 40: 33.16 ³ 65: 12.77 ³ 90: 6.83 ³ 115: 4.32 ³
	// ³ 20:  131.65 ³ 45: 26.27 ³ 70: 11.06 ³ 95: 6.17 ³ 120: 4    ³
	// ÀÄÄÄÄÄÄÄÄÄÄÄÄÄÁÄÄÄÄÄÄÄÄÄÄÄÁÄÄÄÄÄÄÄÄÄÄÄÁÄÄÄÄÄÄÄÄÄÄÁÄÄÄÄÄÄÄÄÄÄÄÙ
void setflash (float px, float py, float pz, int flashradius, int numang, int intens)
{
	uint64_t q;
	float vx, vy;
	int i, j, gx, ogx, angoff;
	size_t col, ixy;
	int ipx, ipy, ipz, sz0, sz1;
	cftype *c, *c2, *ce;
	char *v, *vs;
	int gdz[2], gpz[2];
	
#if (defined(USEV5ASM) && (USEV5ASM != 0)) //if true
	EXTERN_C cftype cfasm[256]={0};
	EXTERN_C castdat skycast;
#else
	cftype cfasm[256]={0};
#endif
	int gxmax, gixy[2];

	ipx = (int)px; ipy = (int)py; ipz = (int)pz;
	vx5.minx = ipx-flashradius; vx5.maxx = ipx+flashradius+1;
	vx5.miny = ipy-flashradius; vx5.maxy = ipy+flashradius+1;
	vx5.minz = ipz-flashradius; vx5.maxz = ipz+flashradius+1;

	if (flashradius > 1023) flashradius = 1023;
	flashradius *= FPREC;

	flashbrival = (intens<<24);
	angoff = ((0x52741630>>(flashcnt<<2))&15); flashcnt++;

	gposxfrac[1] = px - (float)(ipx); gposxfrac[0] = 1 - gposxfrac[1];
	gposyfrac[1] = py - (float)(ipy); gposyfrac[0] = 1 - gposyfrac[1];
	gpixy = &sptr[ipy*VSID + ipx];
	ftol(pz*FPREC-.5f,&gposz);
	for(gylookup[0]=-gposz,i=1;i<260;i++) gylookup[i] = gylookup[i-1]+FPREC;

	vs = *gpixy;
	if (ipz >= vs[1])
	{
		do
		{
			if (!vs[0]) return;
			vs += vs[0]*4;
		} while (ipz >= vs[1]);
		if (ipz < vs[3]) return;
		sz0 = vs[3];
	} else sz0 = 0;
	sz1 = vs[1];

	for(i=0;i<numang;i++)
	{
		fcossin(((float)i+(float)angoff*.125f)*PI*2.0f/(float)numang,&vx,&vy);

		ftol(FPREC/fabs(vx),&gdz[0]);
		ftol(FPREC/fabs(vy),&gdz[1]);

		gixy[0] = (((*(signed int *)&vx)>>31) & (     -8)) +      4;
		gixy[1] = (((*(signed int *)&vy)>>31) & (VSID*-8)) + VSID*4;
		if (gdz[0] < 0) { gpz[0] = 0x7fffffff; gdz[0] = 0; } //Hack for divide overflow
		else ftol(gposxfrac[(*(unsigned int *)&vx)>>31]*(float)gdz[0],&gpz[0]);
		if (gdz[1] < 0) { gpz[1] = 0x7fffffff; gdz[1] = 0; } //Hack for divide overflow
		else ftol(gposyfrac[(*(unsigned int *)&vy)>>31]*(float)gdz[1],&gpz[1]);

		c = ce = &cfasm[128];
		v = vs; c->z0 = sz0; c->z1 = sz1;
			//Note!  These substitions are used in flashscan:
			//   c->i0 in flashscan is now: c->cx0
			//   c->i1 in flashscan is now: c->cx1
		c->cx0 = (((i+flashcnt+rand())&7)<<LOGFLASHVANG);
		c->cx1 = c->cx0+(1<<LOGFLASHVANG)-1;

		gxmax = flashradius;

			//Clip borders safely (MUST use integers!) - don't wrap around
		if (gixy[0] < 0) j = ipx; else j = VSID-1-ipx;
		q = mul64(gdz[0],j); q += (uint64_t)gpz[0];
		if (q < (uint64_t)gxmax) gxmax = (int)q;
		if (gixy[1] < 0) j = ipy; else j = VSID-1-ipy;
		q = mul64(gdz[1],j); q += (uint64_t)gpz[1];
		if (q < (uint64_t)gxmax) gxmax = (int)q;

	//------------------------------------------------------------------------
		j = (((unsigned int)(gpz[1]-gpz[0]))>>31);
		gx = gpz[j];
		ixy = (size_t)gpixy; //64 BIT DANGER: WON'T WORK AT ALL LIKE THIS (AND AT ALL OTHER SPOTS IN THIS FUNCTION)
		if (v == (char *)*(size_t *)gpixy) goto fdrawflor; goto fdrawceil;

		while (1)
		{

fdrawfwall:;
			if (v[1] != c->z1)
			{
				if (v[1] > c->z1) c->z1 = v[1];
				else { do
				{
					c->z1--; col = (size_t)&v[(c->z1-v[1])*4+4];
					while (dmulrethigh(gylookup[c->z1],gfc[c->cx1].x,gfc[c->cx1].y,ogx) < 0)
					{
						mmxcoloradd((int *)col); c->cx1--;
						if (c->cx0 > c->cx1) goto fdeletez;
					}
				} while (v[1] != c->z1); }
			}

			if (v == (char *)*(size_t *)ixy) goto fdrawflor;

//fdrawcwall:;
			if (v[3] != c->z0)
			{
				if (v[3] < c->z0) c->z0 = v[3];
				else { do
				{
					c->z0++; col = (size_t)&v[(c->z0-v[3])*4-4];
					while (dmulrethigh(gylookup[c->z0],gfc[c->cx0].x,gfc[c->cx0].y,ogx) >= 0)
					{
						mmxcoloradd((int *)col); c->cx0++;
						if (c->cx0 > c->cx1) goto fdeletez;
					}
				} while (v[3] != c->z0); }
			}

fdrawceil:;
			while (dmulrethigh(gylookup[c->z0],gfc[c->cx0].x,gfc[c->cx0].y,gx) >= 0)
			{
				mmxcoloradd((int *)&v[-4]); c->cx0++;
				if (c->cx0 > c->cx1) goto fdeletez;
			}

fdrawflor:;
			while (dmulrethigh(gylookup[c->z1],gfc[c->cx1].x,gfc[c->cx1].y,gx) < 0)
			{
				mmxcoloradd((int *)&v[4]); c->cx1--;
				if (c->cx0 > c->cx1) goto fdeletez;
			}

fafterdelete:;
			c--;
			if (c < &cfasm[128])
			{
				ixy += gixy[j];
				gpz[j] += gdz[j];
				j = (((unsigned int)(gpz[1]-gpz[0]))>>31);
				ogx = gx; gx = gpz[j];

				if (gx > gxmax) break;
				v = (char *)*(size_t *)ixy; c = ce;
			}
				//Find highest intersecting vbuf slab
			while (1)
			{
				if (!v[0]) goto fdrawfwall;
				if (dmulrethigh(gylookup[v[2]+1],gfc[c->cx0].x,gfc[c->cx0].y,ogx) >= 0) break;
				v += v[0]*4;
			}
				//If next slab ALSO intersects, split cfasm!
			if (dmulrethigh(gylookup[v[v[0]*4+3]],gfc[c->cx1].x,gfc[c->cx1].y,ogx) < 0)
			{
				col = c->cx1;
				while (dmulrethigh(gylookup[v[2]+1],gfc[col].x,gfc[col].y,ogx) < 0)
					col--;
				ce++; if (ce >= &cfasm[192]) break; //Give it max=64 entries like ASM
				for(c2=ce;c2>c;c2--) c2[0] = c2[-1];
				c[1].cx1 = col; c->cx0 = col+1;
				c[1].z1 = c->z0 = v[v[0]*4+3];
				c++;
			}
		}
fcontinue:;
	}

	updatebbox(vx5.minx,vx5.miny,vx5.minz,vx5.maxx,vx5.maxy,vx5.maxz,0);
	return;

fdeletez:;
	ce--; if (ce < &cfasm[128]) goto fcontinue;
	for(c2=c;c2<=ce;c2++) c2[0] = c2[1];
	goto fafterdelete;
}

#if (ESTNORMRAD == 2)
static signed char bitnum[32] =
{
	0,1,1,2,1,2,2,3,1,2,2,3,2,3,3,4,
	1,2,2,3,2,3,3,4,2,3,3,4,3,4,4,5
};
//static int bitsum[32] =
//{
//   0,-2,-1,-3, 0,-2,-1,-3, 1,-1, 0,-2, 1,-1, 0,-2,
//   2, 0, 1,-1, 2, 0, 1,-1, 3, 1, 2, 0, 3, 1, 2, 0
//};
static int bitsnum[32] =
{
	0        ,1-(2<<16),1-(1<<16),2-(3<<16),
	1        ,2-(2<<16),2-(1<<16),3-(3<<16),
	1+(1<<16),2-(1<<16),2        ,3-(2<<16),
	2+(1<<16),3-(1<<16),3        ,4-(2<<16),
	1+(2<<16),2        ,2+(1<<16),3-(1<<16),
	2+(2<<16),3        ,3+(1<<16),4-(1<<16),
	2+(3<<16),3+(1<<16),3+(2<<16),4,
	3+(3<<16),4+(1<<16),4+(2<<16),5
};
static float fsqrecip[5860]; //75*75 + 15*15 + 3*3 = 5859 is max value (5*5*5 box)
#endif

void estnorm (int x, int y, int z, point3d *fp)
{
	lpoint3d n;
	int *lptr, xx, yy, zz, b[5], i, j, k;
	float f;

	n.x = 0; n.y = 0; n.z = 0;

#if (ESTNORMRAD == 2)
	if (labs(x-xbsox) + labs(y-xbsoy) > 1)
	{
			//x,y not close enough to cache: calls expandbitstack 25 times :(
		xbsox = x; xbsoy = y; xbsof = 24*5;
		lptr = (int *)(&xbsbuf[24*5+1]);
		for(yy=-ESTNORMRAD;yy<=ESTNORMRAD;yy++)
			for(xx=-ESTNORMRAD;xx<=ESTNORMRAD;xx++,lptr-=10)
				expandbitstack(x+xx,y+yy,(int64_t *)lptr);
	}
	else if (x != xbsox)
	{
			//shift xbsbuf cache left/right: calls expandbitstack 5 times :)
		if (x < xbsox) { xx = -ESTNORMRAD; xbsof -= 24*5; lptr = (int *)(&xbsbuf[xbsof+1]); }
					 else { xx = ESTNORMRAD; lptr = (int *)(&xbsbuf[xbsof-5*5+1]); xbsof -= 1*5; }
		xbsox = x; if (xbsof < 0) xbsof += 25*5;
		for(yy=-ESTNORMRAD;yy<=ESTNORMRAD;yy++)
		{
			if (lptr < (int *)&xbsbuf[1]) lptr += 25*10;
			expandbitstack(x+xx,y+yy,(int64_t *)lptr);
			lptr -= 5*10;
		}
	}
	else if (y != xbsoy)
	{
			//shift xbsbuf cache up/down: calls expandbitstack 5 times :)
		if (y < xbsoy) { yy = -ESTNORMRAD; xbsof -= 20*5; lptr = (int *)(&xbsbuf[xbsof+1]); }
					 else { yy = ESTNORMRAD; lptr = (int *)(&xbsbuf[xbsof+1]); xbsof -= 5*5; }
		xbsoy = y; if (xbsof < 0) xbsof += 25*5;
		for(xx=-ESTNORMRAD;xx<=ESTNORMRAD;xx++)
		{
			if (lptr < (int *)&xbsbuf[1]) lptr += 25*10;
			expandbitstack(x+xx,y+yy,(int64_t *)lptr);
			lptr -= 1*10;
		}
	}

	z -= ESTNORMRAD;
	if ((z&31) <= 27) //2 <= (z&31) <= 29
		{ lptr = (int *)((size_t)(&xbsbuf[xbsof+1]) + ((z&~31)>>3)); z &= 31; }
	else
		{ lptr = (int *)((size_t)(&xbsbuf[xbsof+1]) + (z>>3)); z &= 7; }

	for(yy=-ESTNORMRAD;yy<=ESTNORMRAD;yy++)
	{
		if (lptr >= (int *)&xbsbuf[1+10*5])
		{
			b[0] = ((lptr[  0]>>z)&31); b[1] = ((lptr[-10]>>z)&31);
			b[2] = ((lptr[-20]>>z)&31); b[3] = ((lptr[-30]>>z)&31);
			b[4] = ((lptr[-40]>>z)&31); lptr -= 50;
		}
		else
		{
			b[0] = ((lptr[0]>>z)&31); lptr -= 10; if (lptr < (int *)&xbsbuf[1]) lptr += 25*10;
			b[1] = ((lptr[0]>>z)&31); lptr -= 10; if (lptr < (int *)&xbsbuf[1]) lptr += 25*10;
			b[2] = ((lptr[0]>>z)&31); lptr -= 10; if (lptr < (int *)&xbsbuf[1]) lptr += 25*10;
			b[3] = ((lptr[0]>>z)&31); lptr -= 10; if (lptr < (int *)&xbsbuf[1]) lptr += 25*10;
			b[4] = ((lptr[0]>>z)&31); lptr -= 10; if (lptr < (int *)&xbsbuf[1]) lptr += 25*10;
		}

			//Make filter spherical
		//if (yy&1) { b[0] &= 0xe; b[4] &= 0xe; }
		//else if (yy) { b[0] &= 0x4; b[1] &= 0xe; b[3] &= 0xe; b[4] &= 0x4; }

		n.x += ((bitnum[b[4]]-bitnum[b[0]])<<1)+bitnum[b[3]]-bitnum[b[1]];
		j = bitsnum[b[0]]+bitsnum[b[1]]+bitsnum[b[2]]+bitsnum[b[3]]+bitsnum[b[4]];
		n.z += j; n.y += (*(signed short *)&j)*yy;
	}
	n.z >>= 16;
#else
	for(yy=-ESTNORMRAD;yy<=ESTNORMRAD;yy++)
		for(xx=-ESTNORMRAD;xx<=ESTNORMRAD;xx++)
			for(zz=-ESTNORMRAD;zz<=ESTNORMRAD;zz++)
				if (isvoxelsolid(x+xx,y+yy,z+zz))
					{ n.x += xx; n.y += yy; n.z += zz; }
#endif
	f = fsqrecip[n.x*n.x + n.y*n.y + n.z*n.z];
	fp->x = ((float)n.x)*f; fp->y = ((float)n.y)*f; fp->z = ((float)n.z)*f;
}

static int vspan (int x, int y0, int y1)
{
	int y, yy, *bbufx;

	y = (y0>>5); bbufx = &bbuf[x][0];
	if ((y1>>5) == y)
	{
		yy = bbufx[y]; bbufx[y] &= ~(p2m[y1&31]^p2m[y0&31]);
		return(bbufx[y] ^ yy);
	}

	if (!(bbufx[y]&(~p2m[y0&31])))
		if (!(bbufx[y1>>5]&p2m[y1&31]))
		{
			for(yy=(y1>>5)-1;yy>y;yy--)
				if (bbufx[yy]) goto vspan_skip;
			return(0);
		}
vspan_skip:;
	bbufx[y] &= p2m[y0&31];
	bbufx[y1>>5] &= (~p2m[y1&31]);
	for(yy=(y1>>5)-1;yy>y;yy--) bbufx[yy] = 0;
	return(1);
}

static int docube (int x, int y, int z)
{
	int x0, y0, x1, y1, g;

	ffxptr = &ffx[(z+1)*z-1];
	x0 = (int)ffxptr[x].x; x1 = (int)ffxptr[x].y;
	y0 = (int)ffxptr[y].x; y1 = (int)ffxptr[y].y;
	for(g=0;x0<x1;x0++) g |= vspan(x0,y0,y1);
	return(g);
}

void setnormflash (float px, float py, float pz, int flashradius, int intens)
{
	point3d fp;
	float f, fintens;
	int i, j, k, l, m, x, y, z, xx, yy, xi, yi, xe, ye, ipx, ipy, ipz;
	int ceilnum, sq;
	unsigned char *v;

	ipx = (int)px; ipy = (int)py; ipz = (int)pz;
	vx5.minx = ipx-flashradius+1; vx5.maxx = ipx+flashradius;
	vx5.miny = ipy-flashradius+1; vx5.maxy = ipy+flashradius;
	vx5.minz = ipz-flashradius+1; vx5.maxz = ipz+flashradius;

	if (isvoxelsolid(ipx,ipy,ipz)) return;

	fintens = intens;
	if (flashradius > (GSIZ>>1)) flashradius = (GSIZ>>1);

	xbsox = -17;

		//       ÚÄ 7Ä¿
		//      11  . 8
		//  ÚÄ11ÄÅÄ 4ÄÅÄ 8ÄÂÄ 7Ä¿
		//  3 |  0  + 1  | 2  + 3
		//  ÀÄ10ÄÅÄ 5ÄÅÄ 9ÄÁÄ 6ÄÙ
		//      10  . 9
		//       ÀÄ 6ÄÙ

		//Do left&right faces of the cube
	for(j=1;j>=0;j--)
	{
		clearbuf((void *)bbuf,GSIZ*(GSIZ>>5),0xffffffff);
		for(y=1;y<flashradius;y++)
		{
			if (j) yy = ipy-y; else yy = ipy+y;
			for(xi=1,xe=y+1;xi>=-1;xi-=2,xe=-xe)
				for(x=(xi>>1);x!=xe;x+=xi)
				{
					xx = ipx+x;
					if ((unsigned int)(xx|yy) >= VSID) continue;
					v = sptr[yy*VSID+xx]; i = 0; sq = x*x+y*y;
					while (1)
					{
						for(z=v[1];z<=v[2];z++)
						{
							if (z-ipz < 0) { tbuf2[i] = z-ipz; tbuf2[i+1] = (size_t)&v[(z-v[1])*4+4]; i += 2; }
							else
							{
								//if (z-ipz < -y) continue; //TEMP HACK!!!
								if (z-ipz > y) goto normflash_exwhile1;
								if (!docube(x,z-ipz,y)) continue;
								estnorm(xx,yy,z,&fp); if (j) fp.y = -fp.y;
								f = fp.x*x + fp.y*y + fp.z*(z-ipz);
								if (*(int *)&f > 0) addusb(&v[(z-v[1])*4+7],f*fintens/((z-ipz)*(z-ipz)+sq));
							}
						}
						if (!v[0]) break;
						ceilnum = v[2]-v[1]-v[0]+2; v += v[0]*4;
						for(z=v[3]+ceilnum;z<v[3];z++)
						{
							if (z < ipz) { tbuf2[i] = z-ipz; tbuf2[i+1] = (size_t)&v[(z-v[3])*4]; i += 2; }
							else
							{
								//if (z-ipz < -y) continue; //TEMP HACK!!!
								if (z-ipz > y) goto normflash_exwhile1;
								if (!docube(x,z-ipz,y)) continue;
								estnorm(xx,yy,z,&fp); if (j) fp.y = -fp.y;
								f = fp.x*x + fp.y*y + fp.z*(z-ipz);
								if (*(int *)&f > 0) addusb(&v[(z-v[3])*4+3],f*fintens/((z-ipz)*(z-ipz)+sq));
							}
						}
					}
normflash_exwhile1:;
					while (i > 0)
					{
						i -= 2; if (tbuf2[i] < -y) break;
						if (!docube(x,tbuf2[i],y)) continue;
						estnorm(xx,yy,tbuf2[i]+ipz,&fp); if (j) fp.y = -fp.y;
						f = fp.x*x + fp.y*y + fp.z*tbuf2[i];
						if (*(int *)&f > 0) addusb(&((char *)tbuf2[i+1])[3],f*fintens/(tbuf2[i]*tbuf2[i]+sq));
					}
				}
		}
	}

		//Do up&down faces of the cube
	for(j=1;j>=0;j--)
	{
		clearbuf((void *)bbuf,GSIZ*(GSIZ>>5),0xffffffff);
		for(y=1;y<flashradius;y++)
		{
			if (j) xx = ipx-y; else xx = ipx+y;
			for(xi=1,xe=y+1;xi>=-1;xi-=2,xe=-xe)
				for(x=(xi>>1);x!=xe;x+=xi)
				{
					yy = ipy+x;
					if ((unsigned int)(xx|yy) >= VSID) continue;
					v = sptr[yy*VSID+xx]; i = 0; sq = x*x+y*y; m = x+xi-xe;
					while (1)
					{
						for(z=v[1];z<=v[2];z++)
						{
							if (z-ipz < 0) { tbuf2[i] = z-ipz; tbuf2[i+1] = (size_t)&v[(z-v[1])*4+4]; i += 2; }
							else
							{
								//if (z-ipz < -y) continue; //TEMP HACK!!!
								if (z-ipz > y) goto normflash_exwhile2;
								if ((!docube(x,z-ipz,y)) || (!m)) continue;
								estnorm(xx,yy,z,&fp); if (j) fp.x = -fp.x;
								f = fp.x*y + fp.y*x + fp.z*(z-ipz);
								if (*(int *)&f > 0) addusb(&v[(z-v[1])*4+7],f*fintens/((z-ipz)*(z-ipz)+sq));
							}
						}
						if (!v[0]) break;
						ceilnum = v[2]-v[1]-v[0]+2; v += v[0]*4;
						for(z=v[3]+ceilnum;z<v[3];z++)
						{
							if (z < ipz) { tbuf2[i] = z-ipz; tbuf2[i+1] = (size_t)&v[(z-v[3])*4]; i += 2; }
							else
							{
								//if (z-ipz < -y) continue; //TEMP HACK!!!
								if (z-ipz > y) goto normflash_exwhile2;
								if ((!docube(x,z-ipz,y)) || (!m)) continue;
								estnorm(xx,yy,z,&fp); if (j) fp.x = -fp.x;
								f = fp.x*y + fp.y*x + fp.z*(z-ipz);
								if (*(int *)&f > 0) addusb(&v[(z-v[3])*4+3],f*fintens/((z-ipz)*(z-ipz)+sq));
							}
						}
					}
normflash_exwhile2:;
					while (i > 0)
					{
						i -= 2; if (tbuf2[i] < -y) break;
						if ((!docube(x,tbuf2[i],y)) || (!m)) continue;
						estnorm(xx,yy,tbuf2[i]+ipz,&fp); if (j) fp.x = -fp.x;
						f = fp.x*y + fp.y*x + fp.z*tbuf2[i];
						if (*(int *)&f > 0) addusb(&((char *)tbuf2[i+1])[3],f*fintens/(tbuf2[i]*tbuf2[i]+sq));
					}
				}
		}
	}

		//Do the bottom face of the cube
	clearbuf((void *)bbuf,GSIZ*(GSIZ>>5),0xffffffff);
	for(yi=1,ye=flashradius+1;yi>=-1;yi-=2,ye=-ye)
		for(y=(yi>>1);y!=ye;y+=yi)
			for(xi=1,xe=flashradius+1;xi>=-1;xi-=2,xe=-xe)
				for(x=(xi>>1);x!=xe;x+=xi)
				{
					xx = ipx+x; yy = ipy+y;
					if ((unsigned int)(xx|yy) >= VSID) goto normflash_exwhile3;
					k = MAX(labs(x),labs(y));

					v = sptr[yy*VSID+xx]; sq = x*x+y*y;
					while (1)
					{
						for(z=v[1];z<=v[2];z++)
						{
							if (z-ipz < k) continue;
							if (z-ipz >= flashradius) goto normflash_exwhile3;
							if ((!docube(x,y,z-ipz)) || (z-ipz == k)) continue;
							estnorm(xx,yy,z,&fp);
							f = fp.x*x + fp.y*y + fp.z*(z-ipz);
							if (*(int *)&f > 0) addusb(&v[(z-v[1])*4+7],f*fintens/((z-ipz)*(z-ipz)+sq));
						}
						if (!v[0]) break;
						ceilnum = v[2]-v[1]-v[0]+2; v += v[0]*4;
						for(z=v[3]+ceilnum;z<v[3];z++)
						{
							if (z-ipz < k) continue;
							if (z-ipz >= flashradius) goto normflash_exwhile3;
							if ((!docube(x,y,z-ipz)) || (z-ipz <= k)) continue;
							estnorm(xx,yy,z,&fp);
							f = fp.x*x + fp.y*y + fp.z*(z-ipz);
							if (*(int *)&f > 0) addusb(&v[(z-v[3])*4+3],f*fintens/((z-ipz)*(z-ipz)+sq));
						}
					}
normflash_exwhile3:;
				}


		//Do the top face of the cube
	clearbuf((void *)bbuf,GSIZ*(GSIZ>>5),0xffffffff);
	for(yi=1,ye=flashradius+1;yi>=-1;yi-=2,ye=-ye)
		for(y=(yi>>1);y!=ye;y+=yi)
			for(xi=1,xe=flashradius+1;xi>=-1;xi-=2,xe=-xe)
				for(x=(xi>>1);x!=xe;x+=xi)
				{
					xx = ipx+x; yy = ipy+y;
					if ((unsigned int)(xx|yy) >= VSID) goto normflash_exwhile4;
					k = MAX(labs(x),labs(y)); m = ((x+xi != xe) && (y+yi != ye));

					v = sptr[yy*VSID+xx]; i = 0; sq = x*x+y*y;
					while (1)
					{
						for(z=v[1];z<=v[2];z++)
						{
							if (ipz-z >= flashradius) continue;
							if (ipz-z < k) goto normflash_exwhile4;
							tbuf2[i] = ipz-z; tbuf2[i+1] = (size_t)&v[(z-v[1])*4+4]; i += 2;
						}
						if (!v[0]) break;
						ceilnum = v[2]-v[1]-v[0]+2; v += v[0]*4;
						for(z=v[3]+ceilnum;z<v[3];z++)
						{
							if (ipz-z >= flashradius) continue;
							if (ipz-z < k) goto normflash_exwhile4;
							tbuf2[i] = ipz-z; tbuf2[i+1] = (size_t)&v[(z-v[3])*4]; i += 2;
						}
					}
normflash_exwhile4:;
					while (i > 0)
					{
						i -= 2;
						if ((!docube(x,y,tbuf2[i])) || (tbuf2[i] <= k)) continue;
						estnorm(xx,yy,ipz-tbuf2[i],&fp);
						f = fp.x*x + fp.y*y - fp.z*tbuf2[i];
						if (*(int *)&f > 0) addusb(&((char *)tbuf2[i+1])[3],f*fintens/(tbuf2[i]*tbuf2[i]+sq));
					}
				}
	updatebbox(vx5.minx,vx5.miny,vx5.minz,vx5.maxx,vx5.maxy,vx5.maxz,0);
}

void hline (float x0, float y0, float x1, float y1, int *ix0, int *ix1,
const int wx0, const int wy0, const int wx1, const int wy1, const int iwx0, const int iwy0, const int iwx1, const int iwy1, castdat *gscanptr)
{
	float dyx;

	dyx = (y1-y0) * grd; //grd = 1/(x1-x0)

		  if (y0 < wy0) ftol((wy0-y0)/dyx+x0,ix0);
	else if (y0 > wy1) ftol((wy1-y0)/dyx+x0,ix0);
	else ftol(x0,ix0);
		  if (y1 < wy0) ftol((wy0-y0)/dyx+x0,ix1);
	else if (y1 > wy1) ftol((wy1-y0)/dyx+x0,ix1);
	else ftol(x1,ix1);
	if ((*ix0) < iwx0) (*ix0) = iwx0;
	if ((*ix0) > iwx1) (*ix0) = iwx1; //(*ix1) = MIN(MAX(*ix1,wx0),wx1);
	gline(labs((*ix1)-(*ix0)),(float)(*ix0),((*ix0)-x1)*dyx + y1,
									  (float)(*ix1),((*ix1)-x1)*dyx + y1, gscanptr);
}

void vline (float x0, float y0, float x1, float y1, int *iy0, int *iy1,
const int wx0, const int wy0, const int wx1, const int wy1, const int iwx0, const int iwy0, const int iwx1, const int iwy1, castdat *gscanptr)
{
	float dxy;
	
	
	dxy = (x1-x0) * grd; //grd = 1/(y1-y0)

		  if (x0 < wx0) ftol((wx0-x0)/dxy+y0,iy0);
	else if (x0 > wx1) ftol((wx1-x0)/dxy+y0,iy0);
	else ftol(y0,iy0);
		  if (x1 < wx0) ftol((wx0-x0)/dxy+y0,iy1);
	else if (x1 > wx1) ftol((wx1-x0)/dxy+y0,iy1);
	else ftol(y1,iy1);
	if ((*iy0) < iwy0) (*iy0) = iwy0;
	if ((*iy0) > iwy1) (*iy0) = iwy1;
	gline(labs((*iy1)-(*iy0)),((*iy0)-y1)*dxy + x1,(float)(*iy0),
									  ((*iy1)-y1)*dxy + x1,(float)(*iy1), gscanptr);
}

static int64_t foglut[2048], fogcol;
static int ofogdist = -1;

void (*hrend)(int,int,int,int,int,int,castdat *[MAXXDIM*4])=NULL;
void (*vrend)(int,int,int,int,int,castdat *[MAXXDIM*4],unsigned int*)=NULL;

#if (USEZBUFFER != 1) //functions without Z Buffer

	// Portable C/C++
void hrendnoz (int sx, int sy, int p1, int plc, int incr, int j, castdat *angstart[MAXXDIM*4])
{
	sy = ylookup[sy]+frameplace; p1 = sy+(p1<<2); sy += (sx<<2);
	do
	{
		*(int *)sy = angstart[plc>>16][j].col;
		plc += incr; sy += 4;
	} while (sy != p1);
}

void vrendnoz (int sx, int sy, int p1, int iplc, int iinc, castdat *angstart[MAXXDIM*4], unsigned int *uurend)
{
	sy = ylookup[sy]+(sx<<2)+frameplace;
	for(;sx<p1;sx++)
	{
		*(int *)sy = angstart[uurend[sx]>>16][iplc].col;
		uurend[sx] += uurend[sx+MAXXDIM]; sy += 4; iplc += iinc;
	}
}

#else //functions with Z Buffer

/*void hrendz (int sx, int sy, int p1, int plc, int incr, int j)
{
	int p0, i; float dirx, diry;
	p0 = ylookup[sy]+(sx<<2)+frameplace;
	p1 = ylookup[sy]+(p1<<2)+frameplace;
	dirx = optistrx*(float)sx + optiheix*(float)sy + optiaddx;
	diry = optistry*(float)sx + optiheiy*(float)sy + optiaddy;
	i = zbufoff;
	do
	{
		*(int *)p0 = angstart[plc>>16][j].col;
		*(float *)(p0+i) = (float)angstart[plc>>16][j].dist/sqrt(dirx*dirx+diry*diry);
		dirx += optistrx; diry += optistry; plc += incr; p0 += 4;
	} while (p0 != p1);
}*/

/*
 * Here you see how deprecated Voxlap is: fast_hrendz is taking up roughly 40% of the used CPU power to merely fill the screen, because
 * it needs to do entire two pointer dereferences wildly across a ~2MB buffer to get a pixel's colour and Z value instead of one
 * (RIP any caching or memory optimizations, which didn't even exist back then)
*/
void fast_hrendz(int startx, int ypos, int endx, __REGISTER int plc, __REGISTER int incr, __REGISTER int j, castdat *angstart[MAXXDIM*4]){
	enum{
		Upper_Limit=(MAXXDIM*4)<<16
	};
	if(plc<0 || plc>Upper_Limit)
		return;
#if 1
	//Faster version
	/*float dirx = optistrx*(float)startx + optiheix*(float)ypos + optiaddx;
	float diry = optistry*(float)startx + optiheiy*(float)ypos + optiaddy;
	float zbuf_ratio=(1<<30)/(float)(vx5.maxscandist*vx5.maxscandist);
	float dir_length=sqrt(dirx*dirx+diry*diry);
	float dir_veclength=sqrt(optistrx*optistrx+optistry*optistry);*/
	//dirx += optistrx; diry += optistry;
	unsigned int *pixelptr=(unsigned int*)(ylookup[ypos]+(startx<<2)+frameplace);
	unsigned int *zbufptr=(unsigned int*)(((unsigned char*)pixelptr)+zbufoff);
	int targetplc=MIN(Upper_Limit, plc+(endx-startx)*incr);
	do{
		*pixelptr++=angstart[plc>>16][j].col; *zbufptr++=angstart[plc>>16][j].dist;
		plc += incr;
	}while(plc<targetplc && plc>0);
#else
	unsigned int ctr, *pixelptr=ylookup[ypos]+frameplace;
	float *zbufptr=(float*)(((unsigned char*)pixelptr)+zbufoff);
	for(ctr=startx; ctr<endx && plc>0 && plc<Upper_Limit; ctr++){
		pixelptr[ctr]=angstart[plc>>16][j].col;
		zbufptr[ctr]=angstart[plc>>16][j].dist;
		plc+=incr;
	}
#endif
}

void vrendz (int sx, int sy, int p1, int iplc, int iinc, castdat *angstart[MAXXDIM*4], unsigned int *uurend)
{
	int i, *p0;
	__REGISTER castdat *ang_ptr;
	p0 = (int*)(ylookup[sy]+(sx<<2)+frameplace);
	int *endptr = (int*)(ylookup[sy]+(p1<<2)+frameplace);
	i = zbufoff;	
	enum{
		Upper_Limit=(MAXXDIM*4)<<16
	};
	while (p0 < endptr && uurend[sx]<Upper_Limit)
	{
		ang_ptr=&angstart[uurend[sx]>>16][iplc];
		*p0 = ang_ptr->col;
		*(unsigned int *)(((unsigned char*)p0)+i) = (unsigned int)ang_ptr->dist;
		uurend[sx] += uurend[sx+MAXXDIM]; p0 ++; iplc += iinc; ++sx;
	}
}

/*Original*/
/*void hrendzfog (int sx, int sy, int p1, int plc, int incr, int j)
{
	int p0, i, k, l; float dirx, diry;
	p0 = ylookup[sy]+(sx<<2)+frameplace;
	p1 = ylookup[sy]+(p1<<2)+frameplace;
	dirx = optistrx*(float)sx + optiheix*(float)sy + optiaddx;
	diry = optistry*(float)sx + optiheiy*(float)sy + optiaddy;
	i = zbufoff;
	do
	{
		k = angstart[plc>>16][j].col;
		l = angstart[plc>>16][j].dist;
		l = (foglut[l>>20]&32767);
		*(int *)p0 = ((((( vx5.fogcol     &255)-( k     &255))*l)>>15)    ) +
						  ((((((vx5.fogcol>> 8)&255)-((k>> 8)&255))*l)>>15)<< 8) +
						  ((((((vx5.fogcol>>16)&255)-((k>>16)&255))*l)>>15)<<16)+k;
		*(float *)(p0+i) = (float)angstart[plc>>16][j].dist/sqrt(dirx*dirx+diry*diry);
		dirx += optistrx; diry += optistry; plc += incr; p0 += 4;
	} while (p0 != p1);
}*/

#endif

/**
 * Set global camera position for future voxlap5 engine calls. Functions
 * that depend on this include: opticast, drawsprite, spherefill, etc...
 *
 * The 5th & 6th parameters define the center of the screen projection. This
 * is the point on the screen that intersects the <ipos + ifor*t> vector.
 *
 * The last parameter is the focal length - use it to control zoom. If you
 * want a 90 degree field of view (left to right side of screen), then
 * set it to half of the screen's width: (xdim*.5).
 *
 * @param ipo camera position
 * @param ist camera's unit RIGHT vector
 * @param ihe camera's unit DOWN vector
 * @param ifo camera's unit FORWARD vector
 * @param dahx x-dimension of viewing window
 * @param dahy y-dimension of viewing window
 * @param dahz z-dimension of viewing window (should = dahx for 90 degree FOV)
 */

VOXLAP_DLL_FUNC void setcamera (dpoint3d *ipo, dpoint3d *ist, dpoint3d *ihe, dpoint3d *ifo,
					 float dahx, float dahy, float dahz)
{
	int i, j;

	gipos.x = ipo->x; gipos.y = ipo->y; gipos.z = ipo->z;
	gistr.x = ist->x; gistr.y = ist->y; gistr.z = ist->z;
	gihei.x = ihe->x; gihei.y = ihe->y; gihei.z = ihe->z;
	gifor.x = ifo->x; gifor.y = ifo->y; gifor.z = ifo->z;
	gihx = dahx; gihy = dahy; gihz = dahz;

	gixs.x = gistr.x; gixs.y = gihei.x; gixs.z = gifor.x;
	giys.x = gistr.y; giys.y = gihei.y; giys.z = gifor.y;
	gizs.x = gistr.z; gizs.y = gihei.z; gizs.z = gifor.z;
	giadd.x = -(gipos.x*gistr.x + gipos.y*gistr.y + gipos.z*gistr.z);
	giadd.y = -(gipos.x*gihei.x + gipos.y*gihei.y + gipos.z*gihei.z);
	giadd.z = -(gipos.x*gifor.x + gipos.y*gifor.y + gipos.z*gifor.z);

	gcorn[0].x = -gihx*gistr.x - gihy*gihei.x + gihz*gifor.x;
	gcorn[0].y = -gihx*gistr.y - gihy*gihei.y + gihz*gifor.y;
	gcorn[0].z = -gihx*gistr.z - gihy*gihei.z + gihz*gifor.z;
	gcorn[1].x = xres_voxlap*gistr.x+gcorn[0].x;
	gcorn[1].y = xres_voxlap*gistr.y+gcorn[0].y;
	gcorn[1].z = xres_voxlap*gistr.z+gcorn[0].z;
	gcorn[2].x = yres_voxlap*gihei.x+gcorn[1].x;
	gcorn[2].y = yres_voxlap*gihei.y+gcorn[1].y;
	gcorn[2].z = yres_voxlap*gihei.z+gcorn[1].z;
	gcorn[3].x = yres_voxlap*gihei.x+gcorn[0].x;
	gcorn[3].y = yres_voxlap*gihei.y+gcorn[0].y;
	gcorn[3].z = yres_voxlap*gihei.z+gcorn[0].z;
	for(j=0,i=3;j<4;i=j++)
	{
		ginor[i].x = gcorn[i].y*gcorn[j].z - gcorn[i].z*gcorn[j].y;
		ginor[i].y = gcorn[i].z*gcorn[j].x - gcorn[i].x*gcorn[j].z;
		ginor[i].z = gcorn[i].x*gcorn[j].y - gcorn[i].y*gcorn[j].x;
	}
}

/**
 * Render VXL screen (this is where it all happens!)
 * Make sure you have .VXL loaded in memory by using one of the loadnul(),
 * loadvxl(), loadbsp(), loaddta() functions.
 * Also make sure to call setcamera() and setvoxframebuffer() before this.
 */
void opticast (unsigned int screenx1, unsigned int screenx2, unsigned int screeny1, unsigned int screeny2, void **castradarbuf)
{
	double f, ff, cx, cy, fx, fy, gx, gy, x0, y0, x1, y1, x2, y2, x3, y3;
	int i, j, sx, sy, p0, p1, cx16, cy16, kadd, kmul, u, u1, ui;
	int wx0, wy0, wx1, wy1, iwx0, iwy0, iwx1, iwy1;
	castdat *angstart[MAXXDIM*4];
	castdat *gscanptr;
	int lastx[MAX(MAXYDIM,VSID)];
	unsigned int uurendmem[MAXXDIM*2+8], *uurend;
	void *castradarmem;
	const size_t castradarsize=MAX((((MAXXDIM*MAXYDIM*27)>>1)+7)&~7,(VSID+4)*3*SCPITCH*4+8);
	if((*castradarbuf)==NULL)
		*castradarbuf=malloc(castradarsize);
	castradarmem=*castradarbuf;
	void *castradar = (void *)((((size_t)castradarmem)+castradarsize/2+7)&~7);
	
	#if (__64BIT_SYSTEM__==0)
	uurend = (unsigned int*)&uurendmem[((((size_t)frameplace)&4)^(((int)uurendmem)&4))>>2];
	#else
	uurend = uurendmem;
	#endif

	if (gifor.z < 0) giforzsgn = -1; else giforzsgn = 1; //giforzsgn = (gifor.z < 0);

	gixyi[0] = (VSID<<2); gixyi[1] = -gixyi[0];
	glipos.x = ((int)gipos.x);
	glipos.y = ((int)gipos.y);
	glipos.z = ((int)gipos.z);
	gpixy = &sptr[glipos.y*VSID + glipos.x];
	ftol(gipos.z*PREC-.5f,&gposz);
	gposxfrac[1] = gipos.x - (float)glipos.x; gposxfrac[0] = 1-gposxfrac[1];
	gposyfrac[1] = gipos.y - (float)glipos.y; gposyfrac[0] = 1-gposyfrac[1];

/*#if (defined(USEV5ASM) && (USEV5ASM != 0)) //if true*/
	for(j=u=0;j<gmipnum;j++,u+=i)
		for(i=0;i<(256>>j)+4;i++)
			gylookup[i+u] = i*PREC-(gposz>>j);
			/*gylookup[i+u] = ((((gposz>>j)-i*PREC)>>(16-j))&0x0000ffff);*/
	gxmip = MAX(vx5.mipscandist,4)*PREC;
/*#else
	for(i=0;i<256+4;i++) gylookup[i] = (i*PREC-gposz);
#endif*/
	gmaxscandist = MIN(MAX(vx5.maxscandist,1),1023)*PREC;
// Selecting functions
#if (USEZBUFFER != 1)
	hrend = hrendnoz; vrend = vrendnoz;
#else
	hrend=fast_hrendz; vrend=vrendz;
#endif
// END Selecting functions

	if (ofogdist < 0) nskypic = skypic;

	gstartv = *gpixy;
	
	if (glipos.z >= gstartv[1])
	{
		do 
		{
			if (!*gstartv){return;}
			gstartv += *gstartv<<2;
		} while (glipos.z >= gstartv[1]);
		if (glipos.z < gstartv[3]) return;
		gstartz0 = gstartv[3];
	} else gstartz0 = 0;
	gstartz1 = gstartv[1];

	if (gifor.z == 0) f = 32000; else f = gihz/gifor.z;
	f = MIN(MAX(f,-32000),32000);
	cx = gistr.z*f + gihx;
	cy = gihei.z*f + gihy;

	//wx0 = (float)(-(vx5.anginc)); wx1 = (float)(screenx2-1+(vx5.anginc));
	wx0=-vx5.anginc+screenx1; wx1=screenx2-1+vx5.anginc;
	//wy0 = (float)(-(vx5.anginc)); wy1 = (float)(screeny2-1+(vx5.anginc));
	wy0=-vx5.anginc+screeny1; wy1=screeny2-1+vx5.anginc;
	ftol(wx0,&iwx0); ftol(wx1,&iwx1);
	ftol(wy0,&iwy0); ftol(wy1,&iwy1);

	fx = wx0-cx; fy = wy0-cy; gx = wx1-cx; gy = wy1-cy;
	x0 = x3 = wx0; y0 = y1 = wy0; x1 = x2 = wx1; y2 = y3 = wy1;
	if (fy < 0)
	{
		if (fx < 0) { f = sqrt(fx*fy); x0 = cx-f; y0 = cy-f; }
		if (gx > 0) { f = sqrt(-gx*fy); x1 = cx+f; y1 = cy-f; }
	}
	if (gy > 0)
	{
		if (gx > 0) { f = sqrt(gx*gy); x2 = cx+f; y2 = cy+f; }
		if (fx < 0) { f = sqrt(-fx*gy); x3 = cx-f; y3 = cy+f; }
	}
	if (x0 > x1) { if (fx < 0) y0 = fx/gx*fy + cy; else y1 = gx/fx*fy + cy; }
	if (y1 > y2) { if (fy < 0) x1 = fy/gy*gx + cx; else x2 = gy/fy*gx + cx; }
	if (x2 < x3) { if (fx < 0) y3 = fx/gx*gy + cy; else y2 = gx/fx*gy + cy; }
	if (y3 < y0) { if (fy < 0) x0 = fy/gy*fx + cx; else x3 = gy/fy*fx + cx; }
		/*This makes precision errors cause pixels to overwrite rather than omit (unneeded)*/
	/*x0 -= .01; x1 += .01;
	y1 -= .01; y2 += .01;
	x3 -= .01; x2 += .01;
	y0 -= .01; y3 += .01;*/
	
	f=1.0/gihz;

	optistrx = gistr.x*f; optiheix = gihei.x*f; optiaddx = gcorn[0].x*f;
	optistry = gistr.y*f; optiheiy = gihei.y*f; optiaddy = gcorn[0].y*f;
	
	f = (float)PREC / gihz;

	ftol(cx*65536,&cx16);
	ftol(cy*65536,&cy16);

	ftol((x1-x0)/vx5.anginc,&j);
	if ((fy < 0) && (j > 0)) //(cx,cy),(x0,wy0),(x1,wy0)
	{
		ff = (x1-x0) / (float)j; grd = 1.0f / (wy0-cy);
		gscanptr = (castdat *)castradar; skycurlng = -1; skycurdir = -giforzsgn;
		for(i=0,f=x0+ff*.5f;i<j;f+=ff,i++)
		{	
			vline(cx,cy,f,wy0,&p0,&p1, wx0, wy0, wx1, wy1, iwx0, iwy0, iwx1, iwy1, gscanptr);
			if (giforzsgn < 0) angstart[i] = gscanptr+p0; else angstart[i] = gscanptr-p1;
			gscanptr += labs(p1-p0)+1;
		}
		j <<= 16; f = (float)j / ((x1-x0)*grd); ftol((cx-x0)*grd*f,&kadd);
		ftol(cx-.5f,&p1); p0 = lbound0(p1+1,screenx2); p1 = lbound0(p1,screenx2);
		ftol(cy-0.50005f,&sy); if (sy >= screeny2) sy = screeny2-1;
		sy-=screeny1+1;
		ff = (fabs((float)p1-cx)+1)*f/2147483647.0 + cy; //Anti-crash hack
		while ((ff < sy) && (sy >= 0)) sy--;
		if (sy >= 0)
		{
			ftol(f,&kmul);
			for(;sy>=0;sy--) if (isshldiv16safe(kmul,(sy<<16)-cy16)) break; //Anti-crash hack
			if (giforzsgn < 0) i = -sy; else i = sy;
			for(;sy>=0;sy--,i-=giforzsgn)
			{
				ui = shldiv16(kmul,(sy<<16)-cy16);
				u = mulshr16((p0<<16)-cx16,ui)+kadd;
				while ((p0 > 0) && (u >= ui)) { u -= ui; p0--; }
				u1 = (p1-p0)*ui + u;
				while ((p1 < screenx2) && (u1 < j)) { u1 += ui; p1++; }
				if (p0 < p1) hrend(p0,sy+screeny1,p1,u,ui,i,angstart);
			}
		}
	}

	ftol((y2-y1)/vx5.anginc,&j);
	if ((gx > 0) && (j > 0)) //(cx,cy),(wx1,y1),(wx1,y2)
	{
		ff = (y2-y1) / (float)j; grd = 1.0f / (wx1-cx);
		gscanptr = (castdat *)castradar; skycurlng = -1; skycurdir = -giforzsgn;
		for(i=0,f=y1+ff*.5f;i<j;f+=ff,i++)
		{
			hline(cx,cy,wx1,f,&p0,&p1, wx0, wy0, wx1, wy1, iwx0, iwy0, iwx1, iwy1, gscanptr);
			if (giforzsgn < 0) angstart[i] = gscanptr-p0; else angstart[i] = gscanptr+p1;
			gscanptr += labs(p1-p0)+1;
		}

		j <<= 16; f = (float)j / ((y2-y1)*grd); ftol((cy-y1)*grd*f,&kadd);
		ftol(cy-.5f,&p1); p0 = lbound0(p1+1,screeny2); p1 = lbound0(p1,screeny2);
		ftol(cx+0.50005f,&sx); if (sx < 0) sx = 0;
		ff = (fabs((float)p1-cy)+1)*f/2147483647.0 + cx; //Anti-crash hack
		while ((ff > sx) && (sx < screenx2)) sx++;
		if (sx < screenx2)
		{
			ftol(f,&kmul);
			for(;sx<screenx2;sx++) if (isshldiv16safe(kmul,(sx<<16)-cx16)) break; //Anti-crash hack
			for(;sx<screenx2;sx++)
			{
				ui = shldiv16(kmul,(sx<<16)-cx16);
				u = mulshr16((p0<<16)-cy16,ui)+kadd;
				while ((p0 > 0) && (u >= ui)) { u -= ui; lastx[--p0] = sx; }
				uurend[sx] = u; uurend[sx+MAXXDIM] = ui; u += (p1-p0)*ui;
				while ((p1 < screeny2) && (u < j)) { u += ui; lastx[p1++] = sx; }
			}
			if (giforzsgn < 0)
				  { for(sy=p0;sy<p1;sy++) vrend(lastx[sy],sy,screenx2,lastx[sy],1, angstart, uurend); }
			else { for(sy=p0;sy<p1;sy++) vrend(lastx[sy],sy,screenx2,-lastx[sy],-1, angstart, uurend); }
		}
	}

	ftol((x2-x3)/vx5.anginc,&j);
	if ((gy > 0) && (j > 0)) //(cx,cy),(x2,wy1),(x3,wy1)
	{
		ff = (x2-x3) / (float)j; grd = 1.0f / (wy1-cy);
		gscanptr = (castdat *)castradar; skycurlng = -1; skycurdir = giforzsgn;
		for(i=0,f=x3+ff*.5f;i<j;f+=ff,i++)
		{
			vline(cx,cy,f,wy1,&p0,&p1, wx0, wy0, wx1, wy1, iwx0, iwy0, iwx1, iwy1, gscanptr);
			if (giforzsgn < 0) angstart[i] = gscanptr-p0; else angstart[i] = gscanptr+p1;
			gscanptr += labs(p1-p0)+1;
		}

		j <<= 16; f = (float)j / ((x2-x3)*grd); ftol((cx-x3)*grd*f,&kadd);
		ftol(cx-.5f,&p1); p0 = lbound0(p1+1,screenx2); p1 = lbound0(p1,screenx2);
		ftol(cy+0.50005f,&sy); if (sy < 0) sy = 0;
		ff = (fabs((float)p1-cx)+1)*f/2147483647.0 + cy; //Anti-crash hack
		while ((ff > sy) && (sy < screeny2)) sy++;
		if (sy < screeny2)
		{
			ftol(f,&kmul);
			for(;sy<screeny2;sy++) if (isshldiv16safe(kmul,(sy<<16)-cy16)) break; //Anti-crash hack
			if (giforzsgn < 0) i = sy; else i = -sy;
			for(;sy<screeny2;sy++,i-=giforzsgn)
			{
				ui = shldiv16(kmul,(sy<<16)-cy16);
				u = mulshr16((p0<<16)-cx16,ui)+kadd;
				while ((p0 > 0) && (u >= ui)) { u -= ui; p0--; }
				u1 = (p1-p0)*ui + u;
				while ((p1 < screenx2) && (u1 < j)) { u1 += ui; p1++; }
				if (p0 < p1) hrend(p0,sy,p1,u,ui,i,angstart);
			}
		}
	}

	ftol((y3-y0)/vx5.anginc,&j);
	if ((fx < 0) && (j > 0)) //(cx,cy),(wx0,y3),(wx0,y0)
	{
		ff = (y3-y0) / (float)j; grd = 1.0f / (wx0-cx);
		gscanptr = (castdat *)castradar; skycurlng = -1; skycurdir = giforzsgn;
		for(i=0,f=y0+ff*.5f;i<j;f+=ff,i++)
		{
			hline(cx,cy,wx0,f,&p0,&p1, wx0, wy0, wx1, wy1, iwx0, iwy0, iwx1, iwy1, gscanptr);
			if (giforzsgn < 0) angstart[i] = gscanptr+p0; else angstart[i] = gscanptr-p1;
			gscanptr += labs(p1-p0)+1;
		}

		j <<= 16; f = (float)j / ((y3-y0)*grd); ftol((cy-y0)*grd*f,&kadd);
		ftol(cy-.5f,&p1); p0 = lbound0(p1+1,screeny2); p1 = lbound0(p1,screeny2);
		ftol(cx-0.50005f,&sx); if (sx >= screenx2) sx = screenx2-1;
		ff = (fabs((float)p1-cy)+1)*f/2147483647.0 + cx; //Anti-crash hack
		while ((ff < sx) && (sx >= 0)) sx--;
		if (sx >= 0)
		{
			ftol(f,&kmul);
			for(;sx>=0;sx--) if (isshldiv16safe(kmul,(sx<<16)-cx16)) break; //Anti-crash hack
			for(;sx>=0;sx--)
			{
				ui = shldiv16(kmul,(sx<<16)-cx16);
				u = mulshr16((p0<<16)-cy16,ui)+kadd;
				while ((p0 > 0) && (u >= ui)) { u -= ui; lastx[--p0] = sx; }
				uurend[sx] = u; uurend[sx+MAXXDIM] = ui; u += (p1-p0)*ui;
				while ((p1 < screeny2) && (u < j)) { u += ui; lastx[p1++] = sx; }
			}
			for(sy=p0;sy<p1;sy++) vrend(0,sy,lastx[sy]+1,0,giforzsgn,angstart,uurend);
		}
	}
}

void Vox_SetSideShades(unsigned char low_x, unsigned char low_y, 
unsigned char high_x, unsigned char high_y, unsigned char b, unsigned char t){
	HorizontalCubeShades[0]=low_x; HorizontalCubeShades[1]=high_x;
	HorizontalCubeShades[2]=low_y; HorizontalCubeShades[3]=high_y;
	CubeShadeBottom=b; CubeShadeTop=t;
}

	//0: asm temp for current x
	//1: asm temp for current y
	//2: bottom (28)
	//3: top    ( 0)
	//4: left   ( 8)
	//5: right  (24)
	//6: up     (12)
	//7: down   (12)
	//setsideshades(0,0,0,0,0,0);
	//setsideshades(0,28,8,24,12,12);
/**
 * Shade offset for each face of the cube: useful for editing
 * @param sto top face (z minimum) shade offset
 * @param sbo bottom face (z maximum) shade offset
 * @param sle left face (x minimum) shade offset
 * @param sri right face (x maximum) shade offset
 * @param sup top face (y minimum) shade offset
 * @param sdo bottom face (y maximum) shade offset
 */
void setsideshades (char sto, char sbo, char sle, char sri, char sup, char sdo)
{
	((char *)&gcsub[2])[7] = sbo; ((char *)&gcsub[3])[7] = sto;
	((char *)&gcsub[4])[7] = sle; ((char *)&gcsub[5])[7] = sri;
	((char *)&gcsub[6])[7] = sup; ((char *)&gcsub[7])[7] = sdo;
	if (!(sto|sbo|sle|sri|sup|sdo))
	{
		vx5.sideshademode = 0;
		((char *)&gcsub[0])[7] = ((char *)&gcsub[1])[7] = 0x00;
	}
	else vx5.sideshademode = 1;
}

	//MUST have more than: CEILING(max possible CLIPRADIUS) * 4 entries!
#define MAXCLIPIT (VSID*4) //VSID*2+4 is not a power of 2!
static lpoint2d clipit[MAXCLIPIT];

double findmaxcr (double px, double py, double pz, double cr)
{
	double f, g, maxcr, thresh2;
	int x, y, z, i0, i1, ix, y0, y1, z0, z1;
	char *v;

	thresh2 = cr+1.7321+1; thresh2 *= thresh2;
	maxcr = cr*cr;

		//Find closest point of all nearby cubes to (px,py,pz)
	x = (int)px; y = (int)py; z = (int)pz; i0 = i1 = 0; ix = x; y0 = y1 = y;
	while (1)
	{
		f = MAX(fabs((double)x+.5-px)-.5,0);
		g = MAX(fabs((double)y+.5-py)-.5,0);
		f = f*f + g*g;
		if (f < maxcr)
		{
			if (((unsigned int)x >= VSID) || ((unsigned int)y >= VSID))
				{ z0 = z1 = 0; }
			else
			{
				v = sptr[y*VSID+x];
				if (z >= v[1])
				{
					while (1)
					{
						if (!v[0]) { z0 = z1 = 0; break; }
						v += v[0]*4;
						if (z < v[1]) { z0 = v[3]; z1 = v[1]; break; }
					}
				}
				else { z0 = MAXZDIM-2048; z1 = v[1]; }
			}

			if ((pz <= z0) || (pz >= z1))
				maxcr = f;
			else
			{
				g = MIN(pz-(double)z0,(double)z1-pz);
				f += g*g; if (f < maxcr) maxcr = f;
			}
		}

		if ((x-px)*(x-px)+(y-py)*(y-py) < thresh2)
		{
			if ((x <= ix) && (x > 0))
				{ clipit[i1].x = x-1; clipit[i1].y = y; i1 = ((i1+1)&(MAXCLIPIT-1)); }
			if ((x >= ix) && (x < VSID-1))
				{ clipit[i1].x = x+1; clipit[i1].y = y; i1 = ((i1+1)&(MAXCLIPIT-1)); }
			if ((y <= y0) && (y > 0))
				{ clipit[i1].x = x; clipit[i1].y = y-1; i1 = ((i1+1)&(MAXCLIPIT-1)); y0--; }
			if ((y >= y1) && (y < VSID-1))
				{ clipit[i1].x = x; clipit[i1].y = y+1; i1 = ((i1+1)&(MAXCLIPIT-1)); y1++; }
		}
		if (i0 == i1) break;
		x = clipit[i0].x; y = clipit[i0].y; i0 = ((i0+1)&(MAXCLIPIT-1));
	}
	return(sqrt(maxcr));
}

#if 0

	//Point: (x,y), line segment: (px,py)-(px+vx,py+vy)
	//Returns 1 if point is closer than sqrt(cr2) to line
int dist2linept2d (double x, double y, double px, double py, double vx, double vy, double cr2)
{
	double f, g;
	x -= px; y -= py; f = x*vx + y*vy; if (f <= 0) return(x*x + y*y <= cr2);
	g = vx*vx + vy*vy; if (f >= g) { x -= vx; y -= vy; return(x*x + y*y <= cr2); }
	x = x*g-vx*f; y = y*g-vy*f; return(x*x + y*y <= cr2*g*g);
}

static char clipbuf[MAXZDIM+16]; //(8 extra on each side)
int sphtraceo (double px, double py, double pz,    //start pt
					double vx, double vy, double vz,    //move vector
					double *nx, double *ny, double *nz, //new pt after collision
					double *fx, double *fy, double *fz, //pt that caused collision
					double cr, double acr)
{
	double t, u, ex, ey, ez, Za, Zb, Zc, thresh2;
	double vxyz, vyz, vxz, vxy, rvxyz, rvyz, rvxz, rvxy, rvx, rvy, rvz, cr2;
	int i, i0, i1, x, y, z, xx, yy, zz, v, vv, ix, y0, y1, z0, z1;
	char *vp;

	t = 1;
	(*nx) = px + vx;
	(*ny) = py + vy;
	(*nz) = pz + vz;

	z0 = MAX((int)(MIN(pz,*nz)-cr)-2,-1);
	z1 = MIN((int)(MAX(pz,*nz)+cr)+2,MAXZDIM);

	thresh2 = cr+1.7321+1; thresh2 *= thresh2;

	vyz = vz*vz; vxz = vx*vx; vxy = vy*vy; cr2 = cr*cr;
	vyz += vxy; vxy += vxz; vxyz = vyz + vxz; vxz += vz*vz;
	rvx = 1.0 / vx; rvy = 1.0 / vy; rvz = 1.0 / vz;
	rvyz = 1.0 / vyz; rvxz = 1.0 / vxz; rvxy = 1.0 / vxy;
	rvxyz = 1.0 / vxyz;

		//Algorithm fails (stops short) if cr < 2 :(
	i0 = i1 = 0; ix = x = (int)px; y = y0 = y1 = (int)py;
	while (1)
	{
		for(z=z0;z<=z1;z++) clipbuf[z+8] = 0;
		i = 16;
		for(yy=y;yy<y+2;yy++)
			for(xx=x;xx<x+2;xx++,i<<=1)
			{
				z = z0;
				if ((unsigned int)(xx|yy) < VSID)
				{
					vp = sptr[yy*VSID+xx];
					while (1)
					{
						if (vp[1] > z) z = vp[1];
						if (!vp[0]) break;
						vp += vp[0]*4;
						zz = vp[3]; if (zz > z1) zz = z1;
						while (z < zz) clipbuf[(z++)+8] |= i;
					}
				}
				while (z <= z1) clipbuf[(z++)+8] |= i;
			}

		xx = x+1; yy = y+1; v = clipbuf[z0+8];
		for(z=z0;z<z1;z++)
		{
			zz = z+1; v = (v>>4)|clipbuf[zz+8];
			if ((!v) || (v == 255)) continue;

//---------------Check 1(8) corners of cube (sphere intersection)-------------

			//if (((v-1)^v) >= v)  //True if v is: {1,2,4,8,16,32,64,128}
			if (!(v&(v-1)))      //Same as above, but {0,1,2,4,...} (v's never 0)
			{
				ex = xx-px; ey = yy-py; ez = zz-pz;
				Zb = ex*vx + ey*vy + ez*vz;
				Zc = ex*ex + ey*ey + ez*ez - cr2;
				u = Zb*Zb - vxyz*Zc;
				if ((((int *)&u)[1] | ((int *)&Zb)[1]) >= 0)
				//if ((u >= 0) && (Zb >= 0))
				{
						//   //Proposed compare optimization:
						//f = Zb*Zb-u; g = vxyz*t; h = (Zb*2-g)*g;
						//if ((uint64_t *)&f < (uint64_t *)&h)
					u = (Zb - sqrt(u)) * rvxyz;
					if ((u >= 0) && (u < t))
					{
						*fx = xx; *fy = yy; *fz = zz; t = u;
						*nx = vx*u + px; *ny = vy*u + py; *nz = vz*u + pz;
					}
				}
			}

//---------------Check 3(12) edges of cube (cylinder intersection)-----------

			vv = v&0x55; if (((vv-1)^vv) >= vv)  //True if (v&0x55)={1,4,16,64}
			{
				ey = yy-py; ez = zz-pz;
				Zb = ey*vy + ez*vz;
				Zc = ey*ey + ez*ez - cr2;
				u = Zb*Zb - vyz*Zc;
				if ((((int *)&u)[1] | ((int *)&Zb)[1]) >= 0)
				//if ((u >= 0) && (Zb >= 0))
				{
					u = (Zb - sqrt(u)) * rvyz;
					if ((u >= 0) && (u < t))
					{
						ex = vx*u + px;
						if ((ex >= x) && (ex <= xx))
						{
							*fx = ex; *fy = yy; *fz = zz; t = u;
							*nx = ex; *ny = vy*u + py; *nz = vz*u + pz;
						}
					}
				}
			}
			vv = v&0x33; if (((vv-1)^vv) >= vv) //True if (v&0x33)={1,2,16,32}
			{
				ex = xx-px; ez = zz-pz;
				Zb = ex*vx + ez*vz;
				Zc = ex*ex + ez*ez - cr2;
				u = Zb*Zb - vxz*Zc;
				if ((((int *)&u)[1] | ((int *)&Zb)[1]) >= 0)
				//if ((u >= 0) && (Zb >= 0))
				{
					u = (Zb - sqrt(u)) * rvxz;
					if ((u >= 0) && (u < t))
					{
						ey = vy*u + py;
						if ((ey >= y) && (ey <= yy))
						{
							*fx = xx; *fy = ey; *fz = zz; t = u;
							*nx = vx*u + px; *ny = ey; *nz = vz*u + pz;
						}
					}
				}
			}
			vv = v&0x0f; if (((vv-1)^vv) >= vv) //True if (v&0x0f)={1,2,4,8}
			{
				ex = xx-px; ey = yy-py;
				Zb = ex*vx + ey*vy;
				Zc = ex*ex + ey*ey - cr2;
				u = Zb*Zb - vxy*Zc;
				if ((((int *)&u)[1] | ((int *)&Zb)[1]) >= 0)
				//if ((u >= 0) && (Zb >= 0))
				{
					u = (Zb - sqrt(u)) * rvxy;
					if ((u >= 0) && (u < t))
					{
						ez = vz*u + pz;
						if ((ez >= z) && (ez <= zz))
						{
							*fx = xx; *fy = yy; *fz = ez; t = u;
							*nx = vx*u + px; *ny = vy*u + py; *nz = ez;
						}
					}
				}
			}

//---------------Check 3(6) faces of cube (plane intersection)---------------

			if (vx)
			{
				switch(v&0x03)
				{
					case 0x01: ex = xx+cr; if ((vx > 0) || (px < ex)) goto skipfacex; break;
					case 0x02: ex = xx-cr; if ((vx < 0) || (px > ex)) goto skipfacex; break;
					default: goto skipfacex;
				}
				u = (ex - px) * rvx;
				if ((u >= 0) && (u < t))
				{
					ey = vy*u + py;
					ez = vz*u + pz;
					if ((ey >= y) && (ey <= yy) && (ez >= z) && (ez <= zz))
					{
						*fx = xx; *fy = ey; *fz = ez; t = u;
						*nx = ex; *ny = ey; *nz = ez;
					}
				}
			}
skipfacex:;
			if (vy)
			{
				switch(v&0x05)
				{
					case 0x01: ey = yy+cr; if ((vy > 0) || (py < ey)) goto skipfacey; break;
					case 0x04: ey = yy-cr; if ((vy < 0) || (py > ey)) goto skipfacey; break;
					default: goto skipfacey;
				}
				u = (ey - py) * rvy;
				if ((u >= 0) && (u < t))
				{
					ex = vx*u + px;
					ez = vz*u + pz;
					if ((ex >= x) && (ex <= xx) && (ez >= z) && (ez <= zz))
					{
						*fx = ex; *fy = yy; *fz = ez; t = u;
						*nx = ex; *ny = ey; *nz = ez;
					}
				}
			}
skipfacey:;
			if (vz)
			{
				switch(v&0x11)
				{
					case 0x01: ez = zz+cr; if ((vz > 0) || (pz < ez)) goto skipfacez; break;
					case 0x10: ez = zz-cr; if ((vz < 0) || (pz > ez)) goto skipfacez; break;
					default: goto skipfacez;
				}
				u = (ez - pz) * rvz;
				if ((u >= 0) && (u < t))
				{
					ex = vx*u + px;
					ey = vy*u + py;
					if ((ex >= x) && (ex <= xx) && (ey >= y) && (ey <= yy))
					{
						*fx = ex; *fy = ey; *fz = zz; t = u;
						*nx = ex; *ny = ey; *nz = ez;
					}
				}
			}
skipfacez:;
		}

		if ((x <= ix) && (x > 0) && (dist2linept2d(x-1,y,px,py,vx,vy,thresh2)))
			{ clipit[i1].x = x-1; clipit[i1].y = y; i1 = ((i1+1)&(MAXCLIPIT-1)); }
		if ((x >= ix) && (x < VSID-1) && (dist2linept2d(x+1,y,px,py,vx,vy,thresh2)))
			{ clipit[i1].x = x+1; clipit[i1].y = y; i1 = ((i1+1)&(MAXCLIPIT-1)); }
		if ((y <= y0) && (y > 0) && (dist2linept2d(x,y-1,px,py,vx,vy,thresh2)))
			{ clipit[i1].x = x; clipit[i1].y = y-1; i1 = ((i1+1)&(MAXCLIPIT-1)); y0--; }
		if ((y >= y1) && (y < VSID-1) && (dist2linept2d(x,y+1,px,py,vx,vy,thresh2)))
			{ clipit[i1].x = x; clipit[i1].y = y+1; i1 = ((i1+1)&(MAXCLIPIT-1)); y1++; }
		if (i0 == i1) break;
		x = clipit[i0].x; y = clipit[i0].y; i0 = ((i0+1)&(MAXCLIPIT-1));
	}

	if ((*nx) < acr) (*nx) = acr;
	if ((*ny) < acr) (*ny) = acr;
	if ((*nx) > VSID-acr) (*nx) = VSID-acr;
	if ((*ny) > VSID-acr) (*ny) = VSID-acr;
	if ((*nz) > MAXZDIM-1-acr) (*nz) = MAXZDIM-1-acr;
	if ((*nz) < MAXZDIM-2048) (*nz) = MAXZDIM-2048;

	return (t == 1);
}

#endif

static double gx0, gy0, gcrf2, grdst, gendt, gux, guy;
static int gdist2square (double x, double y)
{
	double t;
	x -= gx0; y -= gy0; t = x*gux + y*guy; if (t <= 0) t = gcrf2;
	else if (t*grdst >= gendt) { x -= gux*gendt; y -= guy*gendt; t = gcrf2; }
	else t = t*t*grdst + gcrf2;
	return(x*x + y*y <= t);
}

int sphtrace (double x0, double y0, double z0,          //start pt
					double vx, double vy, double vz,          //move vector
					double *hitx, double *hity, double *hitz, //new pt after collision
					double *clpx, double *clpy, double *clpz, //pt causing collision
					double cr, double acr)
{
	double f, t, dax, day, daz, vyx, vxy, vxz, vyz, rvz, cr2, fz, fc;
	double dx, dy, dx1, dy1;
	double nx, ny, intx, inty, intz, dxy, dxz, dyz, dxyz, rxy, rxz, ryz, rxyz;
	int i, j, x, y, ix, iy0, iy1, i0, i1, iz[2], cz0, cz1;
	char *v;

		 //Precalculate global constants for ins & getval functions
	if ((vx == 0) && (vy == 0) && (vz == 0))
		{ (*hitx) = x0; (*hity) = y0; (*hitz) = z0; return(1); }
	gux = vx; guy = vy; gx0 = x0; gy0 = y0; dxy = vx*vx + vy*vy;
	if (dxy != 0) rxy = 1.0 / dxy; else rxy = 0;
	grdst = rxy; gendt = 1; cr2 = cr*cr; t = cr + 0.7072; gcrf2 = t*t;

	if (((int *)&vz)[1] >= 0) { dtol(   z0-cr-.5,&cz0); dtol(vz+z0+cr-.5,&cz1); }
								 else { dtol(vz+z0-cr-.5,&cz0); dtol(   z0+cr-.5,&cz1); }

		//Precalculate stuff for closest point on cube finder
	dax = 0; day = 0; vyx = 0; vxy = 0; rvz = 0; vxz = 0; vyz = 0;
	if (vx != 0) { vyx = vy/vx; if (((int *)&vx)[1] >= 0) dax = x0+cr; else dax = x0-cr-1; }
	if (vy != 0) { vxy = vx/vy; if (((int *)&vy)[1] >= 0) day = y0+cr; else day = y0-cr-1; }
	if (vz != 0)
	{
		rvz = 1.0/vz; vxz = vx*rvz; vyz = vy*rvz;
		if (((int *)&vz)[1] >= 0) daz = z0+cr; else daz = z0-cr;
	}

	dxyz = vz*vz;
	dxz = vx*vx+dxyz; if (dxz != 0) rxz = 1.0 / dxz;
	dyz = vy*vy+dxyz; if (dyz != 0) ryz = 1.0 / dyz;
	dxyz += dxy; rxyz = 1.0 / dxyz;

	dtol(x0-.5,&x); dtol(y0-.5,&y);
	ix = x; iy0 = iy1 = y;
	i0 = 0; clipit[0].x = x; clipit[0].y = y; i1 = 1;
	do
	{
		x = clipit[i0].x; y = clipit[i0].y; i0 = ((i0+1)&(MAXCLIPIT-1));

		dx = (double)x; dx1 = (double)(x+1);
		dy = (double)y; dy1 = (double)(y+1);

			//closest point on cube finder
			//Plane intersection (both vertical planes)
#if 0
		intx = dbound((dy-day)*vxy + x0,dx,dx1);
		inty = dbound((dx-dax)*vyx + y0,dy,dy1);
#else
		intx = (dy-day)*vxy + x0;
		inty = (dx-dax)*vyx + y0;
		if (((int *)&intx)[1] < ((int *)&dx)[1]) intx = dx;
		if (((int *)&inty)[1] < ((int *)&dy)[1]) inty = dy;
		if (((int *)&intx)[1] >= ((int *)&dx1)[1]) intx = dx1;
		if (((int *)&inty)[1] >= ((int *)&dy1)[1]) inty = dy1;
		//if (intx < (double)x) intx = (double)x;
		//if (inty < (double)y) inty = (double)y;
		//if (intx > (double)(x+1)) intx = (double)(x+1);
		//if (inty > (double)(y+1)) inty = (double)(y+1);
#endif

		do
		{
			if (((int *)&dxy)[1] == 0) { t = -1.0; continue; }
			nx = intx-x0; ny = inty-y0; t = vx*nx + vy*ny; if (((int *)&t)[1] < 0) continue;
			f = cr2 - nx*nx - ny*ny; if (((int *)&f)[1] >= 0) { t = -1.0; continue; }
			f = f*dxy + t*t; if (((int *)&f)[1] < 0) { t = -1.0; continue; }
			t = (t-sqrt(f))*rxy;
		} while (0);
		if (t >= gendt) goto sphtracecont;
		if (((int *)&t)[1] < 0) intz = z0; else intz = vz*t + z0;

			//Find closest ceil(iz[0]) & flor(iz[1]) in (x,y) column
		dtol(intz-.5,&i);
		if ((unsigned int)(x|y) < VSID)
		{
			v = sptr[y*VSID+x]; iz[0] = MAXZDIM-2048; iz[1] = v[1];
			while (i >= iz[1])
			{
				if (!v[0]) { iz[1] = -1; break; }
				v += v[0]*4;
				iz[0] = v[3]; if (i < iz[0]) { iz[1] = -1; break; }
				iz[1] = v[1];
			}
		}
		else iz[1] = -1;

			//hit xz plane, yz plane or z-axis edge?
		if (iz[1] < 0) //Treat whole column as solid
		{
			if (((int *)&t)[1] >= 0) { gendt = t; (*clpx) = intx; (*clpy) = inty; (*clpz) = intz; goto sphtracecont; }
		}

			//Must check tops & bottoms of slab
		for(i=1;i>=0;i--)
		{
				//Ceil/flor outside of quick&dirty bounding box
			if ((iz[i] < cz0) || (iz[i] > cz1)) continue;

				//Plane intersection (parallel to ground)
			intz = (double)iz[i]; t = intz-daz;
			intx = t*vxz + x0;
			inty = t*vyz + y0;

			j = 0;                         // A ³ 8 ³ 9
			//     if (intx < dx)  j |= 2; //ÄÄÄÅÄÄÄÅÄÄÄ
			//else if (intx > dx1) j |= 1; // 2 ³ 0 ³ 1
			//     if (inty < dy)  j |= 8; //ÄÄÄÅÄÄÄÅÄÄÄ
			//else if (inty > dy1) j |= 4; // 6 ³ 4 ³ 5
				  if (((int *)&intx)[1] <  ((int *)&dx)[1])  j |= 2;
			else if (((int *)&intx)[1] >= ((int *)&dx1)[1]) j |= 1;
				  if (((int *)&inty)[1] <  ((int *)&dy)[1])  j |= 8;
			else if (((int *)&inty)[1] >= ((int *)&dy1)[1]) j |= 4;

				//NOTE: only need to check once per "for"!
			if ((!j) && (vz != 0)) //hit xy plane?
			{
				t *= rvz;
				if ((((int *)&t)[1] >= 0) && (t < gendt)) { gendt = t; (*clpx) = intx; (*clpy) = inty; (*clpz) = intz; }
				continue;
			}

				//common calculations used for rest of checks...
			fz = intz-z0; fc = cr2-fz*fz; fz *= vz;

			if (j&3)
			{
				nx = (double)((j&1)+x);
				if (((int *)&dxz)[1] != 0) //hit y-axis edge?
				{
					f = nx-x0; t = vx*f + fz; f = (fc - f*f)*dxz + t*t;
					if (((int *)&f)[1] >= 0) t = (t-sqrt(f))*rxz; else t = -1.0;
				} else t = -1.0;
				ny = vy*t + y0;
					  if (((int *)&ny)[1] > ((int *)&dy1)[1]) j |= 0x10;
				else if (((int *)&ny)[1] >= ((int *)&dy)[1])
				{
					if ((((int *)&t)[1] >= 0) && (t < gendt)) { gendt = t; (*clpx) = nx; (*clpy) = ny; (*clpz) = intz; }
					continue;
				}
				inty = (double)(((j>>4)&1)+y);
			}
			else inty = (double)(((j>>2)&1)+y);

			if (j&12)
			{
				ny = (double)(((j>>2)&1)+y);
				if (((int *)&dyz)[1] != 0) //hit x-axis edge?
				{
					f = ny-y0; t = vy*f + fz; f = (fc - f*f)*dyz + t*t;
					if (((int *)&f)[1] >= 0) t = (t-sqrt(f))*ryz; else t = -1.0;
				} else t = -1.0;
				nx = vx*t + x0;
					  if (((int *)&nx)[1] > ((int *)&dx1)[1]) j |= 0x20;
				else if (((int *)&nx)[1] >= ((int *)&dx)[1])
				{
					if ((((int *)&t)[1] >= 0) && (t < gendt)) { gendt = t; (*clpx) = nx; (*clpy) = ny; (*clpz) = intz; }
					continue;
				}
				intx = (double)(((j>>5)&1)+x);
			}
			else intx = (double)((j&1)+x);

				//hit corner?
			nx = intx-x0; ny = inty-y0;
			t = vx*nx + vy*ny + fz; if (((int *)&t)[1] < 0) continue;
			f = fc - nx*nx - ny*ny; if (((int *)&f)[1] >= 0) continue;
			f = f*dxyz + t*t; if (((int *)&f)[1] < 0) continue;
			t = (t-sqrt(f))*rxyz;
			if (t < gendt) { gendt = t; (*clpx) = intx; (*clpy) = inty; (*clpz) = intz; }
		}
sphtracecont:;
		if ((x <= ix)  && (x >      0) && (gdist2square(dx- .5,dy+ .5))) { clipit[i1].x = x-1; clipit[i1].y = y; i1 = ((i1+1)&(MAXCLIPIT-1)); }
		if ((x >= ix)  && (x < VSID-1) && (gdist2square(dx+1.5,dy+ .5))) { clipit[i1].x = x+1; clipit[i1].y = y; i1 = ((i1+1)&(MAXCLIPIT-1)); }
		if ((y <= iy0) && (y >      0) && (gdist2square(dx+ .5,dy- .5))) { clipit[i1].x = x; clipit[i1].y = y-1; i1 = ((i1+1)&(MAXCLIPIT-1)); iy0 = y-1; }
		if ((y >= iy1) && (y < VSID-1) && (gdist2square(dx+ .5,dy+1.5))) { clipit[i1].x = x; clipit[i1].y = y+1; i1 = ((i1+1)&(MAXCLIPIT-1)); iy1 = y+1; }
	} while (i0 != i1);
#if 1
	(*hitx) = dbound(vx*gendt + x0,acr,VSID-acr);
	(*hity) = dbound(vy*gendt + y0,acr,VSID-acr);
	(*hitz) = dbound(vz*gendt + z0,MAXZDIM-2048+acr,MAXZDIM-1-acr);
#else
	(*hitx) = MIN(MAX(vx*gendt + x0,acr),VSID-acr);
	(*hity) = MIN(MAX(vy*gendt + y0,acr),VSID-acr);
	(*hitz) = MIN(MAX(vz*gendt + z0,MAXZDIM-2048+acr),MAXZDIM-1-acr);
#endif
	return(gendt == 1);
}

void clipmove (dpoint3d *p, dpoint3d *v, double acr)
{
	double f, gx, gy, gz, nx, ny, nz, ex, ey, ez, hitx, hity, hitz, cr;
	//double nx2, ny2, nz2, ex2, ey2, ez2; //double ox, oy, oz;
	int i, j, k;

	//ox = p->x; oy = p->y; oz = p->z;
	gx = p->x+v->x; gy = p->y+v->y; gz = p->z+v->z;

	cr = findmaxcr(p->x,p->y,p->z,acr);
	vx5.clipmaxcr = cr;

	vx5.cliphitnum = 0;
	for(i=0;i<3;i++)
	{
		if ((v->x == 0) && (v->y == 0) && (v->z == 0)) break;

		cr -= 1e-7;  //Shrinking radius error control hack

		//j = sphtraceo(p->x,p->y,p->z,v->x,v->y,v->z,&nx,&ny,&nz,&ex,&ey,&ez,cr,acr);
		//k = sphtraceo(p->x,p->y,p->z,v->x,v->y,v->z,&nx2,&ny2,&nz2,&ex2,&ey2,&ez2,cr,acr);

		j = sphtrace(p->x,p->y,p->z,v->x,v->y,v->z,&nx,&ny,&nz,&ex,&ey,&ez,cr,acr);

		//if ((j != k) || (fabs(nx-nx2) > .000001) || (fabs(ny-ny2) > .000001) || (fabs(nz-nz2) > .000001) ||
		//   ((j == 0) && ((fabs(ex-ex2) > .000001) || (fabs(ey-ey2) > .000001) || (fabs(ez-ez2) > .000001))))
		//{
		//   printf("%d %f %f %f %f %f %f\n",i,p->x,p->y,p->z,v->x,v->y,v->z);
		//   printf("%f %f %f ",nx,ny,nz); if (!j) printf("%f %f %f\n",ex,ey,ez); else printf("\n");
		//   printf("%f %f %f ",nx2,ny2,nz2); if (!k) printf("%f %f %f\n",ex2,ey2,ez2); else printf("\n");
		//   printf("\n");
		//}
		if (j) { p->x = nx; p->y = ny; p->z = nz; break; }

		vx5.cliphit[i].x = ex; vx5.cliphit[i].y = ey; vx5.cliphit[i].z = ez;
		vx5.cliphitnum = i+1;
		p->x = nx; p->y = ny; p->z = nz;

			//Calculate slide vector
		v->x = gx-nx; v->y = gy-ny; v->z = gz-nz;
		switch(i)
		{
			case 0:
				hitx = ex-nx; hity = ey-ny; hitz = ez-nz;
				f = (v->x*hitx + v->y*hity + v->z*hitz) / (cr * cr);
				v->x -= hitx*f; v->y -= hity*f; v->z -= hitz*f;
				break;
			case 1:
				nx -= ex; ny -= ey; nz -= ez;
				ex = hitz*ny - hity*nz;
				ey = hitx*nz - hitz*nx;
				ez = hity*nx - hitx*ny;
				f = ex*ex + ey*ey + ez*ez; if (f <= 0) break;
				f = (v->x*ex + v->y*ey + v->z*ez) / f;
				v->x = ex*f; v->y = ey*f; v->z = ez*f;
				break;
			default: break;
		}
	}

		//If you didn't move much, then don't move at all. This helps prevents
		//cliprad from shrinking, but you get stuck too much :(
	//if ((p->x-ox)*(p->x-ox) + (p->y-oy)*(p->y-oy) + (p->z-oz)*(p->z-oz) < 1e-12)
	//   { p->x = ox; p->y = oy; p->z = oz; }
}

int cansee (point3d *p0, point3d *p1, lpoint3d *hit)
{
	lpoint3d a, c, d, p, i;
	point3d f, g;
	int cnt;

	ftol(p0->x-.5,&a.x); ftol(p0->y-.5,&a.y); ftol(p0->z-.5,&a.z);
	if (isvoxelsolid(a.x,a.y,a.z)) { hit->x = a.x; hit->y = a.y; hit->z = a.z; return(0); }
	ftol(p1->x-.5,&c.x); ftol(p1->y-.5,&c.y); ftol(p1->z-.5,&c.z);
	cnt = 0;

		  if (c.x <  a.x) { d.x = -1; f.x = p0->x-a.x;   g.x = (p0->x-p1->x)*1024; cnt += a.x-c.x; }
	else if (c.x != a.x) { d.x =  1; f.x = a.x+1-p0->x; g.x = (p1->x-p0->x)*1024; cnt += c.x-a.x; }
	else f.x = g.x = 0;
		  if (c.y <  a.y) { d.y = -1; f.y = p0->y-a.y;   g.y = (p0->y-p1->y)*1024; cnt += a.y-c.y; }
	else if (c.y != a.y) { d.y =  1; f.y = a.y+1-p0->y; g.y = (p1->y-p0->y)*1024; cnt += c.y-a.y; }
	else f.y = g.y = 0;
		  if (c.z <  a.z) { d.z = -1; f.z = p0->z-a.z;   g.z = (p0->z-p1->z)*1024; cnt += a.z-c.z; }
	else if (c.z != a.z) { d.z =  1; f.z = a.z+1-p0->z; g.z = (p1->z-p0->z)*1024; cnt += c.z-a.z; }
	else f.z = g.z = 0;

	ftol(f.x*g.z - f.z*g.x,&p.x); ftol(g.x,&i.x);
	ftol(f.y*g.z - f.z*g.y,&p.y); ftol(g.y,&i.y);
	ftol(f.y*g.x - f.x*g.y,&p.z); ftol(g.z,&i.z);

		//NOTE: GIGO! This can happen if p0,p1 (cansee input) is NaN, Inf, etc...
	if ((unsigned int)cnt > (VSID+VSID+2048)*2) cnt = (VSID+VSID+2048)*2;
	while (cnt > 0)
	{
		if (((p.x|p.y) >= 0) && (a.z != c.z)) { a.z += d.z; p.x -= i.x; p.y -= i.y; }
		else if ((p.z >= 0) && (a.x != c.x))  { a.x += d.x; p.x += i.z; p.z -= i.y; }
		else                                  { a.y += d.y; p.y += i.z; p.z += i.x; }
		if (isvoxelsolid(a.x,a.y,a.z)) break;
		cnt--;
	}
	hit->x = a.x; hit->y = a.y; hit->z = a.z; return(!cnt);
}

	//  p: start position
	//  d: direction
	//  h: coordinate of voxel hit (if any)
	//ind: pointer to surface voxel's 32-bit color (0 if none hit)
	//dir: 0-5: last direction moved upon hit (-1 if inside solid)
void hitscan (dpoint3d *p, dpoint3d *d, lpoint3d *h, int **ind, int *dir)
{
	int ixi, iyi, izi, dx, dy, dz, dxi, dyi, dzi, z0, z1, minz;
	float f, kx, ky, kz;
	char *v;

		//Note: (h->x,h->y,h->z) MUST be rounded towards -inf
	(h->x) = (int)p->x;
	(h->y) = (int)p->y;
	(h->z) = (int)p->z;
	if ((unsigned int)(h->x|h->y) >= VSID) { (*ind) = 0; (*dir) = -1; return; }
	ixi = (((((signed int *)&d->x)[1])>>31)|1);
	iyi = (((((signed int *)&d->y)[1])>>31)|1);
	izi = (((((signed int *)&d->z)[1])>>31)|1);

	minz = MIN(h->z,0);

	f = 0x3fffffff/VSID; //Maximum delta value
	if ((fabs(d->x) >= fabs(d->y)) && (fabs(d->x) >= fabs(d->z)))
	{
		kx = 1024.0;
		if (d->y == 0) ky = f; else ky = MIN(fabs(d->x/d->y)*1024.0,f);
		if (d->z == 0) kz = f; else kz = MIN(fabs(d->x/d->z)*1024.0,f);
	}
	else if (fabs(d->y) >= fabs(d->z))
	{
		ky = 1024.0;
		if (d->x == 0) kx = f; else kx = MIN(fabs(d->y/d->x)*1024.0,f);
		if (d->z == 0) kz = f; else kz = MIN(fabs(d->y/d->z)*1024.0,f);
	}
	else
	{
		kz = 1024.0;
		if (d->x == 0) kx = f; else kx = MIN(fabs(d->z/d->x)*1024.0,f);
		if (d->y == 0) ky = f; else ky = MIN(fabs(d->z/d->y)*1024.0,f);
	}
	ftol(kx,&dxi); ftol((p->x-(float)h->x)*kx,&dx); if (ixi >= 0) dx = dxi-dx;
	ftol(ky,&dyi); ftol((p->y-(float)h->y)*ky,&dy); if (iyi >= 0) dy = dyi-dy;
	ftol(kz,&dzi); ftol((p->z-(float)h->z)*kz,&dz); if (izi >= 0) dz = dzi-dz;

	v = sptr[h->y*VSID+h->x];
	if (h->z >= v[1])
	{
		do
		{
			if (!v[0]) { (*ind) = 0; (*dir) = -1; return; }
			v += v[0]*4;
		} while (h->z >= v[1]);
		z0 = v[3];
	} else z0 = minz;
	z1 = v[1];

	while (1)
	{
		//Check cube at: h->x,h->y,h->z

		if ((dz <= dx) && (dz <= dy))
		{
			h->z += izi; dz += dzi; (*dir) = 5-(izi>0);

				//Check if h->z ran into anything solid
			if (h->z < z0)
			{
				if (h->z < minz) (*ind) = 0; else (*ind) = (int *)&v[-4];
				return;
			}
			if (h->z >= z1) { (*ind) = (int *)&v[4]; return; }
		}
		else
		{
			if (dx < dy)
			{
				h->x += ixi; dx += dxi; (*dir) = 1-(ixi>0);
				if ((unsigned int)h->x >= VSID) { (*ind) = 0; return; }
			}
			else
			{
				h->y += iyi; dy += dyi; (*dir) = 3-(iyi>0);
				if ((unsigned int)h->y >= VSID) { (*ind) = 0; return; }
			}

				//Check if (h->x, h->y) ran into anything solid
			v = sptr[h->y*VSID+h->x];
			while (1)
			{
				if (h->z < v[1])
				{
					if (v == sptr[h->y*VSID+h->x]) { z0 = minz; z1 = v[1]; break; }
					if (h->z < v[3]) { (*ind) = (int *)&v[(h->z-v[3])*4]; return; }
					z0 = v[3]; z1 = v[1]; break;
				}
				else if ((h->z <= v[2]) || (!v[0]))
					{ (*ind) = (int *)&v[(h->z-v[1])*4+4]; return; }

				v += v[0]*4;
			}
		}
	}
}

	// p0: start position
	// v0: direction
	//spr: pointer of sprite to test collision with
	//  h: coordinate of voxel hit in sprite coordinates (if any)
	//ind: pointer to voxel hit (kv6voxtype) (0 if none hit)
	//vsc:  input: max multiple/fraction of v0's length to scan (1.0 for |v0|)
	//     output: multiple/fraction of v0's length of hit point
void sprhitscan (dpoint3d *p0, dpoint3d *v0, vx5sprite *spr, lpoint3d *h, kv6voxtype **ind, float *vsc)
{
	kv6voxtype *vx[4];
	kv6data *kv;
	point3d t, u, v;
	lpoint3d a, d, p, q;
	float f, g;
	int i, x, y, xup, ix0, ix1;

	(*ind) = 0;
	if (spr->flags&2)
	{
		kfatype *kf = spr->kfaptr;
			//This sets the sprite pointer to be the parent sprite (voxnum
			//   of the main sprite is invalid for KFA sprites!)
		spr = &kf->spr[kf->hingesort[(kf->numhin)-1]];
	}
	kv = spr->voxnum; if (!kv) return;

		//d transformed to spr space (0,0,0,kv->xsiz,kv->ysiz,kv->zsiz)
	v.x = v0->x*spr->s.x + v0->y*spr->s.y + v0->z*spr->s.z;
	v.y = v0->x*spr->h.x + v0->y*spr->h.y + v0->z*spr->h.z;
	v.z = v0->x*spr->f.x + v0->y*spr->f.y + v0->z*spr->f.z;

		//p transformed to spr space (0,0,0,kv->xsiz,kv->ysiz,kv->zsiz)
	t.x = p0->x-spr->p.x;
	t.y = p0->y-spr->p.y;
	t.z = p0->z-spr->p.z;
	u.x = t.x*spr->s.x + t.y*spr->s.y + t.z*spr->s.z;
	u.y = t.x*spr->h.x + t.y*spr->h.y + t.z*spr->h.z;
	u.z = t.x*spr->f.x + t.y*spr->f.y + t.z*spr->f.z;
	u.x /= (spr->s.x*spr->s.x + spr->s.y*spr->s.y + spr->s.z*spr->s.z);
	u.y /= (spr->h.x*spr->h.x + spr->h.y*spr->h.y + spr->h.z*spr->h.z);
	u.z /= (spr->f.x*spr->f.x + spr->f.y*spr->f.y + spr->f.z*spr->f.z);
	u.x += kv->xpiv; u.y += kv->ypiv; u.z += kv->zpiv;

	ix0 = MAX(vx5.xplanemin,0);
	ix1 = MIN(vx5.xplanemax,kv->xsiz);

		//Increment ray until it hits bounding box
		// (ix0,0,0,ix1-1ulp,kv->ysiz-1ulp,kv->zsiz-1ulp)
	g = (float)ix0;
	t.x = (float)ix1;      (*(int *)&t.x)--;
	t.y = (float)kv->ysiz; (*(int *)&t.y)--;
	t.z = (float)kv->zsiz; (*(int *)&t.z)--;
		  if (u.x <   g) { if (v.x <= 0) return; f = (  g-u.x)/v.x; u.x =   g; u.y += v.y*f; u.z += v.z*f; }
	else if (u.x > t.x) { if (v.x >= 0) return; f = (t.x-u.x)/v.x; u.x = t.x; u.y += v.y*f; u.z += v.z*f; }
		  if (u.y <   0) { if (v.y <= 0) return; f = (  0-u.y)/v.y; u.y =   0; u.x += v.x*f; u.z += v.z*f; }
	else if (u.y > t.y) { if (v.y >= 0) return; f = (t.y-u.y)/v.y; u.y = t.y; u.x += v.x*f; u.z += v.z*f; }
		  if (u.z <   0) { if (v.z <= 0) return; f = (  0-u.z)/v.z; u.z =   0; u.x += v.x*f; u.y += v.y*f; }
	else if (u.z > t.z) { if (v.z >= 0) return; f = (t.z-u.z)/v.z; u.z = t.z; u.x += v.x*f; u.y += v.y*f; }

	ix1 -= ix0;

	g = 262144.0 / sqrt(v.x*v.x + v.y*v.y + v.z*v.z);

		//Note: (a.x,a.y,a.z) MUST be rounded towards -inf
	ftol(u.x-.5,&a.x); if ((unsigned int)(a.x-ix0) >= ix1) return;
	ftol(u.y-.5,&a.y); if ((unsigned int)a.y >= kv->ysiz) return;
	ftol(u.z-.5,&a.z); if ((unsigned int)a.z >= kv->zsiz) return;
	if (*(int *)&v.x < 0) { d.x = -1; u.x -= a.x;      v.x *= -g; }
							else { d.x =  1; u.x = a.x+1-u.x; v.x *=  g; }
	if (*(int *)&v.y < 0) { d.y = -1; u.y -= a.y;      v.y *= -g; }
							else { d.y =  1; u.y = a.y+1-u.y; v.y *=  g; }
	if (*(int *)&v.z < 0) { d.z = -1; u.z -= a.z;      v.z *= -g; }
							else { d.z =  1; u.z = a.z+1-u.z; v.z *=  g; }
	ftol(u.x*v.z - u.z*v.x,&p.x); ftol(v.x,&q.x);
	ftol(u.y*v.z - u.z*v.y,&p.y); ftol(v.y,&q.y);
	ftol(u.y*v.x - u.x*v.y,&p.z); ftol(v.z,&q.z);

		//Check if voxel at: (a.x,a.y,a.z) is solid
	vx[0] = kv->vox;
	for(x=0;x<a.x;x++) vx[0] += kv->xlen[x];
	vx[1] = vx[0]; xup = x*kv->ysiz;
	for(y=0;y<a.y;y++) vx[1] += kv->ylen[xup+y];
	vx[2] = vx[1]; vx[3] = &vx[1][kv->ylen[xup+y]];

	while (1)
	{
		//vs = kv->vox; //Brute force: remove all vx[?] code to enable this
		//for(x=0;x<a.x;x++) vs += kv->xlen[x];
		//for(y=0;y<a.y;y++) vs += kv->ylen[x*kv->ysiz+y];
		//for(ve=&vs[kv->ylen[x+y]];vs<ve;vs++) if (vs->z == a.z) break;

			//Check if voxel at: (a.x,a.y,a.z) is solid
		if (vx[1] < vx[3])
		{
			while ((a.z < vx[2]->z) && (vx[2] > vx[1]  )) vx[2]--;
			while ((a.z > vx[2]->z) && (vx[2] < vx[3]-1)) vx[2]++;
			if (a.z == vx[2]->z) break;
		}

		if ((p.x|p.y) >= 0)
		{
			a.z += d.z; if ((unsigned int)a.z >= kv->zsiz) return;
			p.x -= q.x; p.y -= q.y;
		}
		else if (p.z < 0)
		{
			a.y += d.y; if ((unsigned int)a.y >= kv->ysiz) return;
			p.y += q.z; p.z += q.x;

			if (a.y < y) { y--; vx[1] -= kv->ylen[xup+y];      }
			if (a.y > y) {      vx[1] += kv->ylen[xup+y]; y++; }
			vx[2] = vx[1]; vx[3] = &vx[1][kv->ylen[xup+y]];
		}
		else
		{
			a.x += d.x; if ((unsigned int)(a.x-ix0) >= ix1) return;
			p.x += q.z; p.z -= q.y;

			if (a.x < x) { x--; vx[0] -= kv->xlen[x];      xup -= kv->ysiz; }
			if (a.x > x) {      vx[0] += kv->xlen[x]; x++; xup += kv->ysiz; }
			if ((a.y<<1) < kv->ysiz) //Start y-slice search from closer side
			{
				vx[1] = vx[0];
				for(y=0;y<a.y;y++) vx[1] += kv->ylen[xup+y];
			}
			else
			{
				vx[1] = &vx[0][kv->xlen[x]];
				for(y=kv->ysiz;y>a.y;y--) vx[1] -= kv->ylen[xup+y-1];
			}
			vx[2] = vx[1]; vx[3] = &vx[1][kv->ylen[xup+y]];
		}
	}

		//given: a = kv6 coordinate, find: v = vxl coordinate
	u.x = (float)a.x-kv->xpiv;
	u.y = (float)a.y-kv->ypiv;
	u.z = (float)a.z-kv->zpiv;
	v.x = u.x*spr->s.x + u.y*spr->h.x + u.z*spr->f.x + spr->p.x;
	v.y = u.x*spr->s.y + u.y*spr->h.y + u.z*spr->f.y + spr->p.y;
	v.z = u.x*spr->s.z + u.y*spr->h.z + u.z*spr->f.z + spr->p.z;

		//Stupid dot product stuff...
	f = ((v.x-p0->x)*v0->x + (v.y-p0->y)*v0->y + (v.z-p0->z)*v0->z) /
		  (v0->x*v0->x + v0->y*v0->y + v0->z*v0->z);
	if (f >= (*vsc)) return;
	{ (*vsc) = f; (*h) = a; (*ind) = vx[2]; (*vsc) = f; }
}

unsigned int calcglobalmass ()
{
	unsigned int i, j;
	char *v;

	j = VSID*VSID*256;
	for(i=0;i<VSID*VSID;i++)
	{
		v = sptr[i]; j -= v[1];
		while (v[0]) { v += v[0]*4; j += v[3]-v[1]; }
	}
	return(j);
}

/**
 * Sticks a default VXL map into memory (puts you inside a brownish box)
 * This is useful for VOXED when you want to start a new map.
 * @param ipo default starting camera position
 * @param ist RIGHT unit vector
 * @param ihe DOWN unit vector
 * @param ifo FORWARD unit vector
 */
VOXLAP_DLL_FUNC void loadnul (dpoint3d *ipo, dpoint3d *ist, dpoint3d *ihe, dpoint3d *ifo)
{
	lpoint3d lp0, lp1;
	int i, x, y;
	char *v;
	float f;

	if (!vbuf) { vbuf = (int *)malloc((VOXSIZ>>2)<<2); if (!vbuf) evilquit("vbuf malloc failed"); }
	if (!vbit) { vbit = (int *)malloc((VOXSIZ>>7)<<2); if (!vbit) evilquit("vbuf malloc failed"); }

	v = (char *)(&vbuf[1]); //1st dword for voxalloc compare logic optimization

		//Completely re-compile vbuf
	for(x=0;x<VSID;x++)
		for(y=0;y<VSID;y++)
		{
			sptr[y*VSID+x] = v;
			i = 0; // + (rand()&1);  //i = default height of plain
			v[0] = 0;
			v[1] = i;
			v[2] = i;
			v[3] = 0;  //z0 (Dummy filler)
			//i = ((((x+y)>>3) + ((x^y)>>4)) % 231) + 16;
			//i = (i<<16)+(i<<8)+i;
			v += 4;
			(*(int *)v) = ((x^y)&15)*0x10101+0x807c7c7c; //colorjit(i,0x70707)|0x80000000;
			v += 4;
		}



/*	memset(&sptr[VSID*VSID],0,sizeof(sptr)-VSID*VSID*4);*/
	
	
	vbiti = (((ptrdiff_t)v-(ptrdiff_t)vbuf)>>2); //# vbuf longs/vbit bits allocated
	clearbuf((void *)vbit,vbiti>>5,-1);
	clearbuf((void *)&vbit[vbiti>>5],(VOXSIZ>>7)-(vbiti>>5),0);
	vbit[vbiti>>5] = (1<<vbiti)-1;

		//Blow out sphere and stick you inside map
	vx5.colfunc = jitcolfunc; vx5.curcol = 0x80704030;

	lp0.x = VSID*.5-90; lp0.y = VSID*.5-90; lp0.z = MAXZDIM*.5-45;
	lp1.x = VSID*.5+90; lp1.y = VSID*.5+90; lp1.z = MAXZDIM*.5+45;
	setrect(&lp0,&lp1,-1);
	//lp.x = VSID*.5; lp.y = VSID*.5; lp.z = MAXZDIM*.5; setsphere(&lp,64,-1);

	vx5.globalmass = calcglobalmass();

	ipo->x = VSID*.5; ipo->y = VSID*.5; ipo->z = MAXZDIM*.5; //ipo->z = -16;
	f = 0.0*PI/180.0;
	ist->x = cos(f); ist->y = sin(f); ist->z = 0;
	ihe->x = 0; ihe->y = 0; ihe->z = 1;
	ifo->x = sin(f); ifo->y = -cos(f); ifo->z = 0;

	gmipnum = 1; vx5.flstnum = 0;
	updatebbox(0,0,0,VSID,VSID,MAXZDIM,0);
}


//Quake3 .BSP loading code begins --------------------------------------------
typedef struct { int c, i; float z, z1; } vlinerectyp;
static point3d q3pln[5250];
static float q3pld[5250], q3vz[256];
static int q3nod[4850][3], q3lf[4850];
int vlinebsp (float x, float y, float z0, float z1, float *dvz)
{
	vlinerectyp vlrec[64];
	float z, t;
	int i, j, vcnt, vlcnt;
	char vt[256];

	vcnt = 1; i = 0; vlcnt = 0; vt[0] = 17;
	while (1)
	{
		if (i < 0)
		{
			if (vt[vcnt-1] != (q3lf[~i]&255))
				{ dvz[vcnt] = z0; vt[vcnt] = (q3lf[~i]&255); vcnt++; }
		}
		else
		{
			j = q3nod[i][0]; z = q3pld[j] - q3pln[j].x*x - q3pln[j].y*y;
			t = q3pln[j].z*z0-z;
			if ((t < 0) == (q3pln[j].z*z1 < z))
				{ vlrec[vlcnt].c = 0; i = q3nod[i][(t<0)+1]; }
			else
			{
				z /= q3pln[j].z; j = (q3pln[j].z<0)+1;
				vlrec[vlcnt].c = 1; vlrec[vlcnt].i = q3nod[i][j];
				vlrec[vlcnt].z = z; vlrec[vlcnt].z1 = z1;
				i = q3nod[i][3-j]; z1 = z;
			}
			vlcnt++; continue;
		}
		do { vlcnt--; if (vlcnt < 0) return(vcnt); } while (!vlrec[vlcnt].c);
		vlrec[vlcnt].c = 0; i = vlrec[vlcnt].i;
		z0 = vlrec[vlcnt].z; z1 = vlrec[vlcnt].z1;
		vlcnt++;
	}
	return(0);
}

	//Stupidly useless declarations:
void delslab(int *b2, int y0, int y1);
int *scum2(int x, int y);
void scum2finish();

/**
 * Loads a native Voxlap5 .VXL file into memory.
 * @param filnam .VXL map formatted like this: "UNTITLED.VXL"
 * @param ipo default starting camera position
 * @param ist RIGHT unit vector
 * @param ihe DOWN unit vector
 * @param ifo FORWARD unit vector
 * @return 0:bad, 1:good
 */
int loadvxl (const char *lodfilnam)
{
	int i, j;
	char *v;

	if (!vbuf) { vbuf = (int *)malloc((VOXSIZ>>2)<<2); if (!vbuf) evilquit("vbuf malloc failed"); }
	if (!vbit) { vbit = (int *)malloc((VOXSIZ>>7)<<2); if (!vbit) evilquit("vbuf malloc failed"); }

	FILE *f=fopen(lodfilnam, "r");
	if(!f)
		return 0;
	fseek(f, 0, SEEK_END);
	int fsiz=ftell(f);
	fseek(f, 0, SEEK_SET);
	v = (char *)(&vbuf[1]); //1st dword for voxalloc compare logic optimization
	fread(v, fsiz, 1, f);
	fclose(f);
	for(i=0;i<VSID*VSID;i++)
	{
		sptr[i] = v;
		++j;
		while (*v) {v += (*v)<<2;}
		v += ((((int)v[2])-((int)v[1])+2)<<2);
	}
	unsigned int size=VSID*VSID;
	memset(&sptr[size],0,sizeof(sptr)-size*sizeof(sptr[0]));
	vbiti = (((ptrdiff_t)v-(ptrdiff_t)vbuf)>>2); //# vbuf longs/vbit bits allocated
	clearbuf((void *)vbit,vbiti>>5,-1);
	clearbuf((void *)&vbit[vbiti>>5],(VOXSIZ>>7)-(vbiti>>5),0);
	vbit[vbiti>>5] = (1<<vbiti)-1;

	vx5.globalmass = calcglobalmass();
	backedup = -1;

	gmipnum = 1; vx5.flstnum = 0;
/*	updatebbox(0,0,0,VSID,VSID,MAXZDIM,0);*/
	return(1);
}

/*Loads VXL from a buffer*/
int Vox_vloadvxl (const char *data, unsigned int size)
{
	int i, j, fsiz;
	char *v, *v2;

	if (!vbuf) { vbuf = (int *)malloc((VOXSIZ>>2)<<2); if (!vbuf) evilquit("vbuf malloc failed"); }
	if (!vbit) { vbit = (int *)malloc((VOXSIZ>>7)<<2); if (!vbit) evilquit("vbuf malloc failed"); }
	
	v = (char *)(&vbuf[1]);
	memcpy(v,data,size);
	for(i=0;i<VSID*VSID;i++)
	{
		sptr[i] = v;
		++j;
		while (*v) {v += (*v)<<2;}
		v += ((((int)v[2])-((int)v[1])+2)<<2);
	}
	memset(&sptr[VSID*VSID],0,sizeof(sptr)-VSID*VSID*sizeof(sptr[0]));
	vbiti = (((ptrdiff_t)v-(ptrdiff_t)vbuf)>>2); //# vbuf longs/vbit bits allocated
	clearbuf((void *)vbit,vbiti>>5,-1);
	clearbuf((void *)&vbit[vbiti>>5],(VOXSIZ>>7)-(vbiti>>5),0);
	vbit[vbiti>>5] = (1<<vbiti)-1;

	vx5.globalmass = calcglobalmass();
	backedup = -1;

	gmipnum = 1; vx5.flstnum = 0;
	updatebbox(0,0,0,VSID,VSID,MAXZDIM,0);
	return(1);
}

/**
 * Saves a native Voxlap5 .VXL file & specified position to disk
 * @param filnam .VXL map formatted like this: "UNTITLED.VXL"
 * @param ipo default starting camera position
 * @param ist RIGHT unit vector
 * @param ihe DOWN unit vector
 * @param ifo FORWARD unit vector
 * @return 0:bad, 1:good
 */
int savevxl (const char *savfilnam, dpoint3d *ipo, dpoint3d *ist, dpoint3d *ihe, dpoint3d *ifo)
{
	FILE *fil;
	int i;

	if (!(fil = fopen(savfilnam,"wb"))) return(0);
	i = 0x09072000; fwrite(&i,4,1,fil);  //Version
	i = VSID; fwrite(&i,4,1,fil);
	i = VSID; fwrite(&i,4,1,fil);
	fwrite(ipo,24,1,fil);
	fwrite(ist,24,1,fil);
	fwrite(ihe,24,1,fil);
	fwrite(ifo,24,1,fil);
	for(i=0;i<VSID*VSID;i++) fwrite((void *)sptr[i],slng(sptr[i]),1,fil);
	fclose(fil);
	return(1);
}

/**
 * Loads a sky into memory. Sky must be PNG,JPG,TGA,GIF,BMP,PCX formatted as
 * a Mercator projection on its side. This means x-coordinate is latitude
 * and y-coordinate is longitude. Loadsky() can be called at any time.
 *
 * If for some reason you don't want to load a textured sky, you call call
 * loadsky with these 2 built-in skies:
 * loadsky("BLACK");  //pitch black
 * loadsky("BLUE");   //a cool ramp of bright blue to blue to greenish
 *
 * @param skyfilnam the name of the image to load
 * @return -1:bad, 0:good
 */
/*
VOXLAP_DLL_FUNC int loadsky (const char *skyfilnam)
{
	int x, y, xoff, yoff;
	float ang, f;

	if (skypic) { free((void *)skypic); skypic = skyoff = 0; }
	xoff = yoff = 0;

	//This used to be strcasecmp, but that ran in an endless loop
	if (!strcmp(skyfilnam,"BLACK")) return(0);
	if (!strcmp(skyfilnam,"BLUE")) goto loadbluesky;

	kpzload(skyfilnam,(int *)&skypic,(int *)&skybpl,(int *)&skyxsiz,(int *)&skyysiz);
	

	if (!skypic)
	{
		int r, g, b, *p;
loadbluesky:;
			//Load default sky
		skyxsiz = 512; skyysiz = 1; skybpl = skyxsiz*4;
		if (!(skypic = (int)malloc(skyysiz*skybpl))) return(-1);

		p = (int *)skypic; y = skyxsiz*skyxsiz;
		for(x=0;x<=(skyxsiz>>1);x++)
		{
			p[x] = ((((x*1081 - skyxsiz*252)*x)/y + 35)<<16)+
					 ((((x* 950 - skyxsiz*198)*x)/y + 53)<<8)+
					  (((x* 439 - skyxsiz* 21)*x)/y + 98);
		}
		p[skyxsiz-1] = 0x50903c;
		r = ((p[skyxsiz>>1]>>16)&255);
		g = ((p[skyxsiz>>1]>>8)&255);
		b = ((p[skyxsiz>>1])&255);
		for(x=(skyxsiz>>1)+1;x<skyxsiz;x++)
		{
			p[x] = ((((0x50-r)*(x-(skyxsiz>>1)))/(skyxsiz-1-(skyxsiz>>1))+r)<<16)+
					 ((((0x90-g)*(x-(skyxsiz>>1)))/(skyxsiz-1-(skyxsiz>>1))+g)<<8)+
					 ((((0x3c-b)*(x-(skyxsiz>>1)))/(skyxsiz-1-(skyxsiz>>1))+b));
		}
		y = skyxsiz*skyysiz;
		for(x=skyxsiz;x<y;x++) p[x] = p[x-skyxsiz];
	}
		//Initialize look-up table for longitudes
	if (skylng) free((void *)skylng);
	if (!(skylng = (point2d *)malloc(skyysiz*8))) return(-1);
	f = PI*2.0 / ((float)skyysiz);
	for(y=skyysiz-1;y>=0;y--)
		fcossin((float)y*f+PI,&skylng[y].x,&skylng[y].y);
	skylngmul = (float)skyysiz/(PI*2);
		//This makes those while loops in gline() not lockup when skyysiz==1
	if (skyysiz == 1) { skylng[0].x = 0; skylng[0].y = 0; }

		//Initialize look-up table for latitudes
	if (skylat) free((void *)skylat);
	if (!(skylat = (int *)malloc(skyxsiz*4))) return(-1);
	f = PI*.5 / ((float)skyxsiz);
	for(x=skyxsiz-1;x;x--)
	{
		ang = (float)((x<<1)-skyxsiz)*f;
		ftol(cos(ang)*32767.0,&xoff);
		ftol(sin(ang)*32767.0,&yoff);
		skylat[x] = (xoff<<16)+((-yoff)&65535);
	}
	skylat[0] = 0; //Hack to make sure assembly index never goes < 0
	skyxsiz--; //Hack for assembly code

	return(0);
}*/

void orthonormalize (point3d *v0, point3d *v1, point3d *v2)
{
	float t;

	t = 1.0 / sqrt((v0->x)*(v0->x) + (v0->y)*(v0->y) + (v0->z)*(v0->z));
	(v0->x) *= t; (v0->y) *= t; (v0->z) *= t;
	t = (v1->x)*(v0->x) + (v1->y)*(v0->y) + (v1->z)*(v0->z);
	(v1->x) -= t*(v0->x); (v1->y) -= t*(v0->y); (v1->z) -= t*(v0->z);
	t = 1.0 / sqrt((v1->x)*(v1->x) + (v1->y)*(v1->y) + (v1->z)*(v1->z));
	(v1->x) *= t; (v1->y) *= t; (v1->z) *= t;
	(v2->x) = (v0->y)*(v1->z) - (v0->z)*(v1->y);
	(v2->y) = (v0->z)*(v1->x) - (v0->x)*(v1->z);
	(v2->z) = (v0->x)*(v1->y) - (v0->y)*(v1->x);
}

void dorthonormalize (dpoint3d *v0, dpoint3d *v1, dpoint3d *v2)
{
	double t;

	t = 1.0 / sqrt((v0->x)*(v0->x) + (v0->y)*(v0->y) + (v0->z)*(v0->z));
	(v0->x) *= t; (v0->y) *= t; (v0->z) *= t;
	t = (v1->x)*(v0->x) + (v1->y)*(v0->y) + (v1->z)*(v0->z);
	(v1->x) -= t*(v0->x); (v1->y) -= t*(v0->y); (v1->z) -= t*(v0->z);
	t = 1.0 / sqrt((v1->x)*(v1->x) + (v1->y)*(v1->y) + (v1->z)*(v1->z));
	(v1->x) *= t; (v1->y) *= t; (v1->z) *= t;
	(v2->x) = (v0->y)*(v1->z) - (v0->z)*(v1->y);
	(v2->y) = (v0->z)*(v1->x) - (v0->x)*(v1->z);
	(v2->z) = (v0->x)*(v1->y) - (v0->y)*(v1->x);
}

void orthorotate (float ox, float oy, float oz, point3d *ist, point3d *ihe, point3d *ifo)
{
	float f, t, dx, dy, dz, rr[9];

	fcossin(ox,&ox,&dx);
	fcossin(oy,&oy,&dy);
	fcossin(oz,&oz,&dz);
	f = ox*oz; t = dx*dz; rr[0] =  t*dy + f; rr[7] = -f*dy - t;
	f = ox*dz; t = dx*oz; rr[1] = -f*dy + t; rr[6] =  t*dy - f;
	rr[2] = dz*oy; rr[3] = -dx*oy; rr[4] = ox*oy; rr[8] = oz*oy; rr[5] = dy;
	ox = ist->x; oy = ihe->x; oz = ifo->x;
	ist->x = ox*rr[0] + oy*rr[3] + oz*rr[6];
	ihe->x = ox*rr[1] + oy*rr[4] + oz*rr[7];
	ifo->x = ox*rr[2] + oy*rr[5] + oz*rr[8];
	ox = ist->y; oy = ihe->y; oz = ifo->y;
	ist->y = ox*rr[0] + oy*rr[3] + oz*rr[6];
	ihe->y = ox*rr[1] + oy*rr[4] + oz*rr[7];
	ifo->y = ox*rr[2] + oy*rr[5] + oz*rr[8];
	ox = ist->z; oy = ihe->z; oz = ifo->z;
	ist->z = ox*rr[0] + oy*rr[3] + oz*rr[6];
	ihe->z = ox*rr[1] + oy*rr[4] + oz*rr[7];
	ifo->z = ox*rr[2] + oy*rr[5] + oz*rr[8];
	//orthonormalize(ist,ihe,ifo);
}

void dorthorotate (double ox, double oy, double oz, dpoint3d *ist, dpoint3d *ihe, dpoint3d *ifo)
{
	double f, t, dx, dy, dz, rr[9];

	dcossin(ox,&ox,&dx);
	dcossin(oy,&oy,&dy);
	dcossin(oz,&oz,&dz);
	f = ox*oz; t = dx*dz; rr[0] =  t*dy + f; rr[7] = -f*dy - t;
	f = ox*dz; t = dx*oz; rr[1] = -f*dy + t; rr[6] =  t*dy - f;
	rr[2] = dz*oy; rr[3] = -dx*oy; rr[4] = ox*oy; rr[8] = oz*oy; rr[5] = dy;
	ox = ist->x; oy = ihe->x; oz = ifo->x;
	ist->x = ox*rr[0] + oy*rr[3] + oz*rr[6];
	ihe->x = ox*rr[1] + oy*rr[4] + oz*rr[7];
	ifo->x = ox*rr[2] + oy*rr[5] + oz*rr[8];
	ox = ist->y; oy = ihe->y; oz = ifo->y;
	ist->y = ox*rr[0] + oy*rr[3] + oz*rr[6];
	ihe->y = ox*rr[1] + oy*rr[4] + oz*rr[7];
	ifo->y = ox*rr[2] + oy*rr[5] + oz*rr[8];
	ox = ist->z; oy = ihe->z; oz = ifo->z;
	ist->z = ox*rr[0] + oy*rr[3] + oz*rr[6];
	ihe->z = ox*rr[1] + oy*rr[4] + oz*rr[7];
	ifo->z = ox*rr[2] + oy*rr[5] + oz*rr[8];
	//dorthonormalize(ist,ihe,ifo);
}

void axisrotate (point3d *p, point3d *axis, float w)
{
	point3d ax;
	float t, c, s, ox, oy, oz, k[9];

	fcossin(w,&c,&s);
	t = axis->x*axis->x + axis->y*axis->y + axis->z*axis->z; if (t == 0) return;
	t = 1.0 / sqrt(t); ax.x = axis->x*t; ax.y = axis->y*t; ax.z = axis->z*t;

	t = 1.0-c;
	k[0] = ax.x*t; k[7] = ax.x*s; oz = ax.y*k[0];
	k[4] = ax.y*t; k[2] = ax.y*s; oy = ax.z*k[0];
	k[8] = ax.z*t; k[3] = ax.z*s; ox = ax.z*k[4];
	k[0] = ax.x*k[0] + c; k[5] = ox - k[7]; k[7] += ox;
	k[4] = ax.y*k[4] + c; k[6] = oy - k[2]; k[2] += oy;
	k[8] = ax.z*k[8] + c; k[1] = oz - k[3]; k[3] += oz;

	ox = p->x; oy = p->y; oz = p->z;
	p->x = ox*k[0] + oy*k[1] + oz*k[2];
	p->y = ox*k[3] + oy*k[4] + oz*k[5];
	p->z = ox*k[6] + oy*k[7] + oz*k[8];
}

void slerp (point3d *istr, point3d *ihei, point3d *ifor,
				point3d *istr2, point3d *ihei2, point3d *ifor2,
				point3d *ist, point3d *ihe, point3d *ifo, float rat)
{
	point3d ax;
	float c, s, t, ox, oy, oz, k[9];

	ist->x = istr->x; ist->y = istr->y; ist->z = istr->z;
	ihe->x = ihei->x; ihe->y = ihei->y; ihe->z = ihei->z;
	ifo->x = ifor->x; ifo->y = ifor->y; ifo->z = ifor->z;

	ax.x = istr->y*istr2->z - istr->z*istr2->y + ihei->y*ihei2->z - ihei->z*ihei2->y + ifor->y*ifor2->z - ifor->z*ifor2->y;
	ax.y = istr->z*istr2->x - istr->x*istr2->z + ihei->z*ihei2->x - ihei->x*ihei2->z + ifor->z*ifor2->x - ifor->x*ifor2->z;
	ax.z = istr->x*istr2->y - istr->y*istr2->x + ihei->x*ihei2->y - ihei->y*ihei2->x + ifor->x*ifor2->y - ifor->y*ifor2->x;
	t = ax.x*ax.x + ax.y*ax.y + ax.z*ax.z; if (t == 0) return;

		//Based on the vector suck-out method (see ROTATE2.BAS)
	ox = istr->x*ax.x + istr->y*ax.y + istr->z*ax.z;
	oy = ihei->x*ax.x + ihei->y*ax.y + ihei->z*ax.z;
	if (fabs(ox) < fabs(oy))
		{ c = istr->x*istr2->x + istr->y*istr2->y + istr->z*istr2->z; s = ox*ox; }
	else
		{ c = ihei->x*ihei2->x + ihei->y*ihei2->y + ihei->z*ihei2->z; s = oy*oy; }
	if (t == s) return;
	c = (c*t - s) / (t-s);
	if (c < -1) c = -1;
	if (c > 1) c = 1;
	fcossin(acos(c)*rat,&c,&s);

	t = 1.0 / sqrt(t); ax.x *= t; ax.y *= t; ax.z *= t;

	t = 1.0f-c;
	k[0] = ax.x*t; k[7] = ax.x*s; oz = ax.y*k[0];
	k[4] = ax.y*t; k[2] = ax.y*s; oy = ax.z*k[0];
	k[8] = ax.z*t; k[3] = ax.z*s; ox = ax.z*k[4];
	k[0] = ax.x*k[0] + c; k[5] = ox - k[7]; k[7] += ox;
	k[4] = ax.y*k[4] + c; k[6] = oy - k[2]; k[2] += oy;
	k[8] = ax.z*k[8] + c; k[1] = oz - k[3]; k[3] += oz;

	ox = ist->x; oy = ist->y; oz = ist->z;
	ist->x = ox*k[0] + oy*k[1] + oz*k[2];
	ist->y = ox*k[3] + oy*k[4] + oz*k[5];
	ist->z = ox*k[6] + oy*k[7] + oz*k[8];

	ox = ihe->x; oy = ihe->y; oz = ihe->z;
	ihe->x = ox*k[0] + oy*k[1] + oz*k[2];
	ihe->y = ox*k[3] + oy*k[4] + oz*k[5];
	ihe->z = ox*k[6] + oy*k[7] + oz*k[8];

	ox = ifo->x; oy = ifo->y; oz = ifo->z;
	ifo->x = ox*k[0] + oy*k[1] + oz*k[2];
	ifo->y = ox*k[3] + oy*k[4] + oz*k[5];
	ifo->z = ox*k[6] + oy*k[7] + oz*k[8];
}

void expandrle (int x, int y, int *uind)
{
	int i;
	char *v,v1,v3;

	if ((x|y)&(~(VSID-1))) { uind[0] = 0; uind[1] = MAXZDIM; return; }

	v = sptr[y*VSID+x]; uind[0] = v[1]; i = 2;
	while (*v)
	{
		v += *v<<2;
		++v;
		v1=*v;
		--v;
		v3=v[3];
		if (v3 >=v1) continue;
		uind[i-1] = v3; uind[i] =v1; i += 2;
	}
	uind[i-1] = MAXZDIM;
}

	//Inputs:  n0[<=MAXZDIM]: rle buffer of column to compress
	//         n1-4[<=MAXZDIM]: neighboring rle buffers
	//         top,bot,top,bot,... (ends when bot == MAXZDIM)
	//         px,py: takes color from original column (before modification)
	//            If originally unexposed, calls vx5.colfunc(.)
	//Outputs: cbuf[MAXCSIZ]: compressed output buffer
	//Returns: n: length of compressed buffer (in bytes)
int compilerle (int *n0, int *n1, int *n2, int *n3, int *n4, char *cbuf, int px, int py)
{
	int i, ia, ze, zend, onext, dacnt, n;
	lpoint3d p;

	p.x = px; p.y = py;

		//Generate pointers to color slabs in this format:
		//   0:z0,  1:z1,  2:(pointer to z0's color)-z0
	char *v = sptr[py*VSID+px];
	size_t *ic = tbuf2;
	while (1)
	{
		ia = v[1]; p.z = v[2];
		ic[0] = ia; ic[1] = p.z+1; ic[2] = ((size_t)v)-(ia<<2)+4; ic += 3;
		i = v[0]; if (!i) break;
		v += i*4; ze = v[3];
		ic[0] = ze+p.z-ia-i+2; ic[1] = ze; ic[2] = ((size_t)v)-(ze<<2); ic += 3;
	}
	ic[0] = MAXZDIM; ic[1] = MAXZDIM;

	p.z = n0[0]; cbuf[1] = n0[0];
	ze = n0[1]; cbuf[2] = ze-1;
	cbuf[3] = 0;
	i = onext = 0; ic = tbuf2; ia = 15; n = 4;
	if (ze != MAXZDIM) zend = ze-1; else zend = -1;
	while (1)
	{
		dacnt = 0;
		while (1)
		{
			do
			{
				while (p.z >= ic[1]) ic += 3;
				if (p.z >= ic[0]) *(int *)&cbuf[n] = *(int *)(ic[2]+(p.z<<2));
								 else *(int *)&cbuf[n] = vx5.colfunc(&p);
				n += 4; p.z++; if (p.z >= ze) goto rlendit2;
				while (p.z >= n1[0]) { n1++; ia ^= 1; }
				while (p.z >= n2[0]) { n2++; ia ^= 2; }
				while (p.z >= n3[0]) { n3++; ia ^= 4; }
				while (p.z >= n4[0]) { n4++; ia ^= 8; }
			} while ((ia) || (p.z == zend));

			if (!dacnt) { cbuf[onext+2] = p.z-1; dacnt = 1; }
			else
			{
				cbuf[onext] = ((n-onext)>>2); onext = n;
				cbuf[n+1] = p.z; cbuf[n+2] = p.z-1; cbuf[n+3] = p.z; n += 4;
			}

			if ((n1[0] < n2[0]) && (n1[0] < n3[0]) && (n1[0] < n4[0]))
				{ if (n1[0] >= ze) { p.z = ze-1; } else { p.z = *n1++; ia ^= 1; } }
			else if ((n2[0] < n3[0]) && (n2[0] < n4[0]))
				{ if (n2[0] >= ze) { p.z = ze-1; } else { p.z = *n2++; ia ^= 2; } }
			else if (n3[0] < n4[0])
				{ if (n3[0] >= ze) { p.z = ze-1; } else { p.z = *n3++; ia ^= 4; } }
			else
				{ if (n4[0] >= ze) { p.z = ze-1; } else { p.z = *n4++; ia ^= 8; } }

			if (p.z == MAXZDIM-1) goto rlenditall;
		}
rlendit2:;
		if (ze >= MAXZDIM) break;

		i += 2;
		cbuf[onext] = ((n-onext)>>2); onext = n;
		p.z = n0[i]; cbuf[n+1] = n0[i]; cbuf[n+3] = ze;
		ze = n0[i+1]; cbuf[n+2] = ze-1;
		n += 4;
	}
rlenditall:;
	cbuf[onext] = 0;
	return(n);
}

	//Delete everything on b2() in y0<=y<y1
void delslab (int *b2, int y0, int y1)
{
	int i, j, z;

	if (y1 >= MAXZDIM) y1 = MAXZDIM-1;
	if ((y0 >= y1) || (!b2)) return;
	for(z=0;y0>=b2[z+1];z+=2);
	if (y0 > b2[z])
	{
		if (y1 < b2[z+1])
		{
			for(i=z;b2[i+1]<MAXZDIM;i+=2);
			while (i > z) { b2[i+3] = b2[i+1]; b2[i+2] = b2[i]; i -= 2; }
			b2[z+3] = b2[z+1]; b2[z+1] = y0; b2[z+2] = y1; return;
		}
		b2[z+1] = y0; z += 2;
	}
	if (y1 >= b2[z+1])
	{
		for(i=z+2;y1>=b2[i+1];i+=2);
		j = z-i; b2[z] = b2[i]; b2[z+1] = b2[i+1];
		while (b2[i+1] < MAXZDIM)
			{ i += 2; b2[i+j] = b2[i]; b2[i+j+1] = b2[i+1]; }
	}
	if (y1 > b2[z]) b2[z] = y1;
}

	//Insert everything on b2() in y0<=y<y1
void insslab (int *b2, int y0, int y1)
{
	int i, j, z;

	if ((y0 >= y1) || (!b2)) return;
	for(z=0;y0>b2[z+1];z+=2);
	if (y1 < b2[z])
	{
		for(i=z;b2[i+1]<MAXZDIM;i+=2);
		do { b2[i+3] = b2[i+1]; b2[i+2] = b2[i]; i -= 2; } while (i >= z);
		b2[z+1] = y1; b2[z] = y0; return;
	}
	if (y0 < b2[z]) b2[z] = y0;
	if ((y1 >= b2[z+2]) && (b2[z+1] < MAXZDIM))
	{
		for(i=z+2;(y1 >= b2[i+2]) && (b2[i+1] < MAXZDIM);i+=2);
		j = z-i; b2[z+1] = b2[i+1];
		while (b2[i+1] < MAXZDIM)
			{ i += 2; b2[i+j] = b2[i]; b2[i+j+1] = b2[i+1]; }
	}
	if (y1 > b2[z+1]) b2[z+1] = y1;
}

//------------------------ SETCOLUMN CODE BEGINS ----------------------------

static int scx0, scx1, scox0, scox1, scoox0, scoox1;
static int scex0, scex1, sceox0, sceox1, scoy = 0x80000000, *scoym3;

void scumline ()
{
	int i, j, k, x, y, x0, x1, *mptr, *uptr;
	char *v;

	x0 = MIN(scox0-1,MIN(scx0,scoox0)); scoox0 = scox0; scox0 = scx0;
	x1 = MAX(scox1+1,MAX(scx1,scoox1)); scoox1 = scox1; scox1 = scx1;

	uptr = &scoym3[SCPITCH]; if (uptr == &radar[SCPITCH*9]) uptr = &radar[SCPITCH*6];
	mptr = &uptr[SCPITCH];   if (mptr == &radar[SCPITCH*9]) mptr = &radar[SCPITCH*6];

	if ((x1 < sceox0) || (x0 > sceox1))
	{
		for(x=x0;x<=x1;x++) expandstack(x,scoy-2,&uptr[x*SCPITCH*3]);
	}
	else
	{
		for(x=x0;x<sceox0;x++) expandstack(x,scoy-2,&uptr[x*SCPITCH*3]);
		for(x=x1;x>sceox1;x--) expandstack(x,scoy-2,&uptr[x*SCPITCH*3]);
	}

	if ((scex1|x1) >= 0)
	{
		for(x=x1+2;x<scex0;x++) expandstack(x,scoy-1,&mptr[x*SCPITCH*3]);
		for(x=x0-2;x>scex1;x--) expandstack(x,scoy-1,&mptr[x*SCPITCH*3]);
	}
	if ((x1+1 < scex0) || (x0-1 > scex1))
	{
		for(x=x0-1;x<=x1+1;x++) expandstack(x,scoy-1,&mptr[x*SCPITCH*3]);
	}
	else
	{
		for(x=x0-1;x<scex0;x++) expandstack(x,scoy-1,&mptr[x*SCPITCH*3]);
		for(x=x1+1;x>scex1;x--) expandstack(x,scoy-1,&mptr[x*SCPITCH*3]);
	}
	sceox0 = MIN(x0-1,scex0);
	sceox1 = MAX(x1+1,scex1);

	if ((x1 < scx0) || (x0 > scx1))
	{
		for(x=x0;x<=x1;x++) expandstack(x,scoy,&scoym3[x*SCPITCH*3]);
	}
	else
	{
		for(x=x0;x<scx0;x++) expandstack(x,scoy,&scoym3[x*SCPITCH*3]);
		for(x=x1;x>scx1;x--) expandstack(x,scoy,&scoym3[x*SCPITCH*3]);
	}
	scex0 = x0;
	scex1 = x1;

	y = scoy-1; if (y&(~(VSID-1))) return;
	if (x0 < 0) x0 = 0;
	if (x1 >= VSID) x1 = VSID-1;
	i = y*VSID+x0; k = x0*SCPITCH*3;
	for(x=x0;x<=x1;x++,i++,k+=SCPITCH*3)
	{
		v = sptr[i]; vx5.globalmass += v[1];
		while (v[0]) { v += v[0]*4; vx5.globalmass += v[1]-v[3]; }

			//De-allocate column (x,y)
		voxdealloc(sptr[i]);

		j = compilestack(&mptr[k],&mptr[k-SCPITCH*3],&mptr[k+SCPITCH*3],&uptr[k],&scoym3[k],
							  tbuf,x,y);

			//Allocate & copy to new column (x,y)
		sptr[i] = v = voxalloc(j); copybuf((void *)tbuf,(void *)v,j>>2);

		vx5.globalmass -= v[1];
		while (v[0]) { v += v[0]*4; vx5.globalmass += v[3]-v[1]; }
	}
}

	//x: x on voxel map
	//y: y on voxel map
	//z0: highest z on column
	//z1: lowest z(+1) on column
	//nbuf: buffer of color data from nbuf[z0] to nbuf[z1-1];
	//           -3: don't modify voxel
	//           -2: solid voxel (unexposed): to be calculated in compilestack
	//           -1: write air voxel
	//   0-16777215: write solid voxel (exposed)
void scum (int x, int y, int z0, int z1, int *nbuf)
{
	int z, *mptr;

	if ((x|y)&(~(VSID-1))) return;

	if (y != scoy)
	{
		if (scoy >= 0)
		{
			scumline();
			while (scoy < y-1)
			{
				scx0 = 0x7fffffff; scx1 = 0x80000000;
				scoy++; scoym3 += SCPITCH; if (scoym3 == &radar[SCPITCH*9]) scoym3 = &radar[SCPITCH*6];
				scumline();
			}
			scoy++; scoym3 += SCPITCH; if (scoym3 == &radar[SCPITCH*9]) scoym3 = &radar[SCPITCH*6];
		}
		else
		{
			scoox0 = scox0 = 0x7fffffff;
			sceox0 = scex0 = x+1;
			sceox1 = scex1 = x;
			scoy = y; scoym3 = &radar[SCPITCH*6];
		}
		scx0 = x;
	}
	else
	{
		while (scx1 < x-1) { scx1++; expandstack(scx1,y,&scoym3[scx1*SCPITCH*3]); }
	}

	mptr = &scoym3[x*SCPITCH*3]; scx1 = x; expandstack(x,y,mptr);

		//Modify column (x,y):
	if (nbuf[MAXZDIM-1] == -1) nbuf[MAXZDIM-1] = -2; //Bottom voxel must be solid
	for(z=z0;z<z1;z++)
		if (nbuf[z] != -3) mptr[z] = nbuf[z];
}

void scumfinish ()
{
	int i;

	if (scoy == 0x80000000) return;
	for(i=2;i;i--)
	{
		scumline(); scx0 = 0x7fffffff; scx1 = 0x80000000;
		scoy++; scoym3 += SCPITCH; if (scoym3 == &radar[SCPITCH*9]) scoym3 = &radar[SCPITCH*6];
	}
	scumline(); scoy = 0x80000000;
}

	//Example of how to use this code:
	//vx5.colfunc = curcolfunc; //0<x0<x1<VSID, 0<y0<y1<VSID, 0<z0<z1<256,
	//clearbuf((void *)&templongbuf[z0],z1-z0,-1); //Ex: set all voxels to air
	//for(y=y0;y<y1;y++) //MUST iterate x&y in this order, but can skip around
	//   for(x=x0;x<x1;x++)
	//      if (rand()&8) scum(x,y,z0,z1,templongbuf));
	//scumfinish(); //MUST call this when done!

void scum2line ()
{
	int i, j, k, x, y, x0, x1, *mptr, *uptr;
	char *v;

	x0 = MIN(scox0-1,MIN(scx0,scoox0)); scoox0 = scox0; scox0 = scx0;
	x1 = MAX(scox1+1,MAX(scx1,scoox1)); scoox1 = scox1; scox1 = scx1;

	uptr = &scoym3[SCPITCH]; if (uptr == &radar[SCPITCH*9]) uptr = &radar[SCPITCH*6];
	mptr = &uptr[SCPITCH];   if (mptr == &radar[SCPITCH*9]) mptr = &radar[SCPITCH*6];

	if ((x1 < sceox0) || (x0 > sceox1))
	{
		for(x=x0;x<=x1;x++) expandrle(x,scoy-2,&uptr[x*SCPITCH*3]);
	}
	else
	{
		for(x=x0;x<sceox0;x++) expandrle(x,scoy-2,&uptr[x*SCPITCH*3]);
		for(x=x1;x>sceox1;x--) expandrle(x,scoy-2,&uptr[x*SCPITCH*3]);
	}

	if ((scex1|x1) >= 0)
	{
		for(x=x1+2;x<scex0;x++) expandrle(x,scoy-1,&mptr[x*SCPITCH*3]);
		for(x=x0-2;x>scex1;x--) expandrle(x,scoy-1,&mptr[x*SCPITCH*3]);
	}
	if ((x1+1 < scex0) || (x0-1 > scex1))
	{
		for(x=x0-1;x<=x1+1;x++) expandrle(x,scoy-1,&mptr[x*SCPITCH*3]);
	}
	else
	{
		for(x=x0-1;x<scex0;x++) expandrle(x,scoy-1,&mptr[x*SCPITCH*3]);
		for(x=x1+1;x>scex1;x--) expandrle(x,scoy-1,&mptr[x*SCPITCH*3]);
	}
	sceox0 = MIN(x0-1,scex0);
	sceox1 = MAX(x1+1,scex1);

	if ((x1 < scx0) || (x0 > scx1))
	{
		for(x=x0;x<=x1;x++) expandrle(x,scoy,&scoym3[x*SCPITCH*3]);
	}
	else
	{
		for(x=x0;x<scx0;x++) expandrle(x,scoy,&scoym3[x*SCPITCH*3]);
		for(x=x1;x>scx1;x--) expandrle(x,scoy,&scoym3[x*SCPITCH*3]);
	}
	scex0 = x0;
	scex1 = x1;

	y = scoy-1; if (y&(~(VSID-1))) return;
	if (x0 < 0) x0 = 0;
	if (x1 >= VSID) x1 = VSID-1;
	i = y*VSID+x0; k = x0*SCPITCH*3;
	for(x=x0;x<=x1;x++,i++,k+=SCPITCH*3)
	{
		j = compilerle(&mptr[k],&mptr[k-SCPITCH*3],&mptr[k+SCPITCH*3],&uptr[k],&scoym3[k],
							tbuf,x,y);

		v = sptr[i]; vx5.globalmass += v[1];
		while (v[0]) { v += v[0]*4; vx5.globalmass += v[1]-v[3]; }

			//De-allocate column (x,y)  Note: Must be AFTER compilerle!
		voxdealloc(sptr[i]);
			//Allocate & copy to new column (x,y)
		sptr[i] = v = voxalloc(j); copybuf((void *)tbuf,(void *)v,j>>2);

		vx5.globalmass -= v[1];
		while (v[0]) { v += v[0]*4; vx5.globalmass += v[3]-v[1]; }
	}
}

	//x: x on voxel map
	//y: y on voxel map
	//Returns pointer to rle column (x,y)
int *scum2 (int x, int y)
{
	int *mptr;
	unsigned int scp3=SCPITCH*3,scp6=SCPITCH*6,scp9=SCPITCH*9;

	if ((x|y)&(~(VSID-1))) return(0);

	if (y != scoy)
	{
		if (scoy >= 0)
		{
			scum2line();
			while (scoy < y-1)
			{
				scx0 = 0x7fffffff; scx1 = 0x80000000;
				++scoy; scoym3 += SCPITCH; if (scoym3 == &radar[scp9]) scoym3 = &radar[scp6];
				scum2line();
			}
			++scoy; scoym3 += SCPITCH; if (scoym3 == &radar[scp9]) scoym3 = &radar[scp6];
		}
		else
		{
			scoox0 = scox0 = 0x7fffffff;
			sceox0 = scex0 = x+1;
			sceox1 = scex1 = x;
			scoy = y; scoym3 = &radar[scp6];
		}
		scx0 = x;
	}
	else
	{
		while (scx1 < x-1) { ++scx1; expandrle(scx1,y,&scoym3[scx1*scp3]); }
	}

	mptr = scoym3+x*scp3; scx1 = x; expandrle(x,y,mptr);
	return(mptr);
}

void scum2finish ()
{
	int i;

	if (scoy == 0x80000000) return;
	for(i=2;i;i--)
	{
		scum2line(); scx0 = 0x7fffffff; scx1 = 0x80000000;
		scoy++; scoym3 += SCPITCH; if (scoym3 == &radar[SCPITCH*9]) scoym3 = &radar[SCPITCH*6];
	}
	scum2line(); scoy = 0x80000000;
}

//------------------- editing backup / restore begins ------------------------

void voxdontrestore ()
{
	int i;

	if (backedup == 1)
	{
		for(i=(bacx1-bacx0)*(bacy1-bacy0)-1;i>=0;i--) voxdealloc(bacsptr[i]);
	}
	backedup = -1;
}

void voxrestore ()
{
	int i, j, x, y;
	char *v, *daptr;

	if (backedup == 1)
	{
		i = 0;
		for(y=bacy0;y<bacy1;y++)
		{
			j = y*VSID;
			for(x=bacx0;x<bacx1;x++)
			{
				v = sptr[j+x]; vx5.globalmass += v[1];
				while (v[0]) { v += v[0]*4; vx5.globalmass += v[1]-v[3]; }

				voxdealloc(sptr[j+x]);
				sptr[j+x] = bacsptr[i]; i++;

				v = sptr[j+x]; vx5.globalmass -= v[1];
				while (v[0]) { v += v[0]*4; vx5.globalmass += v[3]-v[1]; }
			}
		}
		if (vx5.vxlmipuse > 1) genmipvxl(bacx0,bacy0,bacx1,bacy1);
	}
	else if (backedup == 2)
	{
		daptr = (char *)bacsptr;
		for(y=bacy0;y<bacy1;y++)
		{
			j = y*VSID;
			for(x=bacx0;x<bacx1;x++)
			{
				for(v=sptr[j+x];v[0];v+=v[0]*4)
					for(i=1;i<v[0];i++) v[(i<<2)+3] = *daptr++;
				for(i=1;i<=v[2]-v[1]+1;i++) v[(i<<2)+3] = *daptr++;
			}
		}
		if (vx5.vxlmipuse > 1) genmipvxl(bacx0,bacy0,bacx1,bacy1);
	}
	backedup = -1;
}

void voxbackup (int x0, int y0, int x1, int y1, int tag)
{
	int i, j, n, x, y;
	char *v, *daptr;

	voxdontrestore();

	x0 = MAX(x0-2,0); y0 = MAX(y0-2,0);
	x1 = MIN(x1+2,VSID); y1 = MIN(y1+2,VSID);
	if ((x1-x0)*(y1-y0) > 262144) return;

	bacx0 = x0; bacy0 = y0; bacx1 = x1; bacy1 = y1; backtag = tag;

	if (tag&0x10000)
	{
		backedup = 1;
		i = 0;
		for(y=bacy0;y<bacy1;y++)
		{
			j = y*VSID;
			for(x=bacx0;x<bacx1;x++)
			{
				bacsptr[i] = v = sptr[j+x]; i++;
				n = slng(v); sptr[j+x] = voxalloc(n);

				copybuf((void *)v,(void *)sptr[j+x],n>>2);
			}
		}
	}
	else if (tag&0x20000)
	{
		backedup = 2;
			//WARNING!!! Right now, function will crash if saving more than
			//   1<<20 colors :( This needs to be addressed!!!
		daptr = (char *)bacsptr;
		for(y=bacy0;y<bacy1;y++)
		{
			j = y*VSID;
			for(x=bacx0;x<bacx1;x++)
			{
				for(v=sptr[j+x];v[0];v+=v[0]*4)
					for(i=1;i<v[0];i++) *daptr++ = v[(i<<2)+3];
				for(i=1;i<=v[2]-v[1]+1;i++) *daptr++ = v[(i<<2)+3];
			}
		}
	}
	else backedup = 0;
}

//-------------------- editing backup / restore ends -------------------------

	//WARNING! Make sure to set vx5.colfunc before calling this function!
	//This function is here for simplicity only - it is NOT optimal.
	//
	//   -1: set air
	//   -2: use vx5.colfunc
void setcube (int px, int py, int pz, int col)
{
	int *bakcolptr, (*bakcolfunc)(lpoint3d *), *lptr;

	vx5.minx = px; vx5.maxx = px+1;
	vx5.miny = py; vx5.maxy = py+1;
	vx5.minz = pz; vx5.maxz = pz+1;
	if ((unsigned int)pz >= MAXZDIM) return;
	if ((unsigned int)col >= (unsigned int)0xfffffffe) //-1 or -2
	{
		lptr = scum2(px,py);
		if (col == -1) delslab(lptr,pz,pz+1); else insslab(lptr,pz,pz+1);
		scum2finish();
		updatebbox(vx5.minx,vx5.miny,vx5.minz,vx5.maxx,vx5.maxy,vx5.maxz,col);
		return;
	}

	bakcolptr = getcube(px,py,pz);
	if (bakcolptr == (int*)1) return; //Unexposed solid
	if (bakcolptr != NULL) //Not 0 (air)
		*bakcolptr = col;
	else
	{
		int bakcol;
		bakcolfunc = vx5.colfunc; bakcol = vx5.curcol;
		vx5.colfunc = curcolfunc; vx5.curcol = col;
		insslab(scum2(px,py),pz,pz+1); scum2finish();
		vx5.colfunc = bakcolfunc; vx5.curcol = bakcol;
	}
	updatebbox(vx5.minx,vx5.miny,vx5.minz,vx5.maxx,vx5.maxy,vx5.maxz,0);
}

void fast_setcube (int px, int py, int pz, int col)
{
	int *bakcolptr, (*bakcolfunc)(lpoint3d *), *lptr;
	if ((unsigned int)col >= (unsigned int)0xfffffffe) //-1 or -2
	{
		lptr = scum2(px,py);
		if (col == -1) delslab(lptr,pz,pz+1); else insslab(lptr,pz,pz+1);
		scum2finish();
		return;
	}

	bakcolptr = getcube(px,py,pz);
	if (bakcolptr == (int*)1) return; //Unexposed solid
	if (bakcolptr != NULL) //Not 0 (air)
		*bakcolptr = col;
	else
	{
		int bakcol;
		bakcolfunc = vx5.colfunc; bakcol = vx5.curcol;
		vx5.colfunc = curcolfunc; vx5.curcol = col;
		insslab(scum2(px,py),pz,pz+1); scum2finish();
		vx5.colfunc = bakcolfunc; vx5.curcol = bakcol;
	}
}

//-------------------------- SETCOLUMN CODE ENDS ----------------------------

static int64_t qmulmip[8] =
{
	0x7fff7fff7fff7fff,0x4000400040004000,0x2aaa2aaa2aaa2aaa,0x2000200020002000,
	0x1999199919991999,0x1555155515551555,0x1249124912491249,0x1000100010001000
};
static int mixc[MAXZDIM>>1][8]; //4K
static int mixn[MAXZDIM>>1];    //0.5K
void genmipvxl (int x0, int y0, int x1, int y1)
{
	int i, n, oldn, x, y, z, xsiz, ysiz, zsiz, oxsiz, oysiz;
	int cz, oz, nz, zz, besti, cstat, curz[4], curzn[4][4], mipnum, mipmax;
	char *v[4], *tv, **sr, **sw, **ssr, **ssw;

	if ((!(x0|y0)) && (x1 == VSID) && (y1 == VSID)) mipmax = vx5.vxlmipuse;
															 else mipmax = gmipnum;
	if (mipmax <= 0) return;
	mipnum = 1;

	vx5.colfunc = curcolfunc;
	xsiz = VSID; ysiz = VSID; zsiz = MAXZDIM;
	ssr = sptr; ssw = sptr+xsiz*ysiz;
	while ((xsiz > 1) && (ysiz > 1) && (zsiz > 1) && (mipnum < mipmax))
	{
		oxsiz = xsiz; xsiz >>= 1;
		oysiz = ysiz; ysiz >>= 1;
				      zsiz >>= 1;

		x0--; if (x0 < 0) x0 = 0;
		y0--; if (y0 < 0) y0 = 0;
		x1++; if (x1 > VSID) x1 = VSID;
		y1++; if (y1 > VSID) y1 = VSID;

		x0 >>= 1; x1 = ((x1+1)>>1);
		y0 >>= 1; y1 = ((y1+1)>>1);
		for(y=y0;y<y1;y++)
		{
			sr = ssr+oxsiz*(y<<1)+(x0<<1);
			sw = ssw+xsiz*y+x0;
			for(x=x0;x<x1;x++)
			{
					//ÚÄÄÄÂÄÄÄÂÄÄÄÂÄÄÄ¿
					//³npt³z1 ³z1c³dum³
					//³ b ³ g ³ r ³ i ³
					//³ b ³ g ³ r ³ i ³
					//³npt³z1 ³z1c³z0 ³
					//³ b ³ g ³ r ³ i ³
					//ÀÄÄÄÁÄÄÄÁÄÄÄÁÄÄÄÙ
				v[0] = sr[      0];
				v[1] = sr[      1];
				v[2] = sr[oysiz  ];
				v[3] = sr[oysiz+1];
				for(i=3;i>=0;i--)
				{
					curz[i] = curzn[i][0] = (int)v[i][1];
					curzn[i][1] = ((int)v[i][2])+1;

					tv = v[i];
					while (1)
					{
						oz = (int)tv[1];
						for(z=oz;z<=((int)tv[2]);z++)
						{
							nz = (z>>1);
							mixc[nz][mixn[nz]++] = *(int *)(&tv[((z-oz)<<2)+4]);
						}
						z = (z-oz) - (((int)tv[0])-1);
						if (!tv[0]) break;
						tv += (((int)tv[0])<<2);
						oz = (int)tv[3];
						for(;z<0;z++)
						{
							nz = ((z+oz)>>1);
							mixc[nz][mixn[nz]++] = *(int *)(&tv[z<<2]);
						}
					}
				}
				cstat = 0; oldn = 0; n = 4; tbuf[3] = 0; z = 0x80000000;
				while (1)
				{
					oz = z;

						//z,besti = min,argMIN(curz[0],curz[1],curz[2],curz[3])
					besti = (((unsigned int)(curz[1]-curz[    0]))>>31);
						 i = (((unsigned int)(curz[3]-curz[    2]))>>31)+2;
					besti +=(((( signed int)(curz[i]-curz[besti]))>>31)&(i-besti));
					z = curz[besti]; if (z >= MAXZDIM) break;

					if ((!cstat) && ((z>>1) >= ((oz+1)>>1)))
					{
						if (oz >= 0)
						{
							tbuf[oldn] = ((n-oldn)>>2);
							tbuf[oldn+2]--;
							tbuf[n+3] = ((oz+1)>>1);
							oldn = n; n += 4;
						}
						tbuf[oldn] = 0;
						tbuf[oldn+1] = tbuf[oldn+2] = (z>>1); cz = -1;
					}
					if (cstat&0x1111)
					{
						if (((((int)tbuf[oldn+2])<<1)+1 >= oz) && (cz < 0))
						{
							while ((((int)tbuf[oldn+2])<<1) < z)
							{
								zz = (int)tbuf[oldn+2];
								*(int *)&tbuf[n] = mixc[zz][rand()%mixn[zz]];
								mixn[zz] = 0;
								tbuf[oldn+2]++; n += 4;
							}
						}
						else
						{
							if (cz < 0) cz = (oz>>1);
							else if ((cz<<1)+1 < oz)
							{
									//Insert fake slab
								tbuf[oldn] = ((n-oldn)>>2);
								tbuf[oldn+2]--;
								tbuf[n] = 0;
								tbuf[n+1] = tbuf[n+2] = tbuf[n+3] = cz;
								oldn = n; n += 4;
								cz = (oz>>1);
							}
							while ((cz<<1) < z)
							{
								*(int *)&tbuf[n] = mixc[cz][rand()%mixn[cz]];
								mixn[cz] = 0;
								cz++; n += 4;
							}
						}
					}

					i = (besti<<2);
					cstat = (((1<<i)+cstat)&0x3333); //--33--22--11--00
					switch ((cstat>>i)&3)
					{
						case 0: curz[besti] = curzn[besti][0]; break;
						case 1: curz[besti] = curzn[besti][1]; break;
						case 2:
							if (!(v[besti][0])) { curz[besti] = MAXZDIM; }
							else
							{
								tv = v[besti]; i = (((int)tv[2])-((int)tv[1])+1)-(((int)tv[0])-1);
								tv += (((int)tv[0])<<2);
								curz[besti] = ((int)(tv[3])) + i;
								curzn[besti][3] = (int)(tv[3]);
								curzn[besti][0] = (int)(tv[1]);
								curzn[besti][1] = ((int)tv[2])+1;
								v[besti] = tv;
							}
							break;
						case 3: curz[besti] = curzn[besti][3]; break;
						//default: _gtfo(); //tells MSVC default can't be reached
					}
				}
				tbuf[oldn+2]--;
				if (cz >= 0)
				{
					tbuf[oldn] = ((n-oldn)>>2);
					tbuf[n] = 0;
					tbuf[n+1] = tbuf[n+3] = cz;
					tbuf[n+2] = cz-1;
					n += 4;
				}

					//De-allocate column (x,y) if it exists
				if (sw[0]) voxdealloc(sw[0]);

					//Allocate & copy to new column (x,y)
				sw[0] = voxalloc(n);
				copybuf((void *)tbuf,(void *)sw[0],n>>2);
				sw++; sr += 2;
			}
			sr += ysiz*2;
		}
		ssr = ssw; ssw += xsiz*ysiz;
		mipnum++; if (mipnum > gmipnum) gmipnum = mipnum;
	}

		//Remove extra mips (bbox must be 0,0,VSID,VSID to get inside this)
	while ((xsiz > 1) && (ysiz > 1) && (zsiz > 1) && (mipnum < gmipnum))
	{
		xsiz >>= 1; ysiz >>= 1; zsiz >>= 1;
		for(i=xsiz*ysiz;i>0;i--)
		{
			if (ssw[0]) voxdealloc(ssw[0]); //De-allocate column if it exists
			ssw++;
		}
		gmipnum--;
	}
	if ((!(x0|y0)) && (x1 == VSID) && (y1 == VSID))
		vx5.vxlmipuse=gmipnum;
}

void setsphere (lpoint3d *hit, int hitrad, int dacol)
{
	void (*modslab)(int *, int, int);
	int i, x, y, xs, ys, zs, xe, ye, ze, sq;
	float f, ff;

	xs = MAX(hit->x-hitrad,0); xe = MIN(hit->x+hitrad,VSID-1);
	ys = MAX(hit->y-hitrad,0); ye = MIN(hit->y+hitrad,VSID-1);
	zs = MAX(hit->z-hitrad,0); ze = MIN(hit->z+hitrad,MAXZDIM-1);
	vx5.minx = xs; vx5.maxx = xe+1;
	vx5.miny = ys; vx5.maxy = ye+1;
	vx5.minz = zs; vx5.maxz = ze+1;
	if ((xs > xe) || (ys > ye) || (zs > ze)) return;

	if (vx5.colfunc == sphcolfunc)
	{
		vx5.cen = hit->x+hit->y+hit->z;
		vx5.daf = 1.f/(hitrad*sqrt(3.f));
	}

	if (hitrad >= SETSPHMAXRAD-1) hitrad = SETSPHMAXRAD-2;
	if (dacol == -1) modslab = delslab; else modslab = insslab;

	tempfloatbuf[0] = 0.0f;
#if 0
		//Totally unoptimized
	for(i=1;i<=hitrad;i++) tempfloatbuf[i] = pow((float)i,vx5.curpow);
#else
	tempfloatbuf[1] = 1.0f;
	for(i=2;i<=hitrad;i++)
	{
		if (!factr[i][0]) tempfloatbuf[i] = exp(logint[i]*vx5.curpow);
		else tempfloatbuf[i] = tempfloatbuf[factr[i][0]]*tempfloatbuf[factr[i][1]];
	}
#endif
	*(int *)&tempfloatbuf[hitrad+1] = 0x7f7fffff; //3.4028235e38f; //Highest float

	sq = 0; //pow(fabs(x-hit->x),vx5.curpow) + "y + "z < pow(vx5.currad,vx5.curpow)
	for(y=ys;y<=ye;y++)
	{
		ff = tempfloatbuf[hitrad]-tempfloatbuf[labs(y-hit->y)];
		if (*(int *)&ff <= 0) continue;
		for(x=xs;x<=xe;x++)
		{
			f = ff-tempfloatbuf[labs(x-hit->x)]; if (*(int *)&f <= 0) continue;
			while (*(int *)&tempfloatbuf[sq] <  *(int *)&f) sq++;
			while (*(int *)&tempfloatbuf[sq] >= *(int *)&f) sq--;
			modslab(scum2(x,y),MAX(hit->z-sq,zs),MIN(hit->z+sq+1,ze));
		}
	}
	scum2finish();
	updatebbox(vx5.minx,vx5.miny,vx5.minz,vx5.maxx,vx5.maxy,vx5.maxz,dacol);
}

void setellipsoid (lpoint3d *hit, lpoint3d *hit2, int hitrad, int dacol, int bakit)
{
	void (*modslab)(int *, int, int);
	int x, y, xs, ys, zs, xe, ye, ze;
	float a, b, c, d, e, f, g, h, r, t, u, Za, Zb, fx0, fy0, fz0, fx1, fy1, fz1;

	xs = MIN(hit->x,hit2->x)-hitrad; xs = MAX(xs,0);
	ys = MIN(hit->y,hit2->y)-hitrad; ys = MAX(ys,0);
	zs = MIN(hit->z,hit2->z)-hitrad; zs = MAX(zs,0);
	xe = MAX(hit->x,hit2->x)+hitrad; xe = MIN(xe,VSID-1);
	ye = MAX(hit->y,hit2->y)+hitrad; ye = MIN(ye,VSID-1);
	ze = MAX(hit->z,hit2->z)+hitrad; ze = MIN(ze,MAXZDIM-1);
	vx5.minx = xs; vx5.maxx = xe+1;
	vx5.miny = ys; vx5.maxy = ye+1;
	vx5.minz = zs; vx5.maxz = ze+1;
	if ((xs > xe) || (ys > ye) || (zs > ze))
		{ if (bakit) voxbackup(xs,ys,xs,ys,bakit); return; }

	fx0 = (float)hit->x; fy0 = (float)hit->y; fz0 = (float)hit->z;
	fx1 = (float)hit2->x; fy1 = (float)hit2->y; fz1 = (float)hit2->z;

	r = (fx1-fx0)*(fx1-fx0) + (fy1-fy0)*(fy1-fy0) + (fz1-fz0)*(fz1-fz0);
	r = sqrt((float)hitrad*(float)hitrad + r*.25);
	c = fz0*fz0 - fz1*fz1; d = r*r*-4; e = d*4;
	f = c*c + fz1*fz1 * e; g = c + c; h = (fz1-fz0)*2; c = c*h - fz1*e;
	Za = -h*h - e; if (Za <= 0) { if (bakit) voxbackup(xs,ys,xs,ys,bakit); return; }
	u = 1 / Za;

	if (vx5.colfunc == jitcolfunc) vx5.amount = 0x70707;

	if (dacol == -1) modslab = delslab; else modslab = insslab;

	if (bakit) voxbackup(xs,ys,xe+1,ye+1,bakit);

	for(y=ys;y<=ye;y++)
		for(x=xs;x<=xe;x++)
		{
			a = (x-fx0)*(x-fx0) + (y-fy0)*(y-fy0);
			b = (x-fx1)*(x-fx1) + (y-fy1)*(y-fy1);
			t = a-b+d; Zb = t*h + c;
			t = ((t+g)*t + b*e + f)*Za + Zb*Zb; if (t <= 0) continue;
			t = sqrt(t);
			ftol((Zb - t)*u,&zs); if (zs < 0) zs = 0;
			ftol((Zb + t)*u,&ze); if (ze > MAXZDIM) ze = MAXZDIM;
			modslab(scum2(x,y),zs,ze);
		}
	scum2finish();
	updatebbox(vx5.minx,vx5.miny,vx5.minz,vx5.maxx,vx5.maxy,vx5.maxz,dacol);
}

	//Draws a cylinder, given: 2 points, a radius, and a color
	//Code mostly optimized - original code from CYLINDER.BAS:drawcylinder
void setcylinder (lpoint3d *p0, lpoint3d *p1, int cr, int dacol, int bakit)
{
	void (*modslab)(int *, int, int);

	float t, ax, ay, az, bx, by, bz, cx, cy, cz, ux, uy, uz, vx, vy, vz;
	float Za, Zb, Zc, tcr, xxyy, rcz, rZa;
	float fx, fxi, xof, vx0, vy0, vz0, vz0i, vxo, vyo, vzo;
	int i, j, ix, iy, ix0, ix1, iz0, iz1, minx, maxx, miny, maxy;
	int x0, y0, z0, x1, y1, z1;

		//Map generic cylinder into unit space:  (0,0,0), (0,0,1), cr = 1
		//   x*x + y*y < 1, z >= 0, z < 1
	if (p0->z > p1->z)
	{
		x0 = p1->x; y0 = p1->y; z0 = p1->z;
		x1 = p0->x; y1 = p0->y; z1 = p0->z;
	}
	else
	{
		x0 = p0->x; y0 = p0->y; z0 = p0->z;
		x1 = p1->x; y1 = p1->y; z1 = p1->z;
	}

	xxyy = (float)((x1-x0)*(x1-x0)+(y1-y0)*(y1-y0));
	t = xxyy + (float)(z1-z0)*(z1-z0);
	if ((t == 0) || (cr == 0))
	{
		vx5.minx = x0; vx5.maxx = x0+1;
		vx5.miny = y0; vx5.maxy = y0+1;
		vx5.minz = z0; vx5.maxz = z0+1;
		if (bakit) voxbackup(x0,y0,x0,y0,bakit);
		return;
	}
	t = 1 / t; cx = ((float)(x1-x0))*t; cy = ((float)(y1-y0))*t; cz = ((float)(z1-z0))*t;
	t = sqrt(t); ux = ((float)(x1-x0))*t; uy = ((float)(y1-y0))*t; uz = ((float)(z1-z0))*t;

	if (vx5.colfunc == jitcolfunc) vx5.amount = 0x70707;

	if (dacol == -1) modslab = delslab; else modslab = insslab;

	if (xxyy == 0)
	{
		iz0 = MAX(z0,0); iz1 = MIN(z1,MAXZDIM);
		minx = MAX(x0-cr,0); maxx = MIN(x0+cr,VSID-1);
		miny = MAX(y0-cr,0); maxy = MIN(y0+cr,VSID-1);

		vx5.minx = minx; vx5.maxx = maxx+1;
		vx5.miny = miny; vx5.maxy = maxy+1;
		vx5.minz = iz0; vx5.maxz = iz1;
		if (bakit) voxbackup(minx,miny,maxx+1,maxy+1,bakit);

		j = cr*cr;
		for(iy=miny;iy<=maxy;iy++)
		{
			i = j-(iy-y0)*(iy-y0);
			for(ix=minx;ix<=maxx;ix++)
				if ((ix-x0)*(ix-x0) < i) modslab(scum2(ix,iy),iz0,iz1);
		}
		scum2finish();
		updatebbox(vx5.minx,vx5.miny,vx5.minz,vx5.maxx,vx5.maxy,vx5.maxz,dacol);
		return;
	}

	if (x0 < x1) { minx = x0; maxx = x1; } else { minx = x1; maxx = x0; }
	if (y0 < y1) { miny = y0; maxy = y1; } else { miny = y1; maxy = y0; }
	tcr = cr / sqrt(xxyy); vx = fabs((float)(x1-x0))*tcr; vy = fabs((float)(y1-y0))*tcr;
	t = vx*uz + vy;
	ftol((float)minx-t,&minx); if (minx < 0) minx = 0;
	ftol((float)maxx+t,&maxx); if (maxx >= VSID) maxx = VSID-1;
	t = vy*uz + vx;
	ftol((float)miny-t,&miny); if (miny < 0) miny = 0;
	ftol((float)maxy+t,&maxy); if (maxy >= VSID) maxy = VSID-1;

	vx5.minx = minx; vx5.maxx = maxx+1;
	vx5.miny = miny; vx5.maxy = maxy+1;
	vx5.minz = z0-cr; vx5.maxz = z1+cr+1;
	if (bakit) voxbackup(minx,miny,maxx+1,maxy+1,bakit);

	vx = (fabs(ux) < fabs(uy)); vy = 1.0f-vx; vz = 0;
	ax = uy*vz - uz*vy; ay = uz*vx - ux*vz; az = ux*vy - uy*vx;
	t = 1.0 / (sqrt(ax*ax + ay*ay + az*az)*cr);
	ax *= t; ay *= t; az *= t;
	bx = ay*uz - az*uy; by = az*ux - ax*uz; bz = ax*uy - ay*ux;

	Za = az*az + bz*bz; rZa = 1.0f / Za;
	if (cz != 0) { rcz = 1.0f / cz; vz0i = -rcz*cx; }
	if (y0 != y1)
	{
		t = 1.0f / ((float)(y1-y0)); fxi = ((float)(x1-x0))*t;
		fx = ((float)miny-y0)*fxi + x0; xof = fabs(tcr*xxyy*t);
	}
	else { fx = (float)minx; fxi = 0.0; xof = (float)(maxx-minx); }

	vy = (float)(miny-y0);
	vxo = vy*ay - z0*az;
	vyo = vy*by - z0*bz;
	vzo = vy*cy - z0*cz;
	for(iy=miny;iy<=maxy;iy++)
	{
		ftol(fx-xof,&ix0); if (ix0 < minx) ix0 = minx;
		ftol(fx+xof,&ix1); if (ix1 > maxx) ix1 = maxx;
		fx += fxi;

		vx = (float)(ix0-x0);
		vx0 = vx*ax + vxo; vxo += ay;
		vy0 = vx*bx + vyo; vyo += by;
		vz0 = vx*cx + vzo; vzo += cy;

		if (cz != 0)   //(vx0 + vx1*t)ý + (vy0 + vy1*t)ý = 1
		{
			vz0 *= -rcz;
			for(ix=ix0;ix<=ix1;ix++,vx0+=ax,vy0+=bx,vz0+=vz0i)
			{
				Zb = vx0*az + vy0*bz; Zc = vx0*vx0 + vy0*vy0 - 1;
				t = Zb*Zb - Za*Zc; if (*(int *)&t <= 0) continue; t = sqrt(t);
				ftol(MAX((-Zb-t)*rZa,vz0    ),&iz0); if (iz0 < 0) iz0 = 0;
				ftol(MIN((-Zb+t)*rZa,vz0+rcz),&iz1); if (iz1 > MAXZDIM) iz1 = MAXZDIM;
				modslab(scum2(ix,iy),iz0,iz1);
			}
		}
		else
		{
			for(ix=ix0;ix<=ix1;ix++,vx0+=ax,vy0+=bx,vz0+=cx)
			{
				if (*(unsigned int *)&vz0 >= 0x3f800000) continue; //vz0<0||vz0>=1
				Zb = vx0*az + vy0*bz; Zc = vx0*vx0 + vy0*vy0 - 1;
				t = Zb*Zb - Za*Zc; if (*(int *)&t <= 0) continue; t = sqrt(t);
				ftol((-Zb-t)*rZa,&iz0); if (iz0 < 0) iz0 = 0;
				ftol((-Zb+t)*rZa,&iz1); if (iz1 > MAXZDIM) iz1 = MAXZDIM;
				modslab(scum2(ix,iy),iz0,iz1);
			}
		}
	}
	scum2finish();
	updatebbox(vx5.minx,vx5.miny,vx5.minz,vx5.maxx,vx5.maxy,vx5.maxz,dacol);
}

	//Draws a rectangle, given: 2 points as opposite corners, and a color
void setrect (lpoint3d *hit, lpoint3d *hit2, int dacol)
{
	int x, y, xs, ys, zs, xe, ye, ze;

		//WARNING: do NOT use lbound because 'c' not guaranteed to be >= 'b'
	xs = MAX(MIN(hit->x,hit2->x),0); xe = MIN(MAX(hit->x,hit2->x),VSID-1);
	ys = MAX(MIN(hit->y,hit2->y),0); ye = MIN(MAX(hit->y,hit2->y),VSID-1);
	zs = MAX(MIN(hit->z,hit2->z),0); ze = MIN(MAX(hit->z,hit2->z),MAXZDIM-1);
	vx5.minx = xs; vx5.maxx = xe+1;
	vx5.miny = ys; vx5.maxy = ye+1;
	vx5.minz = zs; vx5.maxz = ze+1;
	if ((xs > xe) || (ys > ye) || (zs > ze)) return;

	if (vx5.colfunc == jitcolfunc) vx5.amount = 0x70707;

	ze++;
	if (dacol == -1)
	{
		for(y=ys;y<=ye;y++)
			for(x=xs;x<=xe;x++)
				delslab(scum2(x,y),zs,ze);
	}
	else
	{
		for(y=ys;y<=ye;y++)
			for(x=xs;x<=xe;x++)
				insslab(scum2(x,y),zs,ze);
	}
	scum2finish();
	updatebbox(vx5.minx,vx5.miny,vx5.minz,vx5.maxx,vx5.maxy,vx5.maxz,dacol);
}

	//Does CSG using pre-sorted spanlist
void setspans (vspans *lst, int lstnum, lpoint3d *offs, int dacol)
{
	void (*modslab)(int *, int, int);
	int i, j, x, y, z0, z1, *lptr;
	char ox, oy;

	if (lstnum <= 0) return;
	if (dacol == -1) modslab = delslab; else modslab = insslab;
	vx5.minx = vx5.maxx = ((int)lst[0].x)+offs->x;
	vx5.miny = ((int)lst[       0].y)+offs->y;
	vx5.maxy = ((int)lst[lstnum-1].y)+offs->y+1;
	vx5.minz = vx5.maxz = ((int)lst[0].z0)+offs->z;

	i = 0; goto in2setlist;
	do
	{
		if ((ox != lst[i].x) || (oy != lst[i].y))
		{
in2setlist:;
			ox = lst[i].x; oy = lst[i].y;
			x = ((int)lst[i].x)+offs->x;
			y = ((int)lst[i].y)+offs->y;
				  if (x < vx5.minx) vx5.minx = x;
			else if (x > vx5.maxx) vx5.maxx = x;
			lptr = scum2(x,y);
		}
		if ((x|y)&(~(VSID-1))) { i++; continue; }
		z0 = ((int)lst[i].z0)+offs->z;   if (z0 < 0) z0 = 0;
		z1 = ((int)lst[i].z1)+offs->z+1; if (z1 > MAXZDIM) z1 = MAXZDIM;
		if (z0 < vx5.minz) vx5.minz = z0;
		if (z1 > vx5.maxz) vx5.maxz = z1;
		modslab(lptr,z0,z1);
		i++;
	} while (i < lstnum);
	vx5.maxx++; vx5.maxz++;
	if (vx5.minx < 0) vx5.minx = 0;
	if (vx5.miny < 0) vx5.miny = 0;
	if (vx5.maxx > VSID) vx5.maxx = VSID;
	if (vx5.maxy > VSID) vx5.maxy = VSID;

	scum2finish();
	updatebbox(vx5.minx,vx5.miny,vx5.minz,vx5.maxx,vx5.maxy,vx5.maxz,dacol);
}

void setheightmap (const unsigned char *hptr, int hpitch, int hxdim, int hydim,
						 int x0, int y0, int x1, int y1)
{
	int x, y, su, sv, u, v;

	if (x0 < 0) x0 = 0;
	if (y0 < 0) y0 = 0;
	if (x1 > VSID) x1 = VSID;
	if (y1 > VSID) y1 = VSID;
	vx5.minx = x0; vx5.maxx = x1;
	vx5.miny = y0; vx5.maxy = y1;
	vx5.minz = 0; vx5.maxz = MAXZDIM;
	if ((x0 >= x1) || (y0 >= y1)) return;

	su = x0%hxdim; sv = y0%hydim;
	for(y=y0,v=sv;y<y1;y++)
	{
		for(x=x0,u=su;x<x1;x++)
		{
			insslab(scum2(x,y),hptr[v*hpitch+u],MAXZDIM);
			u++; if (u >= hxdim) u = 0;
		}
		v++; if (v >= hydim) v = 0;
	}
	scum2finish();
	updatebbox(vx5.minx,vx5.miny,vx5.minz,vx5.maxx,vx5.maxy,vx5.maxz,0);
}

static int min0[VSID], max0[VSID]; //MAXY
static int min1[VSID], max1[VSID]; //MAXX
static int min2[VSID], max2[VSID]; //MAXY

static void canseerange (point3d *p0, point3d *p1)
{
	lpoint3d a, c, d, p, i;
	point3d f, g;
	int cnt, j;

	ftol(p0->x-.5,&a.x); ftol(p0->y-.5,&a.y); ftol(p0->z-.5,&a.z);
	ftol(p1->x-.5,&c.x); ftol(p1->y-.5,&c.y); ftol(p1->z-.5,&c.z);
	cnt = 0;

		  if (c.x <  a.x) { d.x = -1; f.x = p0->x-a.x;   g.x = (p0->x-p1->x)*1024; cnt += a.x-c.x; }
	else if (c.x != a.x) { d.x =  1; f.x = a.x+1-p0->x; g.x = (p1->x-p0->x)*1024; cnt += c.x-a.x; }
	else f.x = g.x = 0;
		  if (c.y <  a.y) { d.y = -1; f.y = p0->y-a.y;   g.y = (p0->y-p1->y)*1024; cnt += a.y-c.y; }
	else if (c.y != a.y) { d.y =  1; f.y = a.y+1-p0->y; g.y = (p1->y-p0->y)*1024; cnt += c.y-a.y; }
	else f.y = g.y = 0;
		  if (c.z <  a.z) { d.z = -1; f.z = p0->z-a.z;   g.z = (p0->z-p1->z)*1024; cnt += a.z-c.z; }
	else if (c.z != a.z) { d.z =  1; f.z = a.z+1-p0->z; g.z = (p1->z-p0->z)*1024; cnt += c.z-a.z; }
	else f.z = g.z = 0;

	ftol(f.x*g.z - f.z*g.x,&p.x); ftol(g.x,&i.x);
	ftol(f.y*g.z - f.z*g.y,&p.y); ftol(g.y,&i.y);
	ftol(f.y*g.x - f.x*g.y,&p.z); ftol(g.z,&i.z);
	for(;cnt;cnt--)
	{
			//use a.x, a.y, a.z
		if (a.x < min0[a.y]) min0[a.y] = a.x;
		if (a.x > max0[a.y]) max0[a.y] = a.x;
		if (a.z < min1[a.x]) min1[a.x] = a.z;
		if (a.z > max1[a.x]) max1[a.x] = a.z;
		if (a.z < min2[a.y]) min2[a.y] = a.z;
		if (a.z > max2[a.y]) max2[a.y] = a.z;

		if (((p.x|p.y) >= 0) && (a.z != c.z)) { a.z += d.z; p.x -= i.x; p.y -= i.y; }
		else if ((p.z >= 0) && (a.x != c.x))  { a.x += d.x; p.x += i.z; p.z -= i.y; }
		else                                  { a.y += d.y; p.y += i.z; p.z += i.x; }
	}
}

void settri (point3d *p0, point3d *p1, point3d *p2, int bakit)
{
	point3d n;
	float f, x0, y0, z0, x1, y1, z1, rx, ry, k0, k1;
	int i, x, y, z, iz0, iz1, minx, maxx, miny, maxy;

	if (p0->x < p1->x) { x0 = p0->x; x1 = p1->x; } else { x0 = p1->x; x1 = p0->x; }
	if (p2->x < x0) x0 = p2->x;
	if (p2->x > x1) x1 = p2->x;
	if (p0->y < p1->y) { y0 = p0->y; y1 = p1->y; } else { y0 = p1->y; y1 = p0->y; }
	if (p2->y < y0) y0 = p2->y;
	if (p2->y > y1) y1 = p2->y;
	if (p0->z < p1->z) { z0 = p0->z; z1 = p1->z; } else { z0 = p1->z; z1 = p0->z; }
	if (p2->z < z0) z0 = p2->z;
	if (p2->z > z1) z1 = p2->z;

	ftol(x0-.5,&minx); ftol(y0-.5,&miny);
	ftol(x1-.5,&maxx); ftol(y1-.5,&maxy);
	vx5.minx = minx; vx5.maxx = maxx+1;
	vx5.miny = miny; vx5.maxy = maxy+1;
	ftol(z0-.5,&vx5.minz); ftol(z1+.5,&vx5.maxz);
	if (bakit) voxbackup(minx,miny,maxx+1,maxy+1,bakit);

	for(i=miny;i<=maxy;i++) { min0[i] = 0x7fffffff; max0[i] = 0x80000000; }
	for(i=minx;i<=maxx;i++) { min1[i] = 0x7fffffff; max1[i] = 0x80000000; }
	for(i=miny;i<=maxy;i++) { min2[i] = 0x7fffffff; max2[i] = 0x80000000; }

	canseerange(p0,p1);
	canseerange(p1,p2);
	canseerange(p2,p0);

	n.x = (p1->z-p0->z)*(p2->y-p1->y) - (p1->y-p0->y) * (p2->z-p1->z);
	n.y = (p1->x-p0->x)*(p2->z-p1->z) - (p1->z-p0->z) * (p2->x-p1->x);
	n.z = (p1->y-p0->y)*(p2->x-p1->x) - (p1->x-p0->x) * (p2->y-p1->y);
	f = 1.0 / sqrt(n.x*n.x + n.y*n.y + n.z*n.z); if (n.z < 0) f = -f;
	n.x *= f; n.y *= f; n.z *= f;

	if (n.z > .01)
	{
		f = -1.0 / n.z; rx = n.x*f; ry = n.y*f;
		k0 = ((n.x>=0)-p0->x)*rx + ((n.y>=0)-p0->y)*ry - ((n.z>=0)-p0->z) + .5;
		k1 = ((n.x< 0)-p0->x)*rx + ((n.y< 0)-p0->y)*ry - ((n.z< 0)-p0->z) - .5;
	}
	else { rx = 0; ry = 0; k0 = -2147000000.0; k1 = 2147000000.0; }

	for(y=miny;y<=maxy;y++)
		for(x=min0[y];x<=max0[y];x++)
		{
			f = (float)x*rx + (float)y*ry; ftol(f+k0,&iz0); ftol(f+k1,&iz1);
			if (iz0 < min1[x]) iz0 = min1[x];
			if (iz1 > max1[x]) iz1 = max1[x];
			if (iz0 < min2[y]) iz0 = min2[y];
			if (iz1 > max2[y]) iz1 = max2[y];

				//set: (x,y,iz0) to (x,y,iz1) (inclusive)
			insslab(scum2(x,y),iz0,iz1+1);
	}
	scum2finish();
	updatebbox(vx5.minx,vx5.miny,vx5.minz,vx5.maxx,vx5.maxy,vx5.maxz,0);
}

	//Known problems:
	//1. Need to test faces for intersections on p1<->p2 line (not just edges)
	//2. Doesn't guarantee that hit point/line is purely air (but very close)
	//3. Piescan is more useful for parts of rope code :/
static int tripind[24] = {0,4,1,5,2,6,3,7,0,2,1,3,4,6,5,7,0,1,2,3,4,5,6,7};
int triscan (point3d *p0, point3d *p1, point3d *p2, point3d *hit, lpoint3d *lhit)
{
	point3d n, d[8], cp2;
	float f, g, x0, x1, y0, y1, rx, ry, k0, k1, fx, fy, fz, pval[8];
	int i, j, k, x, y, z, iz0, iz1, minx, maxx, miny, maxy, didhit;

	didhit = 0;

	if (p0->x < p1->x) { x0 = p0->x; x1 = p1->x; } else { x0 = p1->x; x1 = p0->x; }
	if (p2->x < x0) x0 = p2->x;
	if (p2->x > x1) x1 = p2->x;
	if (p0->y < p1->y) { y0 = p0->y; y1 = p1->y; } else { y0 = p1->y; y1 = p0->y; }
	if (p2->y < y0) y0 = p2->y;
	if (p2->y > y1) y1 = p2->y;
	ftol(x0-.5,&minx); ftol(y0-.5,&miny);
	ftol(x1-.5,&maxx); ftol(y1-.5,&maxy);
	for(i=miny;i<=maxy;i++) { min0[i] = 0x7fffffff; max0[i] = 0x80000000; }
	for(i=minx;i<=maxx;i++) { min1[i] = 0x7fffffff; max1[i] = 0x80000000; }
	for(i=miny;i<=maxy;i++) { min2[i] = 0x7fffffff; max2[i] = 0x80000000; }

	canseerange(p0,p1);
	canseerange(p1,p2);
	canseerange(p2,p0);

	n.x = (p1->z-p0->z)*(p2->y-p1->y) - (p1->y-p0->y) * (p2->z-p1->z);
	n.y = (p1->x-p0->x)*(p2->z-p1->z) - (p1->z-p0->z) * (p2->x-p1->x);
	n.z = (p1->y-p0->y)*(p2->x-p1->x) - (p1->x-p0->x) * (p2->y-p1->y);
	f = 1.0 / sqrt(n.x*n.x + n.y*n.y + n.z*n.z); if (n.z < 0) f = -f;
	n.x *= f; n.y *= f; n.z *= f;

	if (n.z > .01)
	{
		f = -1.0 / n.z; rx = n.x*f; ry = n.y*f;
		k0 = ((n.x>=0)-p0->x)*rx + ((n.y>=0)-p0->y)*ry - ((n.z>=0)-p0->z) + .5;
		k1 = ((n.x< 0)-p0->x)*rx + ((n.y< 0)-p0->y)*ry - ((n.z< 0)-p0->z) - .5;
	}
	else { rx = 0; ry = 0; k0 = -2147000000.0; k1 = 2147000000.0; }

	cp2.x = p2->x; cp2.y = p2->y; cp2.z = p2->z;

	for(y=miny;y<=maxy;y++)
		for(x=min0[y];x<=max0[y];x++)
		{
			f = (float)x*rx + (float)y*ry; ftol(f+k0,&iz0); ftol(f+k1,&iz1);
			if (iz0 < min1[x]) iz0 = min1[x];
			if (iz1 > max1[x]) iz1 = max1[x];
			if (iz0 < min2[y]) iz0 = min2[y];
			if (iz1 > max2[y]) iz1 = max2[y];
			for(z=iz0;z<=iz1;z++)
			{
				if (!isvoxelsolid(x,y,z)) continue;

				for(i=0;i<8;i++)
				{
					d[i].x = (float)(( i    &1)+x);
					d[i].y = (float)(((i>>1)&1)+y);
					d[i].z = (float)(((i>>2)&1)+z);
					pval[i] = (d[i].x-p0->x)*n.x + (d[i].y-p0->y)*n.y + (d[i].z-p0->z)*n.z;
				}
				for(i=0;i<24;i+=2)
				{
					j = tripind[i+0];
					k = tripind[i+1];
					if (((*(int *)&pval[j])^(*(int *)&pval[k])) < 0)
					{
						f = pval[j]/(pval[j]-pval[k]);
						fx = (d[k].x-d[j].x)*f + d[j].x;
						fy = (d[k].y-d[j].y)*f + d[j].y;
						fz = (d[k].z-d[j].z)*f + d[j].z;

							//         (p0->x,p0->y,p0->z)
							//             _|     |_
							//           _|     .   |_
							//         _|  (fx,fy,fz) |_
							//       _|                 |_
							//(p1->x,p1->y,p1->z)-.----(cp2.x,cp2.y,cp2.z)

						if ((fabs(n.z) > fabs(n.x)) && (fabs(n.z) > fabs(n.y)))
						{ //x,y
						  // ix = p1->x + (cp2.x-p1->x)*t;
						  // iy = p1->y + (cp2.y-p1->y)*t;
						  //(iz = p1->z + (cp2.z-p1->z)*t;)
						  // ix = p0->x + (fx-p0->x)*u;
						  // iy = p0->y + (fy-p0->y)*u;
						  // (p1->x-cp2.x)*t + (fx-p0->x)*u = p1->x-p0->x;
						  // (p1->y-cp2.y)*t + (fy-p0->y)*u = p1->y-p0->y;

							f = (p1->x-cp2.x)*(fy-p0->y) - (p1->y-cp2.y)*(fx-p0->x);
							if ((*(int *)&f) == 0) continue;
							f = 1.0 / f;
							g = ((p1->x-cp2.x)*(p1->y-p0->y) - (p1->y-cp2.y)*(p1->x-p0->x))*f;
							//NOTE: The following trick assumes g not * or / by f!
							//if (((*(int *)&g)-(*(int *)&f))^(*(int *)&f)) >= 0) continue;
							if ((*(int *)&g) < 0x3f800000) continue;
							g = ((p1->x-p0->x)*(fy-p0->y) - (p1->y-p0->y)*(fx-p0->x))*f;
						}
						else if (fabs(n.y) > fabs(n.x))
						{ //x,z
							f = (p1->x-cp2.x)*(fz-p0->z) - (p1->z-cp2.z)*(fx-p0->x);
							if ((*(int *)&f) == 0) continue;
							f = 1.0 / f;
							g = ((p1->x-cp2.x)*(p1->z-p0->z) - (p1->z-cp2.z)*(p1->x-p0->x))*f;
							if ((*(int *)&g) < 0x3f800000) continue;
							g = ((p1->x-p0->x)*(fz-p0->z) - (p1->z-p0->z)*(fx-p0->x))*f;
						}
						else
						{ //y,z
							f = (p1->y-cp2.y)*(fz-p0->z) - (p1->z-cp2.z)*(fy-p0->y);
							if ((*(int *)&f) == 0) continue;
							f = 1.0 / f;
							g = ((p1->y-cp2.y)*(p1->z-p0->z) - (p1->z-cp2.z)*(p1->y-p0->y))*f;
							if ((*(int *)&g) < 0x3f800000) continue;
							g = ((p1->y-p0->y)*(fz-p0->z) - (p1->z-p0->z)*(fy-p0->y))*f;
						}
						if ((*(unsigned int *)&g) >= 0x3f800000) continue;
						(hit->x) = fx; (hit->y) = fy; (hit->z) = fz;
						(lhit->x) = x; (lhit->y) = y; (lhit->z) = z; didhit = 1;
						(cp2.x) = (cp2.x-p1->x)*g + p1->x;
						(cp2.y) = (cp2.y-p1->y)*g + p1->y;
						(cp2.z) = (cp2.z-p1->z)*g + p1->z;
					}
				}
			}
		}
	return(didhit);
}

// ------------------------ CONVEX 3D HULL CODE BEGINS ------------------------

#define MAXPOINTS (256 *2) //Leave the *2 here for safety!
static point3d nm[MAXPOINTS*2+2];
static float nmc[MAXPOINTS*2+2];
static int tri[MAXPOINTS*8+8], lnk[MAXPOINTS*8+8], tricnt;
static char umost[VSID*VSID], dmost[VSID*VSID];

void initetrasid (point3d *pt, int z)
{
	int i, j, k;
	float x0, y0, z0, x1, y1, z1;

	i = tri[z*4]; j = tri[z*4+1]; k = tri[z*4+2];
	x0 = pt[i].x-pt[k].x; y0 = pt[i].y-pt[k].y; z0 = pt[i].z-pt[k].z;
	x1 = pt[j].x-pt[k].x; y1 = pt[j].y-pt[k].y; z1 = pt[j].z-pt[k].z;
	nm[z].x = y0*z1 - z0*y1;
	nm[z].y = z0*x1 - x0*z1;
	nm[z].z = x0*y1 - y0*x1;
	nmc[z] = nm[z].x*pt[k].x + nm[z].y*pt[k].y + nm[z].z*pt[k].z;
}

void inithull3d (point3d *pt, int nump)
{
	float px, py, pz;
	int i, k, s, z, szz, zz, zx, snzz, nzz, zzz, otricnt;

	tri[0] = 0; tri[4] = 0; tri[8] = 0; tri[12] = 1;
	tri[1] = 1; tri[2] = 2; initetrasid(pt,0);
	if (nm[0].x*pt[3].x + nm[0].y*pt[3].y + nm[0].z*pt[3].z >= nmc[0])
	{
		tri[1] = 1; tri[2] = 2; lnk[0] = 10; lnk[1] = 14; lnk[2] = 4;
		tri[5] = 2; tri[6] = 3; lnk[4] = 2; lnk[5] = 13; lnk[6] = 8;
		tri[9] = 3; tri[10] = 1; lnk[8] = 6; lnk[9] = 12; lnk[10] = 0;
		tri[13] = 3; tri[14] = 2; lnk[12] = 9; lnk[13] = 5; lnk[14] = 1;
	}
	else
	{
		tri[1] = 2; tri[2] = 1; lnk[0] = 6; lnk[1] = 12; lnk[2] = 8;
		tri[5] = 3; tri[6] = 2; lnk[4] = 10; lnk[5] = 13; lnk[6] = 0;
		tri[9] = 1; tri[10] = 3; lnk[8] = 2; lnk[9] = 14; lnk[10] = 4;
		tri[13] = 2; tri[14] = 3; lnk[12] = 1; lnk[13] = 5; lnk[14] = 9;
	}
	tricnt = 4*4;

	for(z=0;z<4;z++) initetrasid(pt,z);

	for(z=4;z<nump;z++)
	{
		px = pt[z].x; py = pt[z].y; pz = pt[z].z;
		for(zz=tricnt-4;zz>=0;zz-=4)
		{
			i = (zz>>2);
			if (nm[i].x*px + nm[i].y*py + nm[i].z*pz >= nmc[i]) continue;

			s = 0;
			for(zx=2;zx>=0;zx--)
			{
				i = (lnk[zz+zx]>>2);
				s += (nm[i].x*px + nm[i].y*py + nm[i].z*pz < nmc[i]) + s;
			}
			if (s == 7) continue;

			nzz = ((0x4a4>>(s+s))&3); szz = zz; otricnt = tricnt;
			do
			{
				snzz = nzz;
				do
				{
					zzz = nzz+1; if (zzz >= 3) zzz = 0;

						//Insert triangle tricnt: (p0,p1,z)
					tri[tricnt+0] = tri[zz+nzz];
					tri[tricnt+1] = tri[zz+zzz];
					tri[tricnt+2] = z;
					initetrasid(pt,tricnt>>2);
					k = lnk[zz+nzz]; lnk[tricnt] = k; lnk[k] = tricnt;
					lnk[tricnt+1] = tricnt+6;
					lnk[tricnt+2] = tricnt-3;
					tricnt += 4;

						//watch out for loop inside single triangle
					if (zzz == snzz) goto endit;
					nzz = zzz;
				} while (!(s&(1<<zzz)));
				do
				{
					i = zz+nzz;
					zz = (lnk[i]&~3);
					nzz = (lnk[i]&3)+1; if (nzz == 3) nzz = 0;
					s = 0;
					for(zx=2;zx>=0;zx--)
					{
						i = (lnk[zz+zx]>>2);
						s += (nm[i].x*px + nm[i].y*py + nm[i].z*pz < nmc[i]) + s;
					}
				} while (s&(1<<nzz));
			} while (zz != szz);
endit:;  lnk[tricnt-3] = otricnt+2; lnk[otricnt+2] = tricnt-3;

			for(zz=otricnt-4;zz>=0;zz-=4)
			{
				i = (zz>>2);
				if (nm[i].x*px + nm[i].y*py + nm[i].z*pz < nmc[i])
				{
					tricnt -= 4; //Delete triangle zz%
					nm[i] = nm[tricnt>>2]; nmc[i] = nmc[tricnt>>2];
					for(i=0;i<3;i++)
					{
						tri[zz+i] = tri[tricnt+i];
						lnk[zz+i] = lnk[tricnt+i];
						lnk[lnk[zz+i]] = zz+i;
					}
				}
			}
			break;
		}
	}
	tricnt >>= 2;
}

static int incmod3[3];
void tmaphulltrisortho (point3d *pt)
{
	point3d *i0, *i1;
	float r, knmx, knmy, knmc, xinc;
	size_t i, k, op, p, pe, itop, ibot, damost;
	int zi, sy, sy1, y, yi, z;
	int lastx[MAX(MAXYDIM,VSID)];

	for(k=0;k<tricnt;k++)
	{
		if (nm[k].z >= 0)
			{ damost = (size_t)umost; incmod3[0] = 1; incmod3[1] = 2; incmod3[2] = 0; }
		else
			{ damost = (size_t)dmost; incmod3[0] = 2; incmod3[1] = 0; incmod3[2] = 1; }

		itop = (pt[tri[(k<<2)+1]].y < pt[tri[k<<2]].y); ibot = 1-itop;
			  if (pt[tri[(k<<2)+2]].y < pt[tri[(k<<2)+itop]].y) itop = 2;
		else if (pt[tri[(k<<2)+2]].y > pt[tri[(k<<2)+ibot]].y) ibot = 2;

			//Pre-calculations
		if (fabs(nm[k].z) < .000001) r = 0; else r = -65536.0 / nm[k].z;
		knmx = nm[k].x*r; knmy = nm[k].y*r;
		//knmc = 65536.0-nmc[k]*r-knmx-knmy;
		//knmc = -nmc[k]*r-(knmx+knmy)*.5f;
		knmc = /*65536.0*/  -nmc[k]*r+knmx;
		ftol(knmx,&zi);

		i = ibot;
		do
		{
			i1 = &pt[tri[(k<<2)+i]]; ftol(i1->y,&sy1); i = incmod3[i];
			i0 = &pt[tri[(k<<2)+i]]; ftol(i0->y,&sy); if (sy == sy1) continue;
			xinc = (i1->x-i0->x)/(i1->y-i0->y);
			ftol((((float)sy-i0->y)*xinc+i0->x)*65536,&y); ftol(xinc*65536,&yi);
			for(;sy<sy1;sy++,y+=yi) lastx[sy] = (y>>16);
		} while (i != itop);
		do
		{
			i0 = &pt[tri[(k<<2)+i]]; ftol(i0->y,&sy); i = incmod3[i];
			i1 = &pt[tri[(k<<2)+i]]; ftol(i1->y,&sy1); if (sy == sy1) continue;
			xinc = (i1->x-i0->x)/(i1->y-i0->y);
			ftol((((float)sy-i0->y)*xinc+i0->x)*65536,&y); ftol(xinc*65536,&yi);
			op = sy*VSID+damost;
			for(;sy<sy1;sy++,y+=yi,op+=VSID)
			{
				ftol(knmx*(float)lastx[sy] + knmy*(float)sy + knmc,&z);
				pe = (y>>16)+op; p = lastx[sy]+op;
				for(;p<pe;p++,z+=zi) *(char *)p = (z>>16);
			}
		} while (i != ibot);
	}
}

void sethull3d (point3d *pt, int nump, int dacol, int bakit)
{
	void (*modslab)(int *, int, int);
	float fminx, fminy, fminz, fmaxx, fmaxy, fmaxz;
	int i, x, y, xs, ys, xe, ye, z0, z1;

	if (nump > (MAXPOINTS>>1)) nump = (MAXPOINTS>>1); //DANGER!!!

	fminx = fminy = VSID; fminz = MAXZDIM; fmaxx = fmaxy = fmaxz = 0;
	for(i=0;i<nump;i++)
	{
		pt[i].x = MIN(MAX(pt[i].x,0),VSID-1);
		pt[i].y = MIN(MAX(pt[i].y,0),VSID-1);
		pt[i].z = MIN(MAX(pt[i].z,0),MAXZDIM-1);

		if (pt[i].x < fminx) fminx = pt[i].x;
		if (pt[i].y < fminy) fminy = pt[i].y;
		if (pt[i].z < fminz) fminz = pt[i].z;
		if (pt[i].x > fmaxx) fmaxx = pt[i].x;
		if (pt[i].y > fmaxy) fmaxy = pt[i].y;
		if (pt[i].z > fmaxz) fmaxz = pt[i].z;
	}

	ftol(fminx,&xs); if (xs < 0) xs = 0;
	ftol(fminy,&ys); if (ys < 0) ys = 0;
	ftol(fmaxx,&xe); if (xe >= VSID) xe = VSID-1;
	ftol(fmaxy,&ye); if (ye >= VSID) ye = VSID-1;
	vx5.minx = xs; vx5.maxx = xe+1;
	vx5.miny = ys; vx5.maxy = ye+1;
	ftol(fminz-.5,&vx5.minz); ftol(fmaxz+.5,&vx5.maxz);
	if ((xs > xe) || (ys > ye))
		{ if (bakit) voxbackup(xs,ys,xs,ys,bakit); return; }
	if (bakit) voxbackup(xs,ys,xe,ye,bakit);

	i = ys*VSID+(xs&~3); x = ((((xe+3)&~3)-(xs&~3))>>2)+1;
	for(y=ys;y<=ye;y++,i+=VSID)
		{ clearbuf((void *)&umost[i],x,-1); clearbuf((void *)&dmost[i],x,0); }

	inithull3d(pt,nump);
	tmaphulltrisortho(pt);

	if (vx5.colfunc == jitcolfunc) vx5.amount = 0x70707;

	if (dacol == -1) modslab = delslab; else modslab = insslab;

	for(y=ys;y<=ye;y++)
		for(x=xs;x<=xe;x++)
			modslab(scum2(x,y),(int)umost[y*VSID+x],(int)dmost[y*VSID+x]);
	scum2finish();
	updatebbox(vx5.minx,vx5.miny,vx5.minz,vx5.maxx,vx5.maxy,vx5.maxz,dacol);
}

// ------------------------- CONVEX 3D HULL CODE ENDS -------------------------

	//Old&Slow sector code, but only this one supports the 3D bumpmapping :(
static void setsectorb (point3d *p, int *point2, int n, float thick, int dacol, int bakit, int bumpmap)
{
	point3d norm, p2;
	float d, f, x0, y0, x1, y1;
	int i, j, k, got, x, y, z, xs, ys, zs, xe, ye, ze, maxis, ndacol;

	norm.x = 0; norm.y = 0; norm.z = 0;
	for(i=0;i<n;i++)
	{
		j = point2[i]; k = point2[j];
		norm.x += (p[i].y-p[j].y)*(p[k].z-p[j].z) - (p[i].z-p[j].z)*(p[k].y-p[j].y);
		norm.y += (p[i].z-p[j].z)*(p[k].x-p[j].x) - (p[i].x-p[j].x)*(p[k].z-p[j].z);
		norm.z += (p[i].x-p[j].x)*(p[k].y-p[j].y) - (p[i].y-p[j].y)*(p[k].x-p[j].x);
	}
	f = 1.0 / sqrt(norm.x*norm.x + norm.y*norm.y + norm.z*norm.z);
	norm.x *= f; norm.y *= f; norm.z *= f;

	if ((fabs(norm.z) >= fabs(norm.x)) && (fabs(norm.z) >= fabs(norm.y)))
		maxis = 2;
	else if (fabs(norm.y) > fabs(norm.x))
		maxis = 1;
	else
		maxis = 0;

	xs = xe = p[0].x;
	ys = ye = p[0].y;
	zs = ze = p[0].z;
	for(i=n-1;i;i--)
	{
		if (p[i].x < xs) xs = p[i].x;
		if (p[i].y < ys) ys = p[i].y;
		if (p[i].z < zs) zs = p[i].z;
		if (p[i].x > xe) xe = p[i].x;
		if (p[i].y > ye) ye = p[i].y;
		if (p[i].z > ze) ze = p[i].z;
	}
	xs = MAX(xs-thick-bumpmap,0); xe = MIN(xe+thick+bumpmap,VSID-1);
	ys = MAX(ys-thick-bumpmap,0); ye = MIN(ye+thick+bumpmap,VSID-1);
	zs = MAX(zs-thick-bumpmap,0); ze = MIN(ze+thick+bumpmap,MAXZDIM-1);
	vx5.minx = xs; vx5.maxx = xe+1;
	vx5.miny = ys; vx5.maxy = ye+1;
	vx5.minz = zs; vx5.maxz = ze+1;
	if ((xs > xe) || (ys > ye) || (zs > ze)) return;
	if (bakit) voxbackup(xs,ys,xe+1,ye+1,bakit);

	clearbuf((void *)&templongbuf[zs],ze-zs+1,-3);

	ndacol = (dacol==-1)-2;

	for(y=ys;y<=ye;y++)
		for(x=xs;x<=xe;x++)
		{
			got = 0;
			d = ((float)x-p[0].x)*norm.x + ((float)y-p[0].y)*norm.y + ((float)zs-p[0].z)*norm.z;
			for(z=zs;z<=ze;z++,d+=norm.z)
			{
				if (bumpmap)
				{
					if (d < -thick) continue;
					p2.x = (float)x - d*norm.x;
					p2.y = (float)y - d*norm.y;
					p2.z = (float)z - d*norm.z;
					if (d > (float)hpngcolfunc(&p2)+thick) continue;
				}
				else
				{
					if (fabs(d) > thick) continue;
					p2.x = (float)x - d*norm.x;
					p2.y = (float)y - d*norm.y;
					p2.z = (float)z - d*norm.z;
				}

				k = 0;
				for(i=n-1;i>=0;i--)
				{
					j = point2[i];
					switch(maxis)
					{
						case 0: x0 = p[i].z-p2.z; x1 = p[j].z-p2.z;
								  y0 = p[i].y-p2.y; y1 = p[j].y-p2.y; break;
						case 1: x0 = p[i].x-p2.x; x1 = p[j].x-p2.x;
								  y0 = p[i].z-p2.z; y1 = p[j].z-p2.z; break;
						case 2: x0 = p[i].x-p2.x; x1 = p[j].x-p2.x;
								  y0 = p[i].y-p2.y; y1 = p[j].y-p2.y; break;
						default: _gtfo(); //tells MSVC default can't be reached
					}
					if (((*(int *)&y0)^(*(int *)&y1)) < 0)
					{
						if (((*(int *)&x0)^(*(int *)&x1)) >= 0) k ^= (*(int *)&x0);
						else { f = (x0*y1-x1*y0); k ^= (*(int *)&f)^(*(int *)&y1); }
					}
				}
				if (k >= 0) continue;

				templongbuf[z] = ndacol; got = 1;
			}
			if (got)
			{
				scum(x,y,zs,ze+1,templongbuf);
				clearbuf((void *)&templongbuf[zs],ze-zs+1,-3);
			}
		}
	scumfinish();
	updatebbox(vx5.minx,vx5.miny,vx5.minz,vx5.maxx,vx5.maxy,vx5.maxz,dacol);
}

	//This is for ordfillpolygon&splitpoly
typedef struct { int p, i, t; } raster;
#define MAXCURS 100 //THIS IS VERY EVIL... FIX IT!!!
static raster rst[MAXCURS];
static int slist[MAXCURS];

	//Code taken from POLYOLD\POLYSPLI.BAS:splitpoly (06/09/2001)
void splitpoly (float *px, float *py, int *point2, int *bakn,
					 float x0, float y0, float dx, float dy)
{
	int i, j, s2, n, sn, splcnt, z0, z1, z2, z3;
	float t, t1;

	n = (*bakn); if (n < 3) return;
	i = 0; s2 = sn = n; splcnt = 0;
	do
	{
		t1 = (px[i]-x0)*dy - (py[i]-y0)*dx;
		do
		{
			j = point2[i]; point2[i] |= 0x80000000;
			t = t1; t1 = (px[j]-x0)*dy - (py[j]-y0)*dx;
			if ((*(int *)&t) < 0)
				{ px[n] = px[i]; py[n] = py[i]; point2[n] = n+1; n++; }
			if (((*(int *)&t) ^ (*(int *)&t1)) < 0)
			{
				if ((*(int *)&t) < 0) slist[splcnt++] = n;
				t /= (t-t1);
				px[n] = (px[j]-px[i])*t + px[i];
				py[n] = (py[j]-py[i])*t + py[i];
				point2[n] = n+1; n++;
			}
			i = j;
		} while (point2[i] >= 0);
		if (n > s2) { point2[n-1] = s2; s2 = n; }
		for(i=sn-1;(i) && (point2[i] < 0);i--);
	} while (i > 0);

	if (fabs(dx) > fabs(dy))
	{
		for(i=1;i<splcnt;i++)
		{
			z0 = slist[i];
			for(j=0;j<i;j++)
			{
				z1 = point2[z0]; z2 = slist[j]; z3 = point2[z2];
				if (fabs(px[z0]-px[z3])+fabs(px[z2]-px[z1]) < fabs(px[z0]-px[z1])+fabs(px[z2]-px[z3]))
					{ point2[z0] = z3; point2[z2] = z1; }
			}
		}
	}
	else
	{
		for(i=1;i<splcnt;i++)
		{
			z0 = slist[i];
			for(j=0;j<i;j++)
			{
				z1 = point2[z0]; z2 = slist[j]; z3 = point2[z2];
				if (fabs(py[z0]-py[z3])+fabs(py[z2]-py[z1]) < fabs(py[z0]-py[z1])+fabs(py[z2]-py[z3]))
					{ point2[z0] = z3; point2[z2] = z1; }
			}
		}
	}

	for(i=sn;i<n;i++)
		{ px[i-sn] = px[i]; py[i-sn] = py[i]; point2[i-sn] = point2[i]-sn; }
	(*bakn) = n-sn;
}

void ordfillpolygon (float *px, float *py, int *point2, int n, int day, int xs, int xe, void (*modslab)(int *, int, int))
{
	float f;
	int k, i, z, zz, z0, z1, zx, sx0, sy0, sx1, sy1, sy, nsy, gap, numrst;
	int np, ni;

	if (n < 3) return;

	for(z=0;z<n;z++) slist[z] = z;

		//Sort points by y's
	for(gap=(n>>1);gap;gap>>=1)
		for(z=0;z<n-gap;z++)
			for(zz=z;zz>=0;zz-=gap)
			{
				if (py[point2[slist[zz]]] <= py[point2[slist[zz+gap]]]) break;
				z0 = slist[zz]; slist[zz] = slist[zz+gap]; slist[zz+gap] = z0;
			}

	ftol(py[point2[slist[0]]]+.5,&sy); if (sy < xs) sy = xs;

	numrst = 0; z = 0; n--; //Note: n is local variable!
	while (z < n)
	{
		z1 = slist[z]; z0 = point2[z1];
		for(zx=0;zx<2;zx++)
		{
			ftol(py[z0]+.5,&sy0); ftol(py[z1]+.5,&sy1);
			if (sy1 > sy0) //Insert raster (z0,z1)
			{
				f = (px[z1]-px[z0]) / (py[z1]-py[z0]);
				ftol(((sy-py[z0])*f + px[z0])*65536.0 + 65535.0,&np);
				if (sy1-sy0 >= 2) ftol(f*65536.0,&ni); else ni = 0;
				k = (np<<1)+ni;
				for(i=numrst;i>0;i--)
				{
					if ((rst[i-1].p<<1)+rst[i-1].i < k) break;
					rst[i] = rst[i-1];
				}
				rst[i].i = ni; rst[i].p = np; rst[i].t = (z0<<16)+z1;
				numrst++;
			}
			else if (sy1 < sy0) //Delete raster (z1,z0)
			{
				numrst--;
				k = (z1<<16)+z0; i = 0;
				while ((i < numrst) && (rst[i].t != k)) i++;
				while (i < numrst) { rst[i] = rst[i+1]; i++; }
			}
			z1 = point2[z0];
		}

		z++;
		ftol(py[point2[slist[z]]]+.5,&nsy); if (nsy > xe) nsy = xe;
		for(;sy<nsy;sy++)
			for(i=0;i<numrst;i+=2)
			{
				modslab(scum2(sy,day),MAX(rst[i].p>>16,0),MIN(rst[i+1].p>>16,MAXZDIM));
				rst[i].p += rst[i].i; rst[i+1].p += rst[i+1].i;
			}
	}
}

	//Draws a flat polygon
	//given: p&point2: 3D points, n: # points, thick: thickness, dacol: color
static float ppx[MAXCURS*4], ppy[MAXCURS*4];
static int npoint2[MAXCURS*4];
void setsector (point3d *p, int *point2, int n, float thick, int dacol, int bakit)
{
	void (*modslab)(int *, int, int);
	point3d norm;
	float f, rnormy, xth, zth, dax, daz, t, t1;
	int i, j, k, x, y, z, sn, s2, nn, xs, ys, zs, xe, ye, ze;

	norm.x = 0; norm.y = 0; norm.z = 0;
	for(i=0;i<n;i++)
	{
		j = point2[i]; k = point2[j];
		norm.x += (p[i].y-p[j].y)*(p[k].z-p[j].z) - (p[i].z-p[j].z)*(p[k].y-p[j].y);
		norm.y += (p[i].z-p[j].z)*(p[k].x-p[j].x) - (p[i].x-p[j].x)*(p[k].z-p[j].z);
		norm.z += (p[i].x-p[j].x)*(p[k].y-p[j].y) - (p[i].y-p[j].y)*(p[k].x-p[j].x);
	}
	f = 1.0 / sqrt(norm.x*norm.x + norm.y*norm.y + norm.z*norm.z);
	norm.x *= f; norm.y *= f; norm.z *= f;

	if (vx5.colfunc == jitcolfunc) vx5.amount = 0x70707;
	else if ((vx5.colfunc == pngcolfunc) && (vx5.pic) && (vx5.xsiz > 0) && (vx5.ysiz > 0) && (vx5.picmode == 3))
	{
			//Find biggest height offset to minimize bounding box size
		j = k = vx5.pic[0];
		for(y=vx5.ysiz-1;y>=0;y--)
		{
			i = y*(vx5.bpl>>2);
			for(x=vx5.xsiz-1;x>=0;x--)
			{
				if (vx5.pic[i+x] < j) j = vx5.pic[i+x];
				if (vx5.pic[i+x] > k) k = vx5.pic[i+x];
			}
		}
		if ((j^k)&0xff000000) //If high bytes are !=, then use bumpmapping
		{
			setsectorb(p,point2,n,thick,dacol,bakit,MAX(labs(j>>24),labs(k>>24)));
			return;
		}
	}

	xs = xe = p[0].x;
	ys = ye = p[0].y;
	zs = ze = p[0].z;
	for(i=n-1;i;i--)
	{
		if (p[i].x < xs) xs = p[i].x;
		if (p[i].y < ys) ys = p[i].y;
		if (p[i].z < zs) zs = p[i].z;
		if (p[i].x > xe) xe = p[i].x;
		if (p[i].y > ye) ye = p[i].y;
		if (p[i].z > ze) ze = p[i].z;
	}
	xs = MAX(xs-thick,0); xe = MIN(xe+thick,VSID-1);
	ys = MAX(ys-thick,0); ye = MIN(ye+thick,VSID-1);
	zs = MAX(zs-thick,0); ze = MIN(ze+thick,MAXZDIM-1);
	vx5.minx = xs; vx5.maxx = xe+1;
	vx5.miny = ys; vx5.maxy = ye+1;
	vx5.minz = zs; vx5.maxz = ze+1;
	if ((xs > xe) || (ys > ye) || (zs > ze)) return;
	if (bakit) voxbackup(xs,ys,xe+1,ye+1,bakit);

	if (dacol == -1) modslab = delslab; else modslab = insslab;

	if (fabs(norm.y) >= .001)
	{
		rnormy = 1.0 / norm.y;
		for(y=ys;y<=ye;y++)
		{
			nn = n;
			for(i=0;i<n;i++)
			{
				f = ((float)y-p[i].y) * rnormy;
				ppx[i] = norm.z*f + p[i].z;
				ppy[i] = norm.x*f + p[i].x;
				npoint2[i] = point2[i];
			}
			if (fabs(norm.x) > fabs(norm.z))
			{
				splitpoly(ppx,ppy,npoint2,&nn,p[0].z,((p[0].y-(float)y)*norm.y-thick)/norm.x+p[0].x,norm.x,-norm.z);
				splitpoly(ppx,ppy,npoint2,&nn,p[0].z,((p[0].y-(float)y)*norm.y+thick)/norm.x+p[0].x,-norm.x,norm.z);
			}
			else
			{
				splitpoly(ppx,ppy,npoint2,&nn,((p[0].y-(float)y)*norm.y-thick)/norm.z+p[0].z,p[0].x,norm.x,-norm.z);
				splitpoly(ppx,ppy,npoint2,&nn,((p[0].y-(float)y)*norm.y+thick)/norm.z+p[0].z,p[0].x,-norm.x,norm.z);
			}
			ordfillpolygon(ppx,ppy,npoint2,nn,y,xs,xe,modslab);
		}
	}
	else
	{
		xth = norm.x*thick; zth = norm.z*thick;
		for(y=ys;y<=ye;y++)
		{
			for(z=0;z<n;z++) slist[z] = 0;
			nn = 0; i = 0; sn = n;
			do
			{
				s2 = nn; t1 = p[i].y-(float)y;
				do
				{
					j = point2[i]; slist[i] = 1; t = t1; t1 = p[j].y-(float)y;
					if (((*(int *)&t) ^ (*(int *)&t1)) < 0)
					{
						k = ((*(unsigned int *)&t)>>31); t /= (t-t1);
						daz = (p[j].z-p[i].z)*t + p[i].z;
						dax = (p[j].x-p[i].x)*t + p[i].x;
						ppx[nn+k] = daz+zth; ppx[nn+1-k] = daz-zth;
						ppy[nn+k] = dax+xth; ppy[nn+1-k] = dax-xth;
						npoint2[nn] = nn+1; npoint2[nn+1] = nn+2; nn += 2;
					}
					i = j;
				} while (!slist[i]);
				if (nn > s2) { npoint2[nn-1] = s2; s2 = nn; }
				for(i=sn-1;(i) && (slist[i]);i--);
			} while (i);
			ordfillpolygon(ppx,ppy,npoint2,nn,y,xs,xe,modslab);
		}
	}
	scum2finish();
	updatebbox(vx5.minx,vx5.miny,vx5.minz,vx5.maxx,vx5.maxy,vx5.maxz,dacol);
}

	//Given: p[>=3]: points 0,1 are the axis of rotation, others make up shape
	//      numcurs: number of points
	//        dacol: color
void setlathe (point3d *p, int numcurs, int dacol, int bakit)
{
	point3d norm, ax0, ax1, tp0, tp1;
	float d, f, x0, y0, x1, y1, px, py, pz;
	int i, j, cnt, got, x, y, z, xs, ys, zs, xe, ye, ze, maxis, ndacol;

	norm.x = (p[0].y-p[1].y)*(p[2].z-p[1].z) - (p[0].z-p[1].z)*(p[2].y-p[1].y);
	norm.y = (p[0].z-p[1].z)*(p[2].x-p[1].x) - (p[0].x-p[1].x)*(p[2].z-p[1].z);
	norm.z = (p[0].x-p[1].x)*(p[2].y-p[1].y) - (p[0].y-p[1].y)*(p[2].x-p[1].x);
	f = 1.0 / sqrt(norm.x*norm.x + norm.y*norm.y + norm.z*norm.z);
	norm.x *= f; norm.y *= f; norm.z *= f;

	ax0.x = p[1].x-p[0].x; ax0.y = p[1].y-p[0].y; ax0.z = p[1].z-p[0].z;
	f = 1.0 / sqrt(ax0.x*ax0.x + ax0.y*ax0.y + ax0.z*ax0.z);
	ax0.x *= f; ax0.y *= f; ax0.z *= f;

	ax1.x = ax0.y*norm.z - ax0.z*norm.y;
	ax1.y = ax0.z*norm.x - ax0.x*norm.z;
	ax1.z = ax0.x*norm.y - ax0.y*norm.x;

	x0 = 0; //Cylindrical thickness: Perp-dist from line (p[0],p[1])
	y0 = 0; //Cylindrical min dot product from line (p[0],p[1])
	y1 = 0; //Cylindrical max dot product from line (p[0],p[1])
	for(i=numcurs-1;i;i--)
	{
		d = (p[i].x-p[0].x)*ax0.x + (p[i].y-p[0].y)*ax0.y + (p[i].z-p[0].z)*ax0.z;
		if (d < y0) y0 = d;
		if (d > y1) y1 = d;
		px = (p[i].x-p[0].x) - d*ax0.x;
		py = (p[i].y-p[0].y) - d*ax0.y;
		pz = (p[i].z-p[0].z) - d*ax0.z;
		f = px*px + py*py + pz*pz;     //Note: f is thickness SQUARED
		if (f > x0) x0 = f;
	}
	x0 = sqrt(x0)+1.0;
	tp0.x = ax0.x*y0 + p[0].x; tp1.x = ax0.x*y1 + p[0].x;
	tp0.y = ax0.y*y0 + p[0].y; tp1.y = ax0.y*y1 + p[0].y;
	tp0.z = ax0.z*y0 + p[0].z; tp1.z = ax0.z*y1 + p[0].z;
	xs = MAX(MIN(tp0.x,tp1.x)-x0,0); xe = MIN(MAX(tp0.x,tp1.x)+x0,VSID-1);
	ys = MAX(MIN(tp0.y,tp1.y)-x0,0); ye = MIN(MAX(tp0.y,tp1.y)+x0,VSID-1);
	zs = MAX(MIN(tp0.z,tp1.z)-x0,0); ze = MIN(MAX(tp0.z,tp1.z)+x0,MAXZDIM-1);
	vx5.minx = xs; vx5.maxx = xe+1;
	vx5.miny = ys; vx5.maxy = ye+1;
	vx5.minz = zs; vx5.maxz = ze+1;
	if ((xs > xe) || (ys > ye) || (zs > ze)) return;
	if (bakit) voxbackup(xs,ys,xe,ye,bakit);

	if ((fabs(norm.z) >= fabs(norm.x)) && (fabs(norm.z) >= fabs(norm.y)))
		maxis = 2;
	else if (fabs(norm.y) > fabs(norm.x))
		maxis = 1;
	else
		maxis = 0;

	clearbuf((void *)&templongbuf[zs],ze-zs+1,-3);

	if (vx5.colfunc == jitcolfunc) vx5.amount = 0x70707;

	ndacol = (dacol==-1)-2;

	for(y=ys;y<=ye;y++)
		for(x=xs;x<=xe;x++)
		{
			got = 0;
			d = ((float)x-p[0].x)*ax0.x + ((float)y-p[0].y)*ax0.y + ((float)zs-p[0].z)*ax0.z;
			for(z=zs;z<=ze;z++,d+=ax0.z)
			{
					//Another way: p = sqrt((xyz dot ax1)^2 + (xyz dot norm)^2)
				px = ((float)x-p[0].x) - d*ax0.x;
				py = ((float)y-p[0].y) - d*ax0.y;
				pz = ((float)z-p[0].z) - d*ax0.z;
				f = sqrt(px*px + py*py + pz*pz);

				px = ax0.x*d + ax1.x*f + p[0].x;
				py = ax0.y*d + ax1.y*f + p[0].y;
				pz = ax0.z*d + ax1.z*f + p[0].z;

				cnt = j = 0;
				for(i=numcurs-1;i>=0;i--)
				{
					switch(maxis)
					{
						case 0: x0 = p[i].z-pz; x1 = p[j].z-pz;
								  y0 = p[i].y-py; y1 = p[j].y-py; break;
						case 1: x0 = p[i].x-px; x1 = p[j].x-px;
								  y0 = p[i].z-pz; y1 = p[j].z-pz; break;
						case 2: x0 = p[i].x-px; x1 = p[j].x-px;
								  y0 = p[i].y-py; y1 = p[j].y-py; break;
						default: _gtfo(); //tells MSVC default can't be reached
					}
					if (((*(int *)&y0)^(*(int *)&y1)) < 0)
					{
						if (((*(int *)&x0)^(*(int *)&x1)) >= 0) cnt ^= (*(int *)&x0);
						else { f = (x0*y1-x1*y0); cnt ^= (*(int *)&f)^(*(int *)&y1); }
					}
					j = i;
				}
				if (cnt >= 0) continue;

				templongbuf[z] = ndacol; got = 1;
			}
			if (got)
			{
				scum(x,y,zs,ze+1,templongbuf);
				clearbuf((void *)&templongbuf[zs],ze-zs+1,-3);
			}
		}
	scumfinish();
	updatebbox(vx5.minx,vx5.miny,vx5.minz,vx5.maxx,vx5.maxy,vx5.maxz,dacol);
}

	//Given: p[>=1]: centers
	//   vx5.currad: cutoff value
	//      numcurs: number of points
	//        dacol: color
void setblobs (point3d *p, int numcurs, int dacol, int bakit)
{
	float dx, dy, dz, v, nrad;
	int i, got, x, y, z, xs, ys, zs, xe, ye, ze, ndacol;

	if (numcurs <= 0) return;

		//Boundaries are quick hacks - rewrite this code!!!
	xs = MAX(p[0].x-64,0); xe = MIN(p[0].x+64,VSID-1);
	ys = MAX(p[0].y-64,0); ye = MIN(p[0].y+64,VSID-1);
	zs = MAX(p[0].z-64,0); ze = MIN(p[0].z+64,MAXZDIM-1);
	vx5.minx = xs; vx5.maxx = xe+1;
	vx5.miny = ys; vx5.maxy = ye+1;
	vx5.minz = zs; vx5.maxz = ze+1;
	if ((xs > xe) || (ys > ye) || (zs > ze)) return;
	if (bakit) voxbackup(xs,ys,xe,ye,bakit);

	clearbuf((void *)&templongbuf[zs],ze-zs+1,-3);

	if (vx5.colfunc == jitcolfunc) vx5.amount = 0x70707;

	ndacol = (dacol==-1)-2;

	nrad = (float)numcurs / ((float)vx5.currad*(float)vx5.currad + 256.0);
	for(y=ys;y<=ye;y++)
		for(x=xs;x<=xe;x++)
		{
			got = 0;
			for(z=zs;z<=ze;z++)
			{
				v = 0;
				for(i=numcurs-1;i>=0;i--)
				{
					dx = p[i].x-(float)x;
					dy = p[i].y-(float)y;
					dz = p[i].z-(float)z;
					v += 1.0f / (dx*dx + dy*dy + dz*dz + 256.0f);
				}
				if (*(int *)&v > *(int *)&nrad) { templongbuf[z] = ndacol; got = 1; }
			}
			if (got)
			{
				scum(x,y,zs,ze+1,templongbuf);
				clearbuf((void *)&templongbuf[zs],ze-zs+1,-3);
			}
		}
	scumfinish();
	updatebbox(vx5.minx,vx5.miny,vx5.minz,vx5.maxx,vx5.maxy,vx5.maxz,dacol);
}

//FLOODFILL3D begins --------------------------------------------------------

#define FILLBUFSIZ 16384 //Use realloc instead!
typedef struct { unsigned short x, y, z0, z1; } spoint4d; //128K
static spoint4d fbuf[FILLBUFSIZ];

int dntil0 (int x, int y, int z)
{
	char *v = sptr[y*VSID+x];
	while (1)
	{
		if (z < v[1]) break;
		if (!v[0]) return(MAXZDIM);
		v += v[0]*4;
		if (z < v[3]) return(v[3]);
	}
	return(z);
}

int dntil1 (int x, int y, int z)
{
	char *v = sptr[y*VSID+x];
	while (1)
	{
		if (z <= v[1]) return(v[1]);
		if (!v[0]) break;
		v += v[0]*4;
		if (z < v[3]) break;
	}
	return(z);
}

int uptil1 (int x, int y, int z)
{
	char *v = sptr[y*VSID+x];
	if (z < v[1]) return(0);
	while (v[0])
	{
		v += v[0]*4;
		if (z < v[3]) break;
		if (z < v[1]) return(v[3]);
	}
	return(z);
}

	//Conducts on air and writes solid
void setfloodfill3d (int x, int y, int z, int minx, int miny, int minz,
															int maxx, int maxy, int maxz)
{
	int wholemap, j, z0, z1, nz1, i0, i1, (*bakcolfunc)(lpoint3d *);
	spoint4d a;

	if (minx < 0) minx = 0;
	if (miny < 0) miny = 0;
	if (minz < 0) minz = 0;
	maxx++; maxy++; maxz++;
	if (maxx > VSID) maxx = VSID;
	if (maxy > VSID) maxy = VSID;
	if (maxz > MAXZDIM) maxz = MAXZDIM;
	vx5.minx = minx; vx5.maxx = maxx;
	vx5.miny = miny; vx5.maxy = maxy;
	vx5.minz = minz; vx5.maxz = maxz;
	if ((minx >= maxx) || (miny >= maxy) || (minz >= maxz)) return;

	if ((x < minx) || (x >= maxx) ||
		 (y < miny) || (y >= maxy) ||
		 (z < minz) || (z >= maxz)) return;

	if ((minx != 0) || (miny != 0) || (minz != 0) || (maxx != VSID) || (maxy != VSID) || (maxz != VSID))
		wholemap = 0;
	else wholemap = 1;

	if (isvoxelsolid(x,y,z)) return;

	bakcolfunc = vx5.colfunc; vx5.colfunc = curcolfunc;

	a.x = x; a.z0 = uptil1(x,y,z); if (a.z0 < minz) a.z0 = minz;
	a.y = y; a.z1 = dntil1(x,y,z+1); if (a.z1 > maxz) a.z1 = maxz;
	if (((!a.z0) && (wholemap)) || (a.z0 >= a.z1)) { vx5.colfunc = bakcolfunc; return; } //oops! broke free :/
	insslab(scum2(x,y),a.z0,a.z1); scum2finish();
	i0 = i1 = 0; goto floodfill3dskip;
	do
	{
		a = fbuf[i0]; i0 = ((i0+1)&(FILLBUFSIZ-1));
floodfill3dskip:;
		for(j=3;j>=0;j--)
		{
			if (j&1) { x = a.x+(j&2)-1; if ((x < minx) || (x >= maxx)) continue; y = a.y; }
				 else { y = a.y+(j&2)-1; if ((y < miny) || (y >= maxy)) continue; x = a.x; }

			if (isvoxelsolid(x,y,a.z0)) { z0 = dntil0(x,y,a.z0); z1 = z0; }
										  else { z0 = uptil1(x,y,a.z0); z1 = a.z0; }
			if ((!z0) && (wholemap)) { vx5.colfunc = bakcolfunc; return; } //oops! broke free :/
			while (z1 < a.z1)
			{
				z1 = dntil1(x,y,z1);

				if (z0 < minz) z0 = minz;
				nz1 = z1; if (nz1 > maxz) nz1 = maxz;
				if (z0 < nz1)
				{
					fbuf[i1].x = x; fbuf[i1].y = y;
					fbuf[i1].z0 = z0; fbuf[i1].z1 = nz1;
					i1 = ((i1+1)&(FILLBUFSIZ-1));
					//if (i0 == i1) floodfill stack overflow!
					insslab(scum2(x,y),z0,nz1); scum2finish();
				}
				z0 = dntil0(x,y,z1); z1 = z0;
			}
		}
	} while (i0 != i1);

	vx5.colfunc = bakcolfunc;

	updatebbox(vx5.minx,vx5.miny,vx5.minz,vx5.maxx,vx5.maxy,vx5.maxz,0);
}

void hollowfillstart (int x, int y, int z)
{
	spoint4d a;
	char *v;
	int i, j, z0, z1, i0, i1;

	a.x = x; a.y = y;

	v = sptr[y*VSID+x]; j = ((((ptrdiff_t)v)-(ptrdiff_t)vbuf)>>2); a.z0 = 0;
	while (1)
	{
		a.z1 = (int)(v[1]);
		if ((a.z0 <= z) && (z < a.z1) && (!(vbit[j>>5]&(1<<j)))) break;
		if (!v[0]) return;
		v += v[0]*4; j += 2;
		a.z0 = (int)(v[3]);
	}
	vbit[j>>5] |= (1<<j); //fill a.x,a.y,a.z0<=?<a.z1

	i0 = i1 = 0; goto floodfill3dskip2;
	do
	{
		a = fbuf[i0]; i0 = ((i0+1)&(FILLBUFSIZ-1));
floodfill3dskip2:;
		for(i=3;i>=0;i--)
		{
			if (i&1) { x = a.x+(i&2)-1; if ((unsigned int)x >= VSID) continue; y = a.y; }
				 else { y = a.y+(i&2)-1; if ((unsigned int)y >= VSID) continue; x = a.x; }

			v = sptr[y*VSID+x]; j = ((((ptrdiff_t)v)-(ptrdiff_t)vbuf)>>2); z0 = 0;
			while (1)
			{
				z1 = (int)(v[1]);
				if ((z0 < a.z1) && (a.z0 < z1) && (!(vbit[j>>5]&(1<<j))))
				{
					fbuf[i1].x = x; fbuf[i1].y = y;
					fbuf[i1].z0 = z0; fbuf[i1].z1 = z1;
					i1 = ((i1+1)&(FILLBUFSIZ-1));
					if (i0 == i1) return; //floodfill stack overflow!
					vbit[j>>5] |= (1<<j); //fill x,y,z0<=?<z1
				}
				if (!v[0]) break;
				v += v[0]*4; j += 2;
				z0 = (int)(v[3]);
			}
		}
	} while (i0 != i1);
}

	//hollowfill
void sethollowfill ()
{
	int i, j, l, x, y, z0, z1, *lptr, (*bakcolfunc)(lpoint3d *);
	char *v;

	vx5.minx = 0; vx5.maxx = VSID;
	vx5.miny = 0; vx5.maxy = VSID;
	vx5.minz = 0; vx5.maxz = MAXZDIM;

	for(i=0;i<VSID*VSID;i++)
	{
		j = ((((ptrdiff_t)sptr[i])-(ptrdiff_t)vbuf)>>2);
		for(v=sptr[i];v[0];v+=v[0]*4) { vbit[j>>5] &= ~(1<<j); j += 2; }
		vbit[j>>5] &= ~(1<<j);
	}

	for(y=0;y<VSID;y++)
		for(x=0;x<VSID;x++)
			hollowfillstart(x,y,0);

	bakcolfunc = vx5.colfunc; vx5.colfunc = curcolfunc;
	i = 0;
	for(y=0;y<VSID;y++)
		for(x=0;x<VSID;x++,i++)
		{
			j = ((((ptrdiff_t)sptr[i])-(ptrdiff_t)vbuf)>>2);
			v = sptr[i]; z0 = MAXZDIM;
			while (1)
			{
				z1 = (int)(v[1]);
				if ((z0 < z1) && (!(vbit[j>>5]&(1<<j))))
				{
					vbit[j>>5] |= (1<<j);
					insslab(scum2(x,y),z0,z1);
				}
				if (!v[0]) break;
				v += v[0]*4; j += 2;
				z0 = (int)(v[3]);
			}
		}
	scum2finish();
	vx5.colfunc = bakcolfunc;
	updatebbox(vx5.minx,vx5.miny,vx5.minz,vx5.maxx,vx5.maxy,vx5.maxz,0);
}

//FLOODFILL3D ends ----------------------------------------------------------

#define LPATBUFSIZ 14
static lpoint2d *patbuf;
#define LPATHASHSIZ 12
static lpoint3d *pathashdat;
static int *pathashead, pathashcnt, pathashmax;

static void initpathash ()
{
	patbuf = (lpoint2d *)radar;
	pathashead = (int *)(((size_t)patbuf)+(1<<LPATBUFSIZ)*sizeof(lpoint2d));
	pathashdat = (lpoint3d *)(((size_t)pathashead)+((1<<LPATHASHSIZ)*4));
	pathashmax = ((MAX((MAXXDIM*MAXYDIM*27)>>1,(VSID+4)*3*256*4)-((1<<LPATBUFSIZ)*sizeof(lpoint2d))-(1<<LPATHASHSIZ)*4)/12);
	memset(pathashead,-1,(1<<LPATHASHSIZ)*4);
	pathashcnt = 0;
}

static int readpathash (int i)
{
	int j = (((i>>LPATHASHSIZ)-i) & ((1<<LPATHASHSIZ)-1));
	for(j=pathashead[j];j>=0;j=pathashdat[j].x)
		if (pathashdat[j].y == i) return(pathashdat[j].z);
	return(-1);
}

static void writepathash (int i, int v)
{
	int k, j = (((i>>LPATHASHSIZ)-i) & ((1<<LPATHASHSIZ)-1));
	for(k=pathashead[j];k>=0;k=pathashdat[k].x)
		if (pathashdat[k].y == i) { pathashdat[k].z = v; return; }
	pathashdat[pathashcnt].x = pathashead[j]; pathashead[j] = pathashcnt;
	pathashdat[pathashcnt].y = i;
	pathashdat[pathashcnt].z = v;
	pathashcnt++;
}

static signed char cdir[26*4] = //sqrt(2) =~ 58/41, sqrt(3) =~ 71/41;
{
	-1, 0, 0,41,  1, 0, 0,41,  0,-1, 0,41,  0, 1, 0,41,  0, 0,-1,41,  0, 0, 1,41,
	-1,-1, 0,58, -1, 1, 0,58, -1, 0,-1,58, -1, 0, 1,58,  0,-1,-1,58,  0,-1, 1,58,
	 1,-1, 0,58,  1, 1, 0,58,  1, 0,-1,58,  1, 0, 1,58,  0, 1,-1,58,  0, 1, 1,58,
	-1,-1,-1,71, -1,-1, 1,71, -1, 1,-1,71, -1, 1, 1,71,
	 1,-1,-1,71,  1,-1, 1,71,  1, 1,-1,71,  1, 1, 1,71,
};

int findpath (int *pathpos, int pathmax, lpoint3d *p1, lpoint3d *p0)
{
	int i, j, k, x, y, z, c, nc, xx, yy, zz, bufr, bufw, pcnt;

	if (!(((size_t)getcube(p0->x,p0->y,p0->z))&~1))
	{
		for(i=5;i>=0;i--)
		{
			x = p0->x+(int)cdir[i*4]; y = p0->y+(int)cdir[i*4+1]; z = p0->z+(int)cdir[i*4+2];
			if (((size_t)getcube(x,y,z))&~1) { p0->x = x; p0->y = y; p0->z = z; break; }
		}
		if (i < 0) return(0);
	}
	if (!(((size_t)getcube(p1->x,p1->y,p1->z))&~1))
	{
		for(i=5;i>=0;i--)
		{
			x = p1->x+(int)cdir[i*4]; y = p1->y+(int)cdir[i*4+1]; z = p1->z+(int)cdir[i*4+2];
			if (((size_t)getcube(x,y,z))&~1) { p1->x = x; p1->y = y; p1->z = z; break; }
		}
		if (i < 0) return(0);
	}

	initpathash();
	j = (p0->x*VSID + p0->y)*MAXZDIM+p0->z;
	patbuf[0].x = j; patbuf[0].y = 0; bufr = 0; bufw = 1;
	writepathash(j,0);
	do
	{
		j = patbuf[bufr&((1<<LPATBUFSIZ)-1)].x;
		x = j/(VSID*MAXZDIM); y = ((j/MAXZDIM)&(VSID-1)); z = (j&(MAXZDIM-1));
		c = patbuf[bufr&((1<<LPATBUFSIZ)-1)].y; bufr++;
		for(i=0;i<26;i++)
		{
			xx = x+(int)cdir[i*4]; yy = y+(int)cdir[i*4+1]; zz = z+(int)cdir[i*4+2];
			j = (xx*VSID + yy)*MAXZDIM+zz;

			//nc = c+(int)cdir[i*4+3]; //More accurate but lowers max distance a lot!
			//if (((k = getcube(xx,yy,zz))&~1) && ((unsigned int)nc < (unsigned int)readpathash(j)))

			if (((k = ((size_t)getcube(xx,yy,zz)))&~1) && (readpathash(j) < 0))
			{
				nc = c+(int)cdir[i*4+3];
				if ((xx == p1->x) && (yy == p1->y) && (zz == p1->z)) { c = nc; goto pathfound; }
				writepathash(j,nc);
				if (pathashcnt >= pathashmax) return(0);
				patbuf[bufw&((1<<LPATBUFSIZ)-1)].x = (xx*VSID + yy)*MAXZDIM+zz;
				patbuf[bufw&((1<<LPATBUFSIZ)-1)].y = nc; bufw++;
			}
		}
	} while (bufr != bufw);

pathfound:
	if (pathmax <= 0) return(0);
	pathpos[0] = (p1->x*VSID + p1->y)*MAXZDIM+p1->z; pcnt = 1;
	x = p1->x; y = p1->y; z = p1->z;
	do
	{
		for(i=0;i<26;i++)
		{
			xx = x+(int)cdir[i*4]; yy = y+(int)cdir[i*4+1]; zz = z+(int)cdir[i*4+2];
			nc = c-(int)cdir[i*4+3];
			if (readpathash((xx*VSID + yy)*MAXZDIM+zz) == nc)
			{
				if (pcnt >= pathmax) return(0);
				pathpos[pcnt] = (xx*VSID + yy)*MAXZDIM+zz; pcnt++;
				x = xx; y = yy; z = zz; c = nc; break;
			}
		}
	} while (i < 26);
	if (pcnt >= pathmax) return(0);
	pathpos[pcnt] = (p0->x*VSID + p0->y)*MAXZDIM+p0->z;
	return(pcnt+1);
}

//---------------------------------------------------------------------

static unsigned short xyoffs[256][256+1];
void setkvx (const char *filename, int ox, int oy, int oz, int rot, int bakit)
{
	int i, j, x, y, z, xsiz, ysiz, zsiz, longpal[256], zleng, oldz, vis;
	int d[3], k[9], x0, y0, z0, x1, y1, z1;
	char ch, typ;
	FILE *fp;

	typ = filename[strlen(filename)-3]; if (typ == 'k') typ = 'K';

	if (!(fp = fopen(filename,"rb"))) return;

	fseek(fp,-768,SEEK_END);
	for(i=0;i<255;i++)
	{
		longpal[i]  = (((int)fgetc(fp))<<18);
		longpal[i] += (((int)fgetc(fp))<<10);
		longpal[i] += (((int)fgetc(fp))<< 2) + 0x80000000;
	}
	longpal[255] = 0x7ffffffd;

	if (typ == 'K') //Load .KVX file
	{
		fseek(fp,4,SEEK_SET);
		fread(&xsiz,4,1,fp);
		fread(&ysiz,4,1,fp);
		fread(&zsiz,4,1,fp);
		fseek(fp,((xsiz+1)<<2)+28,SEEK_SET);
		for(i=0;i<xsiz;i++) fread(&xyoffs[i][0],(ysiz+1)<<1,1,fp);
	}
	else           //Load .VOX file
	{
		fseek(fp,0,SEEK_SET);
		fread(&xsiz,4,1,fp);
		fread(&ysiz,4,1,fp);
		fread(&zsiz,4,1,fp);
	}

		//rot: low 3 bits for axis negating, high 6 states for axis swapping
		//k[0], k[3], k[6] are indeces
		//k[1], k[4], k[7] are xors
		//k[2], k[5], k[8] are adds
	switch (rot&~7)
	{
		case  0: k[0] = 0; k[3] = 1; k[6] = 2; break; //can use scum!
		case  8: k[0] = 1; k[3] = 0; k[6] = 2; break; //can use scum!
		case 16: k[0] = 0; k[3] = 2; k[6] = 1; break;
		case 24: k[0] = 2; k[3] = 0; k[6] = 1; break;
		case 32: k[0] = 1; k[3] = 2; k[6] = 0; break;
		case 40: k[0] = 2; k[3] = 1; k[6] = 0; break;
		default: _gtfo(); //tells MSVC default can't be reached
	}
	k[1] = ((rot<<31)>>31);
	k[4] = ((rot<<30)>>31);
	k[7] = ((rot<<29)>>31);

	d[0] = xsiz; d[1] = ysiz; d[2] = zsiz;
	k[2] = ox-((d[k[0]]>>1)^k[1]);
	k[5] = oy-((d[k[3]]>>1)^k[4]);
	k[8] = oz-((d[k[6]]>>1)^k[7]); k[8] -= (d[k[6]]>>1);

	d[0] = d[1] = d[2] = 0;
	x0 = x1 = (d[k[0]]^k[1])+k[2];
	y0 = y1 = (d[k[3]]^k[4])+k[5];
	z0 = z1 = (d[k[6]]^k[7])+k[8];
	d[0] = xsiz; d[1] = ysiz; d[2] = zsiz;
	x0 = MIN(x0,(d[k[0]]^k[1])+k[2]); x1 = MAX(x1,(d[k[0]]^k[1])+k[2]);
	y0 = MIN(y0,(d[k[3]]^k[4])+k[5]); y1 = MAX(y1,(d[k[3]]^k[4])+k[5]);
	z0 = MIN(z0,(d[k[6]]^k[7])+k[8]); z1 = MAX(z1,(d[k[6]]^k[7])+k[8]);
	if (x0 < 1) { i = 1-x0; x0 += i; x1 += i; k[2] += i; }
	if (y0 < 1) { i = 1-y0; y0 += i; y1 += i; k[5] += i; }
	if (z0 < 0) { i = 0-z0; z0 += i; z1 += i; k[8] += i; }
	if (x1 > VSID-2)    { i = VSID-2-x1; x0 += i; x1 += i; k[2] += i; }
	if (y1 > VSID-2)    { i = VSID-2-y1; y0 += i; y1 += i; k[5] += i; }
	if (z1 > MAXZDIM-1) { i = MAXZDIM-1-z1; z0 += i; z1 += i; k[8] += i; }

	vx5.minx = x0; vx5.maxx = x1+1;
	vx5.miny = y0; vx5.maxy = y1+1;
	vx5.minz = z0; vx5.maxz = z1+1;
	if (bakit) voxbackup(x0,y0,x1+1,y1+1,bakit);

	j = (!(k[3]|(rot&3))); //if (j) { can use scum/scumfinish! }

	for(x=0;x<xsiz;x++)
	{
		d[0] = x;
		for(y=0;y<ysiz;y++)
		{
			d[1] = y;
			if (k[6] == 2) //can use scum!
			{
				clearbuf((void *)&templongbuf[z0],z1-z0+1,-3);
				if (typ == 'K')
				{
					oldz = -1;
					i = xyoffs[d[0]][d[1]+1] - xyoffs[d[0]][d[1]]; if (!i) continue;
					while (i > 0)
					{
						z = fgetc(fp); zleng = fgetc(fp); i -= (zleng+3);
						vis = fgetc(fp);

						if ((oldz >= 0) && (!(vis&16)))
							for(;oldz<z;oldz++)
								templongbuf[(oldz^k[7])+k[8]] = vx5.curcol;

						for(;zleng>0;zleng--,z++)
							templongbuf[(z^k[7])+k[8]] = longpal[fgetc(fp)];
						oldz = z;
					}
				}
				else
				{
					for(z=0;z<zsiz;z++)
						templongbuf[(z^k[7])+k[8]] = longpal[fgetc(fp)];
				}

				scum((d[k[0]]^k[1])+k[2],(d[k[3]]^k[4])+k[5],z0,z1+1,templongbuf);
				if (!j) scumfinish();
			}
			else
			{
				if (typ == 'K')
				{
					oldz = -1;
					i = xyoffs[d[0]][d[1]+1] - xyoffs[d[0]][d[1]]; if (!i) continue;
					while (i > 0)
					{
						z = fgetc(fp); zleng = fgetc(fp); i -= (zleng+3);
						vis = fgetc(fp);

						if ((oldz >= 0) && (!(vis&16)))
							for(;oldz<z;oldz++)
							{
								d[2] = oldz;
								setcube((d[k[0]]^k[1])+k[2],(d[k[3]]^k[4])+k[5],(d[k[6]]^k[7])+k[8],vx5.curcol);
							}

						for(;zleng>0;zleng--,z++)
						{
							ch = fgetc(fp);
							d[2] = z;
							setcube((d[k[0]]^k[1])+k[2],(d[k[3]]^k[4])+k[5],(d[k[6]]^k[7])+k[8],longpal[ch]);
						}
						oldz = z;
					}
				}
				else
				{
					for(z=0;z<zsiz;z++)
					{
						ch = fgetc(fp);
						if (((unsigned char*)&ch)[0] != 255)
						{
							d[2] = z;
							setcube((d[k[0]]^k[1])+k[2],(d[k[3]]^k[4])+k[5],(d[k[6]]^k[7])+k[8],longpal[ch]);
						}
					}
				}
			}
		}
	}
	if (j) scumfinish();
	updatebbox(vx5.minx,vx5.miny,vx5.minz,vx5.maxx,vx5.maxy,vx5.maxz,0);

	fclose(fp);
}

	//This here for game programmer only. I would never use it!
/**
 * Draw a pixel on the screen.
 */
void drawpoint2d (int sx, int sy, int col)
{
	if ((unsigned int)sx >= (unsigned int)xres_voxlap) return;
	if ((unsigned int)sy >= (unsigned int)yres_voxlap) return;
	*(int *)(ylookup[sy]+(sx<<2)+frameplace) = col;
}

	//This here for game programmer only. I would never use it!
/**
 * Draw a pixel on the screen. (specified by VXL location) Ignores Z-buffer.
 */
void drawpoint3d (float x0, float y0, float z0, int col)
{
	float ox, oy, oz, r;
	int x, y;

	ox = x0-gipos.x; oy = y0-gipos.y; oz = z0-gipos.z;
	z0 = ox*gifor.x + oy*gifor.y + oz*gifor.z; if (z0 < SCISDIST) return;
	r = 1.0f / z0;
	x0 = (ox*gistr.x + oy*gistr.y + oz*gistr.z)*gihz;
	y0 = (ox*gihei.x + oy*gihei.y + oz*gihei.z)*gihz;

	ftol(x0*r + gihx-.5f,&x); if ((unsigned int)x >= (unsigned int)xres_voxlap) return;
	ftol(y0*r + gihy-.5f,&y); if ((unsigned int)y >= (unsigned int)yres_voxlap) return;
	*(int *)(ylookup[y]+(x<<2)+frameplace) = col;
}

/**
 * Transform & Project a 3D point to a 2D screen coordinate. This could be
 * used for flat sprites (those cardboard-cutouts from old games)
 *
 * @param x VXL location to transform & project
 * @param y VXL location to transform & project
 * @param z VXL location to transform & project
 * @param px screen coordinate returned
 * @param py screen coordinate returned
 * @param sx depth of screen coordinate
 * @return 1 if visible else 0
 */

int project2d (float x, float y, float z, float *px, float *py, float *sx)
{
	float ox, oy, oz;

	ox = x-gipos.x; oy = y-gipos.y; oz = z-gipos.z;
	z = ox*gifor.x + oy*gifor.y + oz*gifor.z; if (z < SCISDIST) return (0);

	z = gihz / z;
	*px = (ox*gistr.x + oy*gistr.y + oz*gistr.z)*z + gihx;
	*py = (ox*gihei.x + oy*gihei.y + oz*gihei.z)*z + gihy;
	*sx = z;
	return(1);
}

static int64_t mskp255 = 0x00ff00ff00ff00ff;
static int64_t mskn255 = 0xff01ff01ff01ff01;
static int64_t rgbmask64 = 0xffffff00ffffff;
/**
 * Draws a 32-bit color texture from memory to the screen. This is the
 * low-level function used to draw text loaded from a PNG,JPG,TGA,GIF.
 *
 * @param tf pointer to top-left corner of SOURCE picture
 * @param tp pitch (bytes per line) of the SOURCE picture
 * @param tx dimensions of the SOURCE picture
 * @param ty dimensions of the SOURCE picture
 * @param tcx texel (<<16) at (sx,sy). Set this to (0,0) if you want (sx,sy)
 *            to be the top-left corner of the destination
 * @param tcy texel (<<16) at (sx,sy). Set this to (0,0) if you want (sx,sy)
 *            to be the top-left corner of the destination
 * @param sx screen coordinate (matches the texture at tcx,tcy)
 * @param sy screen coordinate (matches the texture at tcx,tcy)
 * @param xz x&y zoom, all (<<16). Use (65536,65536) for no zoom change
 * @param yz x&y zoom, all (<<16). Use (65536,65536) for no zoom change
 * @param black shade scale (ARGB format). For no effects, use (0,-1)
 *              NOTE: if alphas of black&white are same, then alpha channel ignored
 * @param white shade scale (ARGB format). For no effects, use (0,-1)
 *              NOTE: if alphas of black&white are same, then alpha channel ignored
 */
void drawtile (int tf, int tp, int tx, int ty, int tcx, int tcy,
					int sx, int sy, int xz, int yz, int black, int white)
{
	int sx0, sy0, sx1, sy1, x0, y0, x1, y1, x, y, u, v, ui, vi, vv;
	int i, j, a;
	unsigned char *p;
	size_t uu;

	if (!tf) return;
	sx0 = sx - mulshr16(tcx,xz); sx1 = sx0 + xz*tx;
	sy0 = sy - mulshr16(tcy,yz); sy1 = sy0 + yz*ty;
	x0 = MAX((sx0+65535)>>16,0); x1 = MIN((sx1+65535)>>16,xres_voxlap);
	y0 = MAX((sy0+65535)>>16,0); y1 = MIN((sy1+65535)>>16,yres_voxlap);
	ui = shldiv16(65536,xz); u = mulshr16(-sx0,ui);
	vi = shldiv16(65536,yz); v = mulshr16(-sy0,vi);
	if (!((black^white)&0xff000000)) //Ignore alpha
	{
		for(y=y0,vv=y*vi+v;y<y1;y++,vv+=vi)
		{
			p = ylookup[y] + frameplace; j = (vv>>16)*tp + tf;
			for(x=x0,uu=x*ui+u;x<x1;x++,uu+=ui)
			*(int *)(p+(x<<2)) = *(int *)(((uu>>16)<<2) + j);
		}
	}
	else //Use alpha for masking
	{
		for(y=y0,vv=y*vi+v;y<y1;y++,vv+=vi)
		{
			p = ylookup[y] + frameplace; j = (vv>>16)*tp + tf;
			for(x=x0,uu=x*ui+u;x<x1;x++,uu+=ui)
			{
				i = *(int *)(((uu>>16)<<2) + j);

				((uint8_t *)&i)[0] = ((uint8_t *)&i)[0] * (((uint8_t *)&white)[0] - ((uint8_t *)&black)[0])/256 + ((uint8_t *)&black)[0];
				((uint8_t *)&i)[1] = ((uint8_t *)&i)[1] * (((uint8_t *)&white)[1] - ((uint8_t *)&black)[1])/256 + ((uint8_t *)&black)[1];
				((uint8_t *)&i)[2] = ((uint8_t *)&i)[2] * (((uint8_t *)&white)[2] - ((uint8_t *)&black)[2])/256 + ((uint8_t *)&black)[2];
				((uint8_t *)&i)[3] = ((uint8_t *)&i)[3] * (((uint8_t *)&white)[3] - ((uint8_t *)&black)[3])/256 + ((uint8_t *)&black)[3];
			}
		}
	}
}
/**
 * Draw a 2d line on the screen
 */
void drawline2d (float x1, float y1, float x2, float y2, int col)
{
	float dx, dy, fxresm1, fyresm1;
	int i, j, incr, ie;

	dx = x2-x1; dy = y2-y1; if ((dx == 0) && (dy == 0)) return;
	fxresm1 = (float)xres_voxlap-.5; fyresm1 = (float)yres_voxlap-.5;
	if (x1 >= fxresm1) { if (x2 >= fxresm1) return; y1 += (fxresm1-x1)*dy/dx; x1 = fxresm1; }
	else if (x1 < 0) { if (x2 < 0) return; y1 += (0-x1)*dy/dx; x1 = 0; }
	if (x2 >= fxresm1) { y2 += (fxresm1-x2)*dy/dx; x2 = fxresm1; }
	else if (x2 < 0) { y2 += (0-x2)*dy/dx; x2 = 0; }
	if (y1 >= fyresm1) { if (y2 >= fyresm1) return; x1 += (fyresm1-y1)*dx/dy; y1 = fyresm1; }
	else if (y1 < 0) { if (y2 < 0) return; x1 += (0-y1)*dx/dy; y1 = 0; }
	if (y2 >= fyresm1) { x2 += (fyresm1-y2)*dx/dy; y2 = fyresm1; }
	else if (y2 < 0) { x2 += (0-y2)*dx/dy; y2 = 0; }

	if (fabs(dx) >= fabs(dy))
	{
		if (x2 > x1) { ftol(x1,&i); ftol(x2,&ie); } else { ftol(x2,&i); ftol(x1,&ie); }
		if (i < 0) i = 0; if (ie >= xres_voxlap) ie = xres_voxlap-1;
		ftol(1048576.0*dy/dx,&incr); ftol(y1*1048576.0+((float)i+.5f-x1)*incr,&j);
		for(;i<=ie;i++,j+=incr)
			if ((unsigned int)(j>>20) < (unsigned int)yres_voxlap)
				*(int *)(ylookup[j>>20]+(i<<2)+frameplace) = col;
	}
	else
	{
		if (y2 > y1) { ftol(y1,&i); ftol(y2,&ie); } else { ftol(y2,&i); ftol(y1,&ie); }
		if (i < 0) i = 0; if (ie >= yres_voxlap) ie = yres_voxlap-1;
		ftol(1048576.0*dx/dy,&incr); ftol(x1*1048576.0+((float)i+.5f-y1)*incr,&j);
		for(;i<=ie;i++,j+=incr)
			if ((unsigned int)(j>>20) < (unsigned int)xres_voxlap)
				*(int *)(ylookup[i]+((j>>18)&~3)+frameplace) = col;
	}
}

#if (USEZBUFFER == 1)
void drawline2dclip (float x1, float y1, float x2, float y2, float rx0, float ry0, float rz0, float rx1, float ry1, float rz1, int col)
{
	float dx, dy, fxresm1, fyresm1, Za, Zb, Zc, z;
	int i, j, incr, ie;
	unsigned char *p;

	dx = x2-x1; dy = y2-y1; if ((dx == 0) && (dy == 0)) return;
	fxresm1 = (float)xres_voxlap-.5; fyresm1 = (float)yres_voxlap-.5;
	if (x1 >= fxresm1) { if (x2 >= fxresm1) return; y1 += (fxresm1-x1)*dy/dx; x1 = fxresm1; }
	else if (x1 < 0) { if (x2 < 0) return; y1 += (0-x1)*dy/dx; x1 = 0; }
	if (x2 >= fxresm1) { y2 += (fxresm1-x2)*dy/dx; x2 = fxresm1; }
	else if (x2 < 0) { y2 += (0-x2)*dy/dx; x2 = 0; }
	if (y1 >= fyresm1) { if (y2 >= fyresm1) return; x1 += (fyresm1-y1)*dx/dy; y1 = fyresm1; }
	else if (y1 < 0) { if (y2 < 0) return; x1 += (0-y1)*dx/dy; y1 = 0; }
	if (y2 >= fyresm1) { x2 += (fyresm1-y2)*dx/dy; y2 = fyresm1; }
	else if (y2 < 0) { x2 += (0-y2)*dx/dy; y2 = 0; }

	if (fabs(dx) >= fabs(dy))
	{
			//Original equation: (rz1*t+rz0) / (rx1*t+rx0) = gihz/(sx-gihx)
		Za = gihz*(rx0*rz1 - rx1*rz0); Zb = rz1; Zc = -gihx*rz1 - gihz*rx1;

		if (x2 > x1) { ftol(x1,&i); ftol(x2,&ie); } else { ftol(x2,&i); ftol(x1,&ie); }
		if (i < 0) i = 0; if (ie >= xres_voxlap) ie = xres_voxlap-1;
		ftol(1048576.0*dy/dx,&incr); ftol(y1*1048576.0+((float)i+.5f-x1)*incr,&j);
		for(;i<=ie;i++,j+=incr)
			if ((unsigned int)(j>>20) < (unsigned int)yres_voxlap)
			{
				p = ylookup[j>>20]+(i<<2)+frameplace;
				z = Za / ((float)i*Zb + Zc);
				if (*(int *)&z >= *(int *)(p+zbufoff)) continue;
				*(int *)(p+zbufoff) = *(int *)&z;
				*(int *)p = col;
			}
	}
	else
	{
		Za = gihz*(ry0*rz1 - ry1*rz0); Zb = rz1; Zc = -gihy*rz1 - gihz*ry1;

		if (y2 > y1) { ftol(y1,&i); ftol(y2,&ie); } else { ftol(y2,&i); ftol(y1,&ie); }
		if (i < 0) i = 0; if (ie >= yres_voxlap) ie = yres_voxlap-1;
		ftol(1048576.0*dx/dy,&incr); ftol(x1*1048576.0+((float)i+.5f-y1)*incr,&j);
		for(;i<=ie;i++,j+=incr)
			if ((unsigned int)(j>>20) < (unsigned int)xres_voxlap)
			{
				p = ylookup[i]+((j>>18)&~3)+frameplace;
				z = Za / ((float)i*Zb + Zc);
				if (*(int *)&z >= *(int *)(p+zbufoff)) continue;
				*(int *)(p+zbufoff) = *(int *)&z;
				*(int *)p = col;
			}
	}
}
#endif
/**
 * Draw a 3d line on the screen (specified by VXL location).
 * Line is automatically Z-buffered into the map.
 * Set alpha of col to non-zero to disable Z-buffering
 */
void drawline3d (float x0, float y0, float z0, float x1, float y1, float z1, int col)
{
	float ox, oy, oz, r;

	ox = x0-gipos.x; oy = y0-gipos.y; oz = z0-gipos.z;
	x0 = ox*gistr.x + oy*gistr.y + oz*gistr.z;
	y0 = ox*gihei.x + oy*gihei.y + oz*gihei.z;
	z0 = ox*gifor.x + oy*gifor.y + oz*gifor.z;

	ox = x1-gipos.x; oy = y1-gipos.y; oz = z1-gipos.z;
	x1 = ox*gistr.x + oy*gistr.y + oz*gistr.z;
	y1 = ox*gihei.x + oy*gihei.y + oz*gihei.z;
	z1 = ox*gifor.x + oy*gifor.y + oz*gifor.z;

	if (z0 < SCISDIST)
	{
		if (z1 < SCISDIST) return;
		r = (SCISDIST-z0)/(z1-z0); z0 = SCISDIST;
		x0 += (x1-x0)*r; y0 += (y1-y0)*r;
	}
	else if (z1 < SCISDIST)
	{
		r = (SCISDIST-z1)/(z1-z0); z1 = SCISDIST;
		x1 += (x1-x0)*r; y1 += (y1-y0)*r;
	}

	ox = gihz/z0;
	oy = gihz/z1;

#if (USEZBUFFER == 1)
	if (!(col&0xff000000))
		drawline2dclip(x0*ox+gihx,y0*ox+gihy,x1*oy+gihx,y1*oy+gihy,x0,y0,z0,x1-x0,y1-y0,z1-z0,col);
	else
		drawline2d(x0*ox+gihx,y0*ox+gihy,x1*oy+gihx,y1*oy+gihy,col&0xffffff);
#else
	drawline2d(x0*ox+gihx,y0*ox+gihy,x1*oy+gihx,y1*oy+gihy,col);
#endif
}

//--------------------------  Name hash code begins --------------------------

	//khashbuf format: (used by getkv6/getkfa to avoid duplicate loads)
	//[int index to next hash or -1][pointer to struct][char type]string[\0]
	//[int index to next hash or -1][pointer to struct][chat type]string[\0]
	//...
	//type:0 = kv6data
	//type:1 = kfatype
#define KHASHINITSIZE 8192
static char *khashbuf = 0;
static int khashead[256], khashpos = 0, khashsiz = 0;

/**
 * Returns a pointer to the filename associated with the kv6data/kfatype
 * object. Notice that each structure has a "namoff" member. Since I
 * use remalloc(), I have to make these offsets, not pointers. Use this
 * function to convert the offsets into pointers.
 *
 * @param namoff offset to the name
 */
char *getkfilname (int namoff) { return(&khashbuf[namoff]); }

	//Returns: 0,retptr=-1: Error! (bad filename or out of memory)
	//         0,retptr>=0: Not in hash; new name allocated, valid index
	//         1,retptr>=0: Already in hash, valid index
	//   Uses a 256-entry hash to compare names very quickly.
static int inkhash (const char *filnam, int *retind)
{
	int i, j, hashind;

	(*retind) = -1;

	if (!filnam) return(0);
	j = strlen(filnam); if (!j) return(0);
	j += 10;
	if (khashpos+j > khashsiz) //Make sure string fits in khashbuf
	{
		i = khashsiz; do { i <<= 1; } while (khashpos+j > i);
		if (!(khashbuf = (char *)realloc(khashbuf,i))) return(0);
		khashsiz = i;
	}

		//Copy filename to avoid destroying original string
		//Also, calculate hash index (which hopefully is uniformly random :)
	strcpy(&khashbuf[khashpos+9],filnam);
	for(i=khashpos+9,hashind=0;khashbuf[i];i++)
	{
		if ((khashbuf[i] >= 'a') && (khashbuf[i] <= 'z')) khashbuf[i] -= 32;
		if (khashbuf[i] == '/') khashbuf[i] = '\\';
		hashind = (khashbuf[i] - hashind*3);
	}
	hashind %= (sizeof(khashead)/sizeof(khashead[0]));

		//Find if string is already in hash...
	for(i=khashead[hashind];i>=0;i=(*(int *)&khashbuf[i]))
		if (!strcmp(&khashbuf[i+9],&khashbuf[khashpos+9]))
			{ (*retind) = i; return(1); } //Early out: already in hash

	(*retind) = khashpos;
	*(int *)&khashbuf[khashpos] = khashead[hashind];
	*(int *)&khashbuf[khashpos+4] = 0; //Set to 0 just in case load fails
	khashead[hashind] = khashpos; khashpos += j;
	return(0);
}

//-------------------------- KV6 sprite code begins --------------------------

//EQUIVEC code begins -----------------------------------------------------
static point3d univec[256];
static __ALIGN(8) short iunivec[256][4];

typedef struct
{
	float fibx[45], fiby[45];
	float azval[20], zmulk, zaddk;
	int fib[47], aztop, npoints;
} equivectyp;
static equivectyp equivec;

void equiind2vec (int i, float *x, float *y, float *z)
{
	float r;
	(*z) = (float)i*equivec.zmulk + equivec.zaddk; r = sqrt(1.f - (*z)*(*z));
	fcossin((float)i*(GOLDRAT*PI*2),x,y); (*x) *= r; (*y) *= r;
}

	//Very fast; good quality
int equivec2indmem (float x, float y, float z)
{
	int b, i, j, k, bestc;
	float xy, zz, md, d;

	xy = atan2(y,x); //atan2 is 150 clock cycles!
	j = ((*(int *)&z)&0x7fffffff);
	bestc = equivec.aztop;
	do
	{
		if (j < *(int *)&equivec.azval[bestc]) break;
		bestc--;
	} while (bestc);

	zz = z + 1.f;
	ftol(equivec.fibx[bestc]*xy + equivec.fiby[bestc]*zz - .5,&i);
	bestc++;
	ftol(equivec.fibx[bestc]*xy + equivec.fiby[bestc]*zz - .5,&j);

	k = dmulshr0(equivec.fib[bestc+2],i,equivec.fib[bestc+1],j);
	if ((unsigned int)k < equivec.npoints)
	{
		md = univec[k].x*x + univec[k].y*y + univec[k].z*z;
		j = k;
	} else md = -2.f;
	b = bestc+3;
	do
	{
		i = equivec.fib[b] + k;
		if ((unsigned int)i < equivec.npoints)
		{
			d = univec[i].x*x + univec[i].y*y + univec[i].z*z;
			if (*(int *)&d > *(int *)&md) { md = d; j = i; }
		}
		b--;
	} while (b != bestc);
	return(j);
}

void equivecinit (int n)
{
	float t0, t1;
	int z;

		//Init constants for ind2vec
	equivec.npoints = n;
	equivec.zmulk = 2 / (float)n; equivec.zaddk = equivec.zmulk*.5 - 1.0;

		//equimemset
	for(z=n-1;z>=0;z--)
		equiind2vec(z,&univec[z].x,&univec[z].y,&univec[z].z);
	if (n&1) //Hack for when n=255 and want a <0,0,0> vector
		{ univec[n].x = univec[n].y = univec[n].z = 0; }

		//Init Fibonacci table
	equivec.fib[0] = 0; equivec.fib[1] = 1;
	for(z=2;z<47;z++) equivec.fib[z] = equivec.fib[z-2]+equivec.fib[z-1];

		//Init fibx/y LUT
	t0 = .5 / PI; t1 = (float)n * -.5;
	for(z=0;z<45;z++)
	{
		t0 = -t0; equivec.fibx[z] = (float)equivec.fib[z+2]*t0;
		t1 = -t1; equivec.fiby[z] = ((float)equivec.fib[z+2]*GOLDRAT - (float)equivec.fib[z])*t1;
	}

	t0 = 1 / ((float)n * PI);
	for(equivec.aztop=0;equivec.aztop<20;equivec.aztop++)
	{
		t1 = 1 - (float)equivec.fib[(equivec.aztop<<1)+6]*t0; if (t1 < 0) break;
		equivec.azval[equivec.aztop+1] = sqrt(t1);
	}
}

//EQUIVEC code ends -------------------------------------------------------

static int umulmip[9] =
{
	(int)0u,(int)4294967295u,(int)2147483648u,(int)1431655765u,(int)1073741824u,
	(int)858993459u,(int)715827882u,(int)613566756u,(int)536870912u
};

/**
 * Generate 1 more mip-level for a .KV6 sprite. This function generates a
 * lower MIP level only if kv6->lowermip is NULL, and kv6->xsiz,
 * kv6->ysiz, and kv6->zsiz are all >= 3. When these conditions are
 * true, it will generate a new .KV6 sprite with half the resolution in
 * all 3 dimensions. It will set kv6->lowermip so it points to the newly
 * generated .KV6 object. You can use freekv6() to de-allocate all levels
 * of the .KV6 object.
 *
 * To generate all mip levels use this pseudo-code:
 * for(kv6data *tempkv6=mykv6;tempkv6=genmipkv6(tempkv6););
 *
 * @param kv6 pointer to current MIP-level
 * @return pointer to newly generated half-size MIP-level
 */
kv6data *genmipkv6 (kv6data *kv6)
{
	kv6data *nkv6;
	kv6voxtype *v0[2], *vs[4], *ve[4], *voxptr;
	unsigned short *xyptr, *xyi2, *sxyi2;
	int i, j, x, y, z, xs, ys, zs, xysiz, n, oxn, oxyn, *xptr;
	int xx, yy, zz, r, g, b, vis, npix, sxyi2i, darand = 0;
	char vecbuf[8];

	if ((!kv6) || (kv6->lowermip)) return(0);

	xs = ((kv6->xsiz+1)>>1); ys = ((kv6->ysiz+1)>>1); zs = ((kv6->zsiz+1)>>1);
	if ((xs < 2) || (ys < 2) || (zs < 2)) return(0);
	xysiz = ((((xs*ys)<<1)+3)&~3);
	i = sizeof(kv6data) + (xs<<2) + xysiz + kv6->numvoxs*sizeof(kv6voxtype);
	nkv6 = (kv6data *)malloc(i);
	if (!nkv6) return(0);

	kv6->lowermip = nkv6;
	nkv6->xsiz = xs;
	nkv6->ysiz = ys;
	nkv6->zsiz = zs;
	nkv6->xpiv = kv6->xpiv*.5;
	nkv6->ypiv = kv6->ypiv*.5;
	nkv6->zpiv = kv6->zpiv*.5;
	nkv6->namoff = 0;
	nkv6->lowermip = 0;

	xptr = (int *)(((size_t)nkv6) + sizeof(kv6data));
	xyptr = (unsigned short *)(((size_t)xptr) + (xs<<2));
	voxptr = (kv6voxtype *)(((size_t)xyptr) + xysiz);
	n = 0;

	v0[0] = kv6->vox; sxyi2 = kv6->ylen; sxyi2i = (kv6->ysiz<<1);
	for(x=0;x<xs;x++)
	{
		v0[1] = v0[0]+kv6->xlen[x<<1];

			//vs: start pointer of each of the 4 columns
			//ve: end pointer of each of the 4 columns
		vs[0] = v0[0]; vs[2] = v0[1];

		xyi2 = sxyi2; sxyi2 += sxyi2i;

		oxn = n;
		for(y=0;y<ys;y++)
		{
			oxyn = n;

			ve[0] = vs[1] = vs[0]+xyi2[0];
			if ((x<<1)+1 < kv6->xsiz) { ve[2] = vs[3] = vs[2]+xyi2[kv6->ysiz]; }
			if ((y<<1)+1 < kv6->ysiz)
			{
				ve[1] = vs[1]+xyi2[1];
				if ((x<<1)+1 < kv6->xsiz) ve[3] = vs[3]+xyi2[kv6->ysiz+1];
			}
			xyi2 += 2;

			while (1)
			{
				z = 0x7fffffff;
				for(i=3;i>=0;i--)
					if ((vs[i] < ve[i]) && (vs[i]->z < z)) z = vs[i]->z;
				if (z == 0x7fffffff) break;

				z |= 1;

				r = g = b = vis = npix = 0;
				for(i=3;i>=0;i--)
					for(zz=z-1;zz<=z;zz++)
					{
						if ((vs[i] >= ve[i]) || (vs[i]->z > zz)) continue;
						r += (vs[i]->col&0xff00ff); //MMX-style trick!
						g += (vs[i]->col&  0xff00);
						//b += (vs[i]->col&    0xff);
						vis |= vs[i]->vis;
						vecbuf[npix] = vs[i]->dir;
						npix++; vs[i]++;
					}

				if (npix)
				{
					if (n >= kv6->numvoxs) return(0); //Don't let it crash!

					i = umulmip[npix]; j = (npix>>1);
					voxptr[n].col = (umulshr32(r+(j<<16),i)&0xff0000) +
										 (umulshr32(g+(j<< 8),i)&  0xff00) +
										 (umulshr32((r&0xfff)+ j     ,i));
					voxptr[n].z = (z>>1);
					voxptr[n].vis = vis;
					voxptr[n].dir = vecbuf[umulshr32(darand,npix)]; darand += i;
					n++;
				}
			}
			xyptr[0] = n-oxyn; xyptr++;
			vs[0] = ve[1]; vs[2] = ve[3];
		}
		xptr[x] = n-oxn;
		if ((x<<1)+1 >= kv6->xsiz) break; //Avoid read page fault
		v0[0] = v0[1]+kv6->xlen[(x<<1)+1];
	}

	nkv6->leng = sizeof(kv6data) + (xs<<2) + xysiz + n*sizeof(kv6voxtype);
	nkv6 = (kv6data *)realloc(nkv6,nkv6->leng); if (!nkv6) return(0);
	nkv6->xlen = (unsigned int *)(((size_t)nkv6) + sizeof(kv6data));
	nkv6->ylen = (unsigned short *)(((size_t)nkv6->xlen) + (xs<<2));
	nkv6->vox = (kv6voxtype *)(((size_t)nkv6->ylen) + xysiz);
	nkv6->numvoxs = n;
	kv6->lowermip = nkv6;
	return(nkv6);
}
/**
 * This could be a handy function for debugging I suppose. Use it to save
 * .KV6 sprites to disk.
 *
 * @param filnam filename of .KV6 to save to disk. It's your responsibility to
 *               make sure it doesn't overwrite a file of the same name.
 * @param kv pointer to .KV6 object to save to disk.
 */
void savekv6 (const char *filnam, kv6data *kv)
{
	FILE *fil;
	int i;

	if ((fil = fopen(filnam,"wb")))
	{
		i = 0x6c78764b; fwrite(&i,4,1,fil); //Kvxl
		fwrite(&kv->xsiz,4,1,fil); fwrite(&kv->ysiz,4,1,fil); fwrite(&kv->zsiz,4,1,fil);
		fwrite(&kv->xpiv,4,1,fil); fwrite(&kv->ypiv,4,1,fil); fwrite(&kv->zpiv,4,1,fil);
		fwrite(&kv->numvoxs,4,1,fil);
		fwrite(kv->vox,kv->numvoxs*sizeof(kv6voxtype),1,fil);
		fwrite(kv->xlen,kv->xsiz*sizeof(int),1,fil);
		fwrite(kv->ylen,kv->xsiz*kv->ysiz*sizeof(short),1,fil);
		fclose(fil);
	}
}

	//NOTE: should make this inline to getkv6!
kv6data *loadkv6 (const char *filnam)
{
	FILE *fil;
	kv6data tk, *newkv6;
	int i;
	fil=fopen(filnam, "rb");

	if (!fil)
	{
			//File not found, but allocate a structure anyway
			//   so it can keep track of the filename
		if (!(newkv6 = (kv6data *)malloc(sizeof(kv6data)))) return(0);
		newkv6->leng = sizeof(kv6data);
		newkv6->xsiz = newkv6->ysiz = newkv6->zsiz = 0;
		newkv6->xpiv = newkv6->ypiv = newkv6->zpiv = 0;
		newkv6->numvoxs = 0;
		newkv6->namoff = 0;
		newkv6->lowermip = 0;
		newkv6->vox = (kv6voxtype *)(((size_t)newkv6)+sizeof(kv6data));
		newkv6->xlen = (unsigned int *)newkv6->vox;
		newkv6->ylen = (unsigned short *)newkv6->xlen;
		return(newkv6);
	}

	fread(&tk, 32, 1, fil);

	i = tk.numvoxs*sizeof(kv6voxtype) + tk.xsiz*4 + tk.xsiz*tk.ysiz*2;
	newkv6 = (kv6data *)malloc(i+sizeof(kv6data));
	if (!newkv6) { fclose(fil); return(0); }
	if (((size_t)newkv6)&3) evilquit("getkv6 malloc not 32-bit aligned!");

	newkv6->leng = i+sizeof(kv6data);
	memcpy(&newkv6->xsiz,&tk.xsiz,28);
	newkv6->namoff = 0;
	newkv6->lowermip = 0;
	newkv6->vox = (kv6voxtype *)(((size_t)newkv6)+sizeof(kv6data));
	newkv6->xlen = (unsigned int *)(((size_t)newkv6->vox)+tk.numvoxs*sizeof(kv6voxtype));
	newkv6->ylen = (unsigned short *)(((size_t)newkv6->xlen) + tk.xsiz*4);

	fread(newkv6->vox, i, 1, fil);
	fclose(fil);
	return(newkv6);
}

/**
 * Loads a .KV6 voxel sprite into memory. It malloc's the array for you and
 * returns the pointer to the loaded vx5sprite. If the same filename was
 * passed before to this function, it will return the pointer to the
 * previous instance of the .KV6 buffer in memory (It will NOT load the
 * same file twice). Uninitvoxlap() de-allocates all .KV6 sprites for
 * you.
 *
 * Other advanced info: Uses a 256-entry hash table to compare filenames, so
 * it should be fast. If you want to modify a .KV6 without affecting all
 * instances, you must allocate&de-allocate your own kv6data structure,
 * and use memcpy. The buffer is kv6data.leng bytes int (inclusive).
 *
 * Cover-up function for LOADKV6: returns a pointer to the loaded kv6data
 * structure. Loads file only if not already loaded before with getkv6.
 *
 * @param kv6nam .KV6 filename
 * @return pointer to malloc'ed kv6data structure. Do NOT free this buffer
 *         yourself! Returns 0 if there's an error - such as bad filename.
 */
VOXLAP_DLL_FUNC kv6data *getkv6 (const char *filnam)
{
	kv6data *kv6ptr;
	int i;

	if (inkhash(filnam,&i)) return(*(kv6data **)&khashbuf[i+4]);
	if (i == -1) return(0);

	if ((kv6ptr = loadkv6((char *)&khashbuf[i+9])))
		kv6ptr->namoff = i+9; //Must use offset for ptr->name conversion

	*(kv6data **)&khashbuf[i+4] = kv6ptr;
	*(char *)&khashbuf[i+8] = 0; //0 for KV6
	return(kv6ptr);
}

#define MAXZSIZ 1024
	//variables now initialized here and not in assembly
EXTERN_C __ALIGN(16) point4d caddasm[8]         = {};
EXTERN_C __ALIGN(16) point4d ztabasm[MAXZSIZ+3] = {};
EXTERN_C __ALIGN(16) unsigned short qsum0[4]    = {}; //[8000h-hy,8000h-hx,8000h-hy,8000h-hx]
EXTERN_C __ALIGN(16) unsigned short qsum1[4]    = {}; //[8000h-fy,8000h-fx,8000h-fy,8000h-fx]
EXTERN_C __ALIGN(16) unsigned short qbplbpp[4]  = {}; //[0,0,bpl,bpp]
EXTERN_C __ALIGN(16) int64_t kv6colmul[256]     = {};
EXTERN_C __ALIGN(16) int64_t kv6coladd[256]     = {};
EXTERN_C __ALIGN(16) int kv6frameplace         =  0;
EXTERN_C __ALIGN(16) int kv6bytesperline       =  0;
EXTERN_C __ALIGN(16) float scisdist             =  0;
//((uint8_t *)&scisdist)[4] = { 40800000h,0,0,0};

//EXTERN_C point4d caddasm[8];
//EXTERN_C point4d ztabasm[MAXZSIZ+3];

#ifdef __cplusplus
extern "C" {
#endif

char ptfaces16[43][8] =
{
	{0, 0, 0,  0,  0, 0, 0,0} , {4, 0,32,96, 64, 0,32,0} , {4,16,80,112,48, 16,80,0} , {0,0,0,0,0,0,0,0} ,
	{4,64,96,112, 80,64,96,0} , {6, 0,32,96,112,80,64,0} , {6,16,80, 64,96,112,48,0} , {0,0,0,0,0,0,0,0} ,
	{4, 0,16, 48, 32, 0,16,0} , {6, 0,16,48, 32,96,64,0} , {6, 0,16, 80,112,48,32,0} , {0,0,0,0,0,0,0,0} ,
	{0, 0, 0,  0,  0, 0, 0,0} , {0, 0, 0, 0,  0, 0, 0,0} , {0, 0, 0,  0,  0, 0, 0,0} , {0,0,0,0,0,0,0,0} ,
	{4, 0,64, 80, 16, 0,64,0} , {6, 0,32,96, 64,80,16,0} , {6, 0,64, 80,112,48,16,0} , {0,0,0,0,0,0,0,0} ,
	{6, 0,64, 96,112,80,16,0} , {6, 0,32,96,112,80,16,0} , {6, 0,64, 96,112,48,16,0} , {0,0,0,0,0,0,0,0} ,
	{6, 0,64, 80, 16,48,32,0} , {6,16,48,32, 96,64,80,0} , {6, 0,64, 80,112,48,32,0} , {0,0,0,0,0,0,0,0} ,
	{0, 0, 0,  0,  0, 0, 0,0} , {0, 0, 0, 0,  0, 0, 0,0} , {0, 0, 0,  0,  0, 0, 0,0} , {0,0,0,0,0,0,0,0} ,
	{4,32,48,112, 96,32,48,0} , {6, 0,32,48,112,96,64,0} , {6,16,80,112, 96,32,48,0} , {0,0,0,0,0,0,0,0} ,
	{6,32,48,112, 80,64,96,0} , {6, 0,32,48,112,80,64,0} , {6,16,80, 64, 96,32,48,0} , {0,0,0,0,0,0,0,0} ,
	{6, 0,16, 48,112,96,32,0} , {6, 0,16,48,112,96,64,0} , {6, 0,16, 80,112,96,32,0}
};

extern void drawboundcubesseinit ();
void drawboundcubesse (kv6voxtype *, int);
void drawboundcube3dninit ();
void drawboundcube3dn (kv6voxtype *, int);

#ifdef __cplusplus
}
#endif

//static void initboundcubescr (int dafram, int dabpl, int x, int y, int dabpp)
//{
//   qsum1[3] = qsum1[1] = 0x7fff-y; qsum1[2] = qsum1[0] = 0x7fff-x;
//   qbplbpp[1] = dabpl; qbplbpp[0] = ((dabpp+7)>>3);
//   kv6frameplace = dafram; kv6bytesperline = dabpl;
//}

static __ALIGN(8) short lightlist[MAXLIGHTS+1][4];
static int64_t all32767 = 0x7fff7fff7fff7fff;

static inline void movps (point4d *dest, point4d *src)
{
	*dest = *src;
}

static inline void intss (point4d *dest, int src)
{
	dest->x = dest->y = dest->z = dest->z2 = (float)src;
}

static inline void addps (point4d *sum, point4d *a, point4d *b)
{
	sum->x  =  a->x  +  b->x;
	sum->y  =  a->y  +  b->y;
	sum->z  =  a->z  +  b->z;
	sum->z2 =  a->z2 +  b->z2;
}

static inline void mulps (point4d *sum, point4d *a, point4d *b)
{
	sum->x  =  a->x  *  b->x;
	sum->y  =  a->y  *  b->y;
	sum->z  =  a->z  *  b->z;
	sum->z2 =  a->z2 *  b->z2;
}

static inline void subps (point4d *sum, point4d *a, point4d *b)
{
	sum->x  =  a->x  -  b->x;
	sum->y  =  a->y  -  b->y;
	sum->z  =  a->z  -  b->z;
	sum->z2 =  a->z2 -  b->z2;
}

static inline void minps (point4d *sum, point4d *a, point4d *b)
{
	sum->x  =  MIN(a->x,  b->x);
	sum->y  =  MIN(a->y,  b->y);
	sum->z  =  MIN(a->z,  b->z);
	sum->z2 =  MIN(a->z2, b->z2);
}

static inline void maxps (point4d *sum, point4d *a, point4d *b)
{
	sum->x  =  MAX(a->x,  b->x);
	sum->y  =  MAX(a->y,  b->y);
	sum->z  =  MAX(a->z,  b->z);
	sum->z2 =  MAX(a->z2, b->z2);

}

//-------------------------- KFA sprite code begins --------------------------

static kv6voxtype *getvptr (kv6data *kv, int x, int y)
{
	kv6voxtype *v;
	int i, j;

	v = kv->vox;
	if ((x<<1) < kv->xsiz) { for(i=0         ;i< x;i++) v += kv->xlen[i]; }
	else { v += kv->numvoxs; for(i=kv->xsiz-1;i>=x;i--) v -= kv->xlen[i]; }
	j = x*kv->ysiz;
	if ((y<<1) < kv->ysiz) { for(i=0         ;i< y;i++) v += kv->ylen[j+i]; }
	else { v += kv->xlen[x]; for(i=kv->ysiz-1;i>=y;i--) v -= kv->ylen[j+i]; }
	return(v);
}

#define VFIFSIZ 16384 //SHOULDN'T BE STATIC ALLOCATION!!!
static size_t vfifo[VFIFSIZ];
static void floodsucksprite (vx5sprite *spr, kv6data *kv, int ox, int oy,
									  kv6voxtype *v0, kv6voxtype *v1)
{
	kv6voxtype *v, *ve, *ov, *v2, *v3;
	kv6data *kv6;
	int i, j, x, y, z, x0, y0, z0, x1, y1, z1, n, vfif0, vfif1;

	x0 = x1 = ox; y0 = y1 = oy; z0 = v0->z; z1 = v1->z;

	n = (((ptrdiff_t)v1)-((ptrdiff_t)v0))/sizeof(kv6voxtype)+1;
	v1->vis &= ~64;

	vfifo[0] = ox; vfifo[1] = oy;
	vfifo[2] = (size_t)v0; vfifo[3] = (size_t)v1;
	vfif0 = 0; vfif1 = 4;

	while (vfif0 < vfif1)
	{
		i = (vfif0&(VFIFSIZ-1)); vfif0 += 4;
		ox = vfifo[i]; oy = vfifo[i+1];
		v0 = (kv6voxtype *)vfifo[i+2]; v1 = (kv6voxtype *)vfifo[i+3];

		if (ox < x0) x0 = ox;
		if (ox > x1) x1 = ox;
		if (oy < y0) y0 = oy;
		if (oy > y1) y1 = oy;
		if (v0->z < z0) z0 = v0->z;
		if (v1->z > z1) z1 = v1->z;
		for(v=v0;v<=v1;v++) v->vis |= 128; //Mark as part of current piece

		for(j=0;j<4;j++)
		{
			switch(j)
			{
				case 0: x = ox-1; y = oy; break;
				case 1: x = ox+1; y = oy; break;
				case 2: x = ox; y = oy-1; break;
				case 3: x = ox; y = oy+1; break;
				default: _gtfo(); //tells MSVC default can't be reached
			}
			if ((unsigned int)x >= kv->xsiz) continue;
			if ((unsigned int)y >= kv->ysiz) continue;

			v = getvptr(kv,x,y);
			for(ve=&v[kv->ylen[x*kv->ysiz+y]];v<ve;v++)
			{
				if (v->vis&16) ov = v;
				if (((v->vis&(64+32)) == 64+32) && (v0->z <= v->z) && (v1->z >= ov->z))
				{
					i = (vfif1&(VFIFSIZ-1)); vfif1 += 4;
					if (vfif1-vfif0 >= VFIFSIZ) //FIFO Overflow... make entire object 1 piece :/
					{
						for(i=kv->numvoxs-1;i>=0;i--)
						{
							if ((kv->vox[i].vis&(64+32)) == 64+32) { v1 = &kv->vox[i]; v1->vis &= ~64; }
							if (kv->vox[i].vis&16) for(v=&kv->vox[i];v<=v1;v++) kv->vox[i].vis |= 128;
						}
						x0 = y0 = z0 = 0; x1 = kv->xsiz; y1 = kv->ysiz; z1 = kv->zsiz; n = kv->numvoxs;
						goto floodsuckend;
					}
					vfifo[i] = x; vfifo[i+1] = y;
					vfifo[i+2] = (size_t)ov; vfifo[i+3] = (size_t)v;
					n += (((ptrdiff_t)v)-((ptrdiff_t)ov))/sizeof(kv6voxtype)+1;
					v->vis &= ~64;
				}
			}
		}
	}
	x1++; y1++; z1++;
floodsuckend:;

	i = sizeof(kv6data) + n*sizeof(kv6voxtype) + (x1-x0)*4 + (x1-x0)*(y1-y0)*2;
	if (!(kv6 = (kv6data *)malloc(i))) return;
	kv6->leng = i;
	kv6->xsiz = x1-x0;
	kv6->ysiz = y1-y0;
	kv6->zsiz = z1-z0;
	kv6->xpiv = 0; //Set limb pivots to 0 - don't need it twice!
	kv6->ypiv = 0;
	kv6->zpiv = 0;
	kv6->numvoxs = n;
	kv6->namoff = 0;
	kv6->lowermip = 0;
	kv6->vox = (kv6voxtype *)(((size_t)kv6)+sizeof(kv6data));
	kv6->xlen = (unsigned int *)(((size_t)kv6->vox)+n*sizeof(kv6voxtype));
	kv6->ylen = (unsigned short *)(((size_t)kv6->xlen)+(x1-x0)*4);

		//Extract sub-KV6 to newly allocated kv6data
	v3 = kv6->vox; n = 0;
	for(x=0,v=kv->vox;x<x0;x++) v += kv->xlen[x];
	for(;x<x1;x++)
	{
		v2 = v; ox = n;
		for(y=0;y<y0;y++) v += kv->ylen[x*kv->ysiz+y];
		for(;y<y1;y++)
		{
			oy = n;
			for(ve=&v[kv->ylen[x*kv->ysiz+y]];v<ve;v++)
				if (v->vis&128)
				{
					v->vis &= ~128;
					(*v3) = (*v);
					v3->z -= z0;
					v3++; n++;
				}
			kv6->ylen[(x-x0)*(y1-y0)+(y-y0)] = n-oy;
		}
		kv6->xlen[x-x0] = n-ox;
		v = v2+kv->xlen[x];
	}

	spr->p.x = x0-kv->xpiv;
	spr->p.y = y0-kv->ypiv;
	spr->p.z = z0-kv->zpiv;
	spr->s.x = 1; spr->s.y = 0; spr->s.z = 0;
	spr->h.x = 0; spr->h.y = 1; spr->h.z = 0;
	spr->f.x = 0; spr->f.y = 0; spr->f.z = 1;
	spr->voxnum = kv6;
	spr->flags = 0;
}

static char *stripdir (char *filnam)
{
	int i, j;
	for(i=0,j=-1;filnam[i];i++)
		if ((filnam[i] == '/') || (filnam[i] == '\\')) j = i;
	return(&filnam[j+1]);
}

static void kfasorthinge (hingetype *h, int nh, int *hsort)
{
	int i, j, n;

		//First pass: stick hinges with parent=-1 at end
	n = nh; j = 0;
	for(i=n-1;i>=0;i--)
	{
		if (h[i].parent < 0) hsort[--n] = i;
							 else hsort[j++] = i;
	}
		//Finish accumulation (n*log(n) if tree is perfectly balanced)
	while (n > 0)
	{
		i--; if (i < 0) i = n-1;
		j = hsort[i];
		if (h[h[j].parent].parent < 0)
		{
			h[j].parent = -2-h[j].parent; n--;
			hsort[i] = hsort[n]; hsort[n] = j;
		}
	}
		//Restore parents to original values
	for(i=nh-1;i>=0;i--) h[i].parent = -2-h[i].parent;
}

/**
 * Loads a .KFA file and its associated .KV6 voxel sprite into memory. Works
 * just like getkv6() for for .KFA files: (Returns a pointer to the loaded
 * kfatype structure. Loads data only if not already loaded before with
 * getkfa.)
 *
 * @param kfanam .KFA filename
 * @return pointer to malloc'ed kfatype structure. Do NOT free this buffer
 *         yourself! Returns 0 if there's an error - such as bad filename.
 */
/*kfatype *getkfa (const char *kfanam)
{
	kfatype *kfa;
	kv6voxtype *v, *ov, *ve;
	kv6data *kv;
	int i, j, x, y;
	char *cptr, snotbuf[MAX_PATH];

	if (inkhash(kfanam,&i)) return(*(kfatype **)&khashbuf[i+4]);
	if (i == -1) return(0);

	if (!(kfa = (kfatype *)malloc(sizeof(kfatype)))) return(0);
	memset(kfa,0,sizeof(kfatype));

	kfa->namoff = i+9; //Must use offset for ptr->name conversion
	*(kfatype **)&khashbuf[i+4] = kfa;
	*(char *)&khashbuf[i+8] = 1; //1 for KFA

	if (!kzopen(kfanam)) return(0);
	kzread(&i,4); if (i != 0x6b6c774b) { kzclose(); return(0); } //Kwlk
	kzread(&i,4); strcpy(snotbuf,kfanam); cptr = stripdir(snotbuf);
	kzread(cptr,i); cptr[i] = 0;
	kzread(&kfa->numhin,4);

		//Actual allocation for ->spr is numspr, which is <= numhin!
	if (!(kfa->spr = (vx5sprite *)malloc(kfa->numhin*sizeof(vx5sprite)))) { kzclose(); return(0); }

	if (!(kfa->hinge = (hingetype *)malloc(kfa->numhin*sizeof(hingetype)))) { kzclose(); return(0); }
	kzread(kfa->hinge,kfa->numhin*sizeof(hingetype));

	kzread(&kfa->numfrm,4);
	if (!(kfa->frmval = (short *)malloc(kfa->numfrm*kfa->numhin*2))) { kzclose(); return(0); }
	kzread(kfa->frmval,kfa->numfrm*kfa->numhin*2);

	kzread(&kfa->seqnum,4);
	if (!(kfa->seq = (seqtyp *)malloc(kfa->seqnum*sizeof(seqtyp)))) { kzclose(); return(0); }
	kzread(kfa->seq,kfa->seqnum*sizeof(seqtyp));

	kzclose();

		//MUST load the associated KV6 AFTER the kzclose :/
	kfa->numspr = 0;
	kv = getkv6(snotbuf); if (!kv) return(0);
	kfa->basekv6 = kv;
	if (!kv->numvoxs) return(0);
	v = kv->vox;
	for(x=kv->numvoxs-1;x>=0;x--) v[x].vis |= ((v[x].vis&32)<<1);
	for(x=0;x<kv->xsiz;x++)
		for(y=0;y<kv->ysiz;y++)
			for(ve=&v[kv->ylen[x*kv->ysiz+y]];v<ve;v++)
			{
				if (v->vis&16) ov = v;
				if ((v->vis&(64+32)) == 64+32)
				{
					floodsucksprite(&kfa->spr[kfa->numspr],kv,x,y,ov,v);
					kfa->numspr++;
				}
			}

	kfa->hingesort = (int *)malloc(kfa->numhin*4);
	kfasorthinge(kfa->hinge,kfa->numhin,kfa->hingesort);

		//Remember position offsets of limbs with no parent in hinge[?].p[0]
	for(i=(kfa->numhin)-1;i>=0;i--)
	{
		j = kfa->hingesort[i]; if (j >= kfa->numspr) continue;
		if (kfa->hinge[j].parent < 0)
		{
			kfa->hinge[j].p[0].x = -kfa->spr[j].p.x;
			kfa->hinge[j].p[0].y = -kfa->spr[j].p.y;
			kfa->hinge[j].p[0].z = -kfa->spr[j].p.z;
		}
	}

	return(kfa);
}*/

/**
 * Cover-up function to handle both .KV6 and .KFA files. It looks at the
 * filename extension and uses the appropriate function (either getkv6
 * or getkfa) and sets the sprite flags depending on the type of file.
 * The file must have either .KV6 or .KFA as the filename extension. If
 * you want to use weird filenames, then use getkv6/getkfa instead.
 *
 * @param spr Pointer to sprite structure that you provide. getspr() writes:
 *            only to the kv6data/voxtype, kfatim, and flags members.
 * @param filnam filename of either a .KV6 or .KFA file.
 */
/*void getspr (vx5sprite *s, const char *filnam)
{
	int i;

	if (!filnam) return;
	i = strlen(filnam); if (!i) return;

	if ((filnam[i-1] == 'A') || (filnam[i-1] == 'a'))
		{ s->kfaptr = getkfa(filnam); s->flags = 2; s->kfatim = 0; }
	else if (filnam[i-1] == '6')
		{ s->voxnum = getkv6(filnam); s->flags = 0; }
}*/

	//Given vector a, returns b&c that makes (a,b,c) orthonormal
void genperp (point3d *a, point3d *b, point3d *c)
{
	float t;

	if ((a->x == 0) && (a->y == 0) && (a->z == 0))
		{ b->x = 0; b->y = 0; b->z = 0; return; }
	if ((fabs(a->x) < fabs(a->y)) && (fabs(a->x) < fabs(a->z)))
		{ t = 1.0 / sqrt(a->y*a->y + a->z*a->z); b->x = 0; b->y = a->z*t; b->z = -a->y*t; }
	else if (fabs(a->y) < fabs(a->z))
		{ t = 1.0 / sqrt(a->x*a->x + a->z*a->z); b->x = -a->z*t; b->y = 0; b->z = a->x*t; }
	else
		{ t = 1.0 / sqrt(a->x*a->x + a->y*a->y); b->x = a->y*t; b->y = -a->x*t; b->z = 0; }
	c->x = a->y*b->z - a->z*b->y;
	c->y = a->z*b->x - a->x*b->z;
	c->z = a->x*b->y - a->y*b->x;
}

	//A * B = C, find A   36*, 27ñ
	//[asx ahx agx aox][bsx bhx bgx box]   [csx chx cgx cox]
	//[asy ahy agy aoy][bsy bhy bgy boy] = [csy chy cgy coy]
	//[asz ahz agz aoz][bsz bhz bgz boz]   [csz chz cgz coz]
	//[  0   0   0   1][  0   0   0   1]   [  0   0   0   1]
void mat0 (point3d *as, point3d *ah, point3d *ag, point3d *ao,
		   point3d *bs, point3d *bh, point3d *bg, point3d *bo,
		   point3d *cs, point3d *ch, point3d *cg, point3d *co)
{
	point3d ts, th, tg, to;
	ts.x = bs->x*cs->x + bh->x*ch->x + bg->x*cg->x;
	ts.y = bs->x*cs->y + bh->x*ch->y + bg->x*cg->y;
	ts.z = bs->x*cs->z + bh->x*ch->z + bg->x*cg->z;
	th.x = bs->y*cs->x + bh->y*ch->x + bg->y*cg->x;
	th.y = bs->y*cs->y + bh->y*ch->y + bg->y*cg->y;
	th.z = bs->y*cs->z + bh->y*ch->z + bg->y*cg->z;
	tg.x = bs->z*cs->x + bh->z*ch->x + bg->z*cg->x;
	tg.y = bs->z*cs->y + bh->z*ch->y + bg->z*cg->y;
	tg.z = bs->z*cs->z + bh->z*ch->z + bg->z*cg->z;
	to.x = co->x - bo->x*ts.x - bo->y*th.x - bo->z*tg.x;
	to.y = co->y - bo->x*ts.y - bo->y*th.y - bo->z*tg.y;
	to.z = co->z - bo->x*ts.z - bo->y*th.z - bo->z*tg.z;
	(*as) = ts; (*ah) = th; (*ag) = tg; (*ao) = to;
}

	//A * B = C, find B   36*, 27ñ
	//[asx ahx agx aox][bsx bhx bgx box]   [csx chx cgx cox]
	//[asy ahy agy aoy][bsy bhy bgy boy] = [csy chy cgy coy]
	//[asz ahz agz aoz][bsz bhz bgz boz]   [csz chz cgz coz]
	//[  0   0   0   1][  0   0   0   1]   [  0   0   0   1]
void mat1 (point3d *as, point3d *ah, point3d *ag, point3d *ao,
		   point3d *bs, point3d *bh, point3d *bg, point3d *bo,
		   point3d *cs, point3d *ch, point3d *cg, point3d *co)
{
	point3d ts, th, tg, to;
	float x = co->x-ao->x, y = co->y-ao->y, z = co->z-ao->z;
	ts.x = cs->x*as->x + cs->y*as->y + cs->z*as->z;
	ts.y = cs->x*ah->x + cs->y*ah->y + cs->z*ah->z;
	ts.z = cs->x*ag->x + cs->y*ag->y + cs->z*ag->z;
	th.x = ch->x*as->x + ch->y*as->y + ch->z*as->z;
	th.y = ch->x*ah->x + ch->y*ah->y + ch->z*ah->z;
	th.z = ch->x*ag->x + ch->y*ag->y + ch->z*ag->z;
	tg.x = cg->x*as->x + cg->y*as->y + cg->z*as->z;
	tg.y = cg->x*ah->x + cg->y*ah->y + cg->z*ah->z;
	tg.z = cg->x*ag->x + cg->y*ag->y + cg->z*ag->z;
	to.x = as->x*x + as->y*y + as->z*z;
	to.y = ah->x*x + ah->y*y + ah->z*z;
	to.z = ag->x*x + ag->y*y + ag->z*z;
	(*bs) = ts; (*bh) = th; (*bg) = tg; (*bo) = to;
}

	//A * B = C, find C   36*, 27ñ
	//[asx ahx afx aox][bsx bhx bfx box]   [csx chx cfx cox]
	//[asy ahy afy aoy][bsy bhy bfy boy] = [csy chy cfy coy]
	//[asz ahz afz aoz][bsz bhz bfz boz]   [csz chz cfz coz]
	//[  0   0   0   1][  0   0   0   1]   [  0   0   0   1]
void mat2 (point3d *a_s, point3d *a_h, point3d *a_f, point3d *a_o,
		   point3d *b_s, point3d *b_h, point3d *b_f, point3d *b_o,
		   point3d *c_s, point3d *c_h, point3d *c_f, point3d *c_o)
{
	point3d ts, th, tf, to;
	ts.x = a_s->x*b_s->x + a_h->x*b_s->y + a_f->x*b_s->z;
	ts.y = a_s->y*b_s->x + a_h->y*b_s->y + a_f->y*b_s->z;
	ts.z = a_s->z*b_s->x + a_h->z*b_s->y + a_f->z*b_s->z;
	th.x = a_s->x*b_h->x + a_h->x*b_h->y + a_f->x*b_h->z;
	th.y = a_s->y*b_h->x + a_h->y*b_h->y + a_f->y*b_h->z;
	th.z = a_s->z*b_h->x + a_h->z*b_h->y + a_f->z*b_h->z;
	tf.x = a_s->x*b_f->x + a_h->x*b_f->y + a_f->x*b_f->z;
	tf.y = a_s->y*b_f->x + a_h->y*b_f->y + a_f->y*b_f->z;
	tf.z = a_s->z*b_f->x + a_h->z*b_f->y + a_f->z*b_f->z;
	to.x = a_s->x*b_o->x + a_h->x*b_o->y + a_f->x*b_o->z + a_o->x;
	to.y = a_s->y*b_o->x + a_h->y*b_o->y + a_f->y*b_o->z + a_o->y;
	to.z = a_s->z*b_o->x + a_h->z*b_o->y + a_f->z*b_o->z + a_o->z;
	(*c_s) = ts; (*c_h) = th; (*c_f) = tf; (*c_o) = to;
}

static void setlimb (kfatype *kfa, int i, int p, int trans_type, short val)
{
	point3d ps, ph, pf, pp;
	point3d qs, qh, qf, qp;
	float r[2];

		//Generate orthonormal matrix in world space for child limb
	qp = kfa->hinge[i].p[0]; qs = kfa->hinge[i].v[0]; genperp(&qs,&qh,&qf);

	switch (trans_type)
	{
		case 0: //Hinge rotate!
			//fcossin(((float)val)*(PI/32768.0),&c,&s);
			ucossin(((int)val)<<16,r);
			ph = qh; pf = qf;
			qh.x = ph.x*r[0] - pf.x*r[1]; qf.x = ph.x*r[1] + pf.x*r[0];
			qh.y = ph.y*r[0] - pf.y*r[1]; qf.y = ph.y*r[1] + pf.y*r[0];
			qh.z = ph.z*r[0] - pf.z*r[1]; qf.z = ph.z*r[1] + pf.z*r[0];
			break;
		default: _gtfo(); //tells MSVC default can't be reached
	}

		//Generate orthonormal matrix in world space for parent limb
	pp = kfa->hinge[i].p[1]; ps = kfa->hinge[i].v[1]; genperp(&ps,&ph,&pf);

		//mat0(rotrans, loc_velcro, par_velcro)
	mat0(&qs,&qh,&qf,&qp, &qs,&qh,&qf,&qp, &ps,&ph,&pf,&pp);
		//mat2(par, rotrans, parent * par_velcro * (loc_velcro x rotrans)^-1)
	mat2(&kfa->spr[p].s,&kfa->spr[p].h,&kfa->spr[p].f,&kfa->spr[p].p,
		  &qs,&qh,&qf,&qp,
		  &kfa->spr[i].s,&kfa->spr[i].h,&kfa->spr[i].f,&kfa->spr[i].p);
}

	//Uses binary search to find sequence index at time "tim"
static int kfatime2seq (kfatype *kfa, int tim)
{
	int i, a, b;

	for(a=0,b=(kfa->seqnum)-1;b-a>=2;)
		{ i = ((a+b)>>1); if (tim >= kfa->seq[i].tim) a = i; else b = i; }
	return(a);
}

/**
 * You could animate .KFA sprites by simply modifying the .kfatim member of
 * vx5sprite structure. A better way is to use this function because it
 * will handle repeat/stop markers for you.
 *
 * @param spr .KFA sprite to animate
 * @param timeadd number of milliseconds to add to the current animation time
 */
void animsprite (vx5sprite *s, int ti)
{
	kfatype *kfa;
	int i, j, k, x, y, z, zz, trat;
	int trat2, z0, zz0, frm0;

	if (!(s->flags&2)) return;
	kfa = s->kfaptr; if (!kfa) return;

	z = kfatime2seq(kfa,s->kfatim);
	while (ti > 0)
	{
		z++; if (z >= kfa->seqnum) break;
		i = kfa->seq[z].tim-s->kfatim; if (i <= 0) break;
		if (i > ti) { s->kfatim += ti; break; }
		ti -= i;
		zz = ~kfa->seq[z].frm; if (zz >= 0) { if (z == zz) break; z = zz; }
		s->kfatim = kfa->seq[z].tim;
	}

// --------------------------------------------------------------------------

	z = kfatime2seq(kfa,s->kfatim); zz = z+1;
	if ((zz < kfa->seqnum) && (kfa->seq[zz].frm != ~zz))
	{
		trat = kfa->seq[zz].tim-kfa->seq[z].tim;
		if (trat) trat = shldiv16(s->kfatim-kfa->seq[z].tim,trat);
		i = kfa->seq[zz].frm; if (i < 0) zz = kfa->seq[~i].frm; else zz = i;
	} else trat = 0;
	z = kfa->seq[z].frm;
	if (z < 0)
	{
		z0 = kfatime2seq(kfa,s->okfatim); zz0 = z0+1;
		if ((zz0 < kfa->seqnum) && (kfa->seq[zz0].frm != ~zz0))
		{
			trat2 = kfa->seq[zz0].tim-kfa->seq[z0].tim;
			if (trat2) trat2 = shldiv16(s->okfatim-kfa->seq[z0].tim,trat2);
			i = kfa->seq[zz0].frm; if (i < 0) zz0 = kfa->seq[~i].frm; else zz0 = i;
		} else trat2 = 0;
		z0 = kfa->seq[z0].frm; if (z0 < 0) { z0 = zz0; trat2 = 0; }
	} else trat2 = -1;

	for(i=(kfa->numhin)-1;i>=0;i--)
	{
		if (kfa->hinge[i].parent < 0) continue;

		if (trat2 < 0) frm0 = (int)kfa->frmval[z*(kfa->numhin)+i];
		else
		{
			frm0 = (int)kfa->frmval[z0*(kfa->numhin)+i];
			if (trat2 > 0)
			{
				x = (((int)(kfa->frmval[zz0*(kfa->numhin)+i]-frm0))&65535);
				if (kfa->hinge[i].vmin == kfa->hinge[i].vmax) x = ((x<<16)>>16);
				else if ((((int)(kfa->frmval[zz0*(kfa->numhin)+i]-kfa->hinge[i].vmin))&65535) <
							(((int)(frm0-kfa->hinge[i].vmin))&65535))
					x -= 65536;
				frm0 += mulshr16(x,trat2);
			}
		}
		if (trat > 0)
		{
			x = (((int)(kfa->frmval[zz*(kfa->numhin)+i]-frm0))&65535);
			if (kfa->hinge[i].vmin == kfa->hinge[i].vmax) x = ((x<<16)>>16);
			else if ((((int)(kfa->frmval[zz*(kfa->numhin)+i]-kfa->hinge[i].vmin))&65535) <
						(((int)(frm0-kfa->hinge[i].vmin))&65535))
				x -= 65536;
			frm0 += mulshr16(x,trat);
		}
		vx5.kfaval[i] = frm0;
	}
}

static void kfadraw (vx5sprite *s)
{
	point3d tp;
	kfatype *kfa;
	int i, j, k;

	kfa = s->kfaptr; if (!kfa) return;

	for(i=(kfa->numhin)-1;i>=0;i--)
	{
		j = kfa->hingesort[i]; k = kfa->hinge[j].parent;
		if (k >= 0) setlimb(kfa,j,k,kfa->hinge[j].htype,vx5.kfaval[j]);
		else
		{
			kfa->spr[j].s = s->s;
			kfa->spr[j].h = s->h;
			kfa->spr[j].f = s->f;
			//kfa->spr[j].p = s->p;
			tp.x = kfa->hinge[j].p[0].x;
			tp.y = kfa->hinge[j].p[0].y;
			tp.z = kfa->hinge[j].p[0].z;
			kfa->spr[j].p.x = s->p.x - tp.x*s->s.x - tp.y*s->h.x - tp.z*s->f.x;
			kfa->spr[j].p.y = s->p.y - tp.x*s->s.y - tp.y*s->h.y - tp.z*s->f.y;
			kfa->spr[j].p.z = s->p.z - tp.x*s->s.z - tp.y*s->h.z - tp.z*s->f.z;
		}
		/*if (j < kfa->numspr) kv6draw(&kfa->spr[j]);*/
	}
}

//--------------------------- KFA sprite code ends ---------------------------

/**
 * Draw a .KV6/.KFA voxel sprite to the screen. Position & orientation are
 * specified in the vx5sprite structure. See VOXLAP5.H for details on the
 * structure.
 *
 * @param spr pointer to vx5sprite
 */
/*VOXLAP_DLL_FUNC void drawsprite (vx5sprite *spr)
{
	if (spr->flags&4) return;
	if (!(spr->flags&2)) kv6draw(spr); else kfadraw(spr);
}*/

#if 0

void setkv6 (vx5sprite *spr)
{
	point3d r0, r1;
	int x, y, vx, vy, vz;
	kv6data *kv;
	kv6voxtype *v, *ve;

	if (spr->flags&2) return;
	kv = spr->voxnum; if (!kv) return;

	vx5.minx = ?; vx5.maxx = ?+1;
	vx5.miny = ?; vx5.maxy = ?+1;
	vx5.minz = ?; vx5.maxz = ?+1;

	v = kv->vox; //.01 is to fool rounding so they aren't all even numbers
	r0.x = spr->p.x - kv->xpiv*spr->s.x - kv->ypiv*spr->h.x - kv->zpiv*spr->f.x - .01;
	r0.y = spr->p.y - kv->xpiv*spr->s.y - kv->ypiv*spr->h.y - kv->zpiv*spr->f.y - .01;
	r0.z = spr->p.z - kv->xpiv*spr->s.z - kv->ypiv*spr->h.z - kv->zpiv*spr->f.z - .01;
	vx5.colfunc = curcolfunc;
	for(x=0;x<kv->xsiz;x++)
	{
		r1 = r0;
		for(y=0;y<kv->ysiz;y++)
		{
			for(ve=&v[kv->ylen[x*kv->ysiz+y]];v<ve;v++)
			{
				ftol(spr->f.x*v->z + r1.x,&vx);
				ftol(spr->f.y*v->z + r1.y,&vy);
				ftol(spr->f.z*v->z + r1.z,&vz);
				vx5.curcol = ((v->col&0xffffff)|0x80000000);
				setcube(vx,vy,vz,-2);
			}
			r1.x += spr->h.x; r1.y += spr->h.y; r1.z += spr->h.z;
		}
		r0.x += spr->s.x; r0.y += spr->s.y; r0.z += spr->s.z;
	}
}

#else


static kv6data *gfrezkv;
static lpoint3d gfrezx, gfrezy, gfrezz, gfrezp;
static signed char gkv6colx[27] = {0,  0, 0, 0, 0, 1,-1, -1,-1,-1,-1, 0, 0, 0, 0, 1, 1, 1, 1,  1, 1, 1, 1,-1,-1,-1,-1};
static signed char gkv6coly[27] = {0,  0, 0, 1,-1, 0, 0,  0, 0,-1, 1, 1, 1,-1,-1, 0, 0,-1, 1,  1, 1,-1,-1, 1, 1,-1,-1};
static signed char gkv6colz[27] = {0,  1,-1, 0, 0, 0, 0, -1, 1, 0, 0, 1,-1, 1,-1, 1,-1, 0, 0,  1,-1, 1,-1, 1,-1, 1,-1};
int kv6colfunc (lpoint3d *p)
{
	kv6voxtype *v0, *v1, *v, *ve;
	int i, j, k, x, y, z, ox, oy, nx, ny, nz, mind, d;

	x = ((p->x*gfrezx.x + p->y*gfrezy.x + p->z*gfrezz.x + gfrezp.x)>>16);
	y = ((p->x*gfrezx.y + p->y*gfrezy.y + p->z*gfrezz.y + gfrezp.y)>>16);
	z = ((p->x*gfrezx.z + p->y*gfrezy.z + p->z*gfrezz.z + gfrezp.z)>>16);
	x = lbound0(x,gfrezkv->xsiz-1);
	y = lbound0(y,gfrezkv->ysiz-1);
	z = lbound0(z,gfrezkv->zsiz-1);

		//Process x
	v0 = gfrezkv->vox;
	if ((x<<1) < gfrezkv->xsiz) { ox = oy = j = 0; }
	else { v0 += gfrezkv->numvoxs; ox = gfrezkv->xsiz; oy = gfrezkv->ysiz; j = ox*oy; }
	v1 = v0;

	for(k=0;k<27;k++)
	{
		nx = ((int)gkv6colx[k])+x; if ((unsigned int)nx >= gfrezkv->xsiz) continue;
		ny = ((int)gkv6coly[k])+y; if ((unsigned int)ny >= gfrezkv->ysiz) continue;
		nz = ((int)gkv6colz[k])+z; if ((unsigned int)nz >= gfrezkv->zsiz) continue;

		if (nx != ox)
		{
			while (nx > ox) { v0 += gfrezkv->xlen[ox]; ox++; j += gfrezkv->ysiz; }
			while (nx < ox) { ox--; v0 -= gfrezkv->xlen[ox]; j -= gfrezkv->ysiz; }
			if ((ny<<1) < gfrezkv->ysiz) { oy = 0; v1 = v0; }
			else { oy = gfrezkv->ysiz; v1 = v0+gfrezkv->xlen[nx]; }
		}
		if (ny != oy)
		{
			while (ny > oy) { v1 += gfrezkv->ylen[j+oy]; oy++; }
			while (ny < oy) { oy--; v1 -= gfrezkv->ylen[j+oy]; }
		}

			//Process z
		for(v=v1,ve=&v1[gfrezkv->ylen[j+ny]];v<ve;v++)
			if (v->z == nz) return(v->col);
	}

		//Use brute force when all else fails.. :/
	v = gfrezkv->vox; mind = 0x7fffffff;
	for(nx=0;nx<gfrezkv->xsiz;nx++)
		for(ny=0;ny<gfrezkv->ysiz;ny++)
			for(ve=&v[gfrezkv->ylen[nx*gfrezkv->ysiz+ny]];v<ve;v++)
			{
				d = labs(x-nx)+labs(y-ny)+labs(z-v->z);
				if (d < mind) { mind = d; k = v->col; }
			}
	return(k);
}

static void kv6colfuncinit (vx5sprite *spr, float det)
{
	point3d tp, tp2;
	float f;

	gfrezkv = spr->voxnum; if (!gfrezkv) { vx5.colfunc = curcolfunc; return; }

	tp2.x = gfrezkv->xpiv + .5;
	tp2.y = gfrezkv->ypiv + .5;
	tp2.z = gfrezkv->zpiv + .5;
	tp.x = spr->p.x - spr->s.x*tp2.x - spr->h.x*tp2.y - spr->f.x*tp2.z;
	tp.y = spr->p.y - spr->s.y*tp2.x - spr->h.y*tp2.y - spr->f.y*tp2.z;
	tp.z = spr->p.z - spr->s.z*tp2.x - spr->h.z*tp2.y - spr->f.z*tp2.z;

		//spr->s.x*x + spr->h.x*y + spr->f.x*z = np.x; //Solve for x,y,z
		//spr->s.y*x + spr->h.y*y + spr->f.y*z = np.y;
		//spr->s.z*x + spr->h.z*y + spr->f.z*z = np.z;
	f = 65536.0 / det;

	tp2.x = (spr->h.y*spr->f.z - spr->h.z*spr->f.y)*f; ftol(tp2.x,&gfrezx.x);
	tp2.y = (spr->h.z*spr->f.x - spr->h.x*spr->f.z)*f; ftol(tp2.y,&gfrezy.x);
	tp2.z = (spr->h.x*spr->f.y - spr->h.y*spr->f.x)*f; ftol(tp2.z,&gfrezz.x);
	ftol(-tp.x*tp2.x - tp.y*tp2.y - tp.z*tp2.z,&gfrezp.x); gfrezp.x += 32767;

	tp2.x = (spr->f.y*spr->s.z - spr->f.z*spr->s.y)*f; ftol(tp2.x,&gfrezx.y);
	tp2.y = (spr->f.z*spr->s.x - spr->f.x*spr->s.z)*f; ftol(tp2.y,&gfrezy.y);
	tp2.z = (spr->f.x*spr->s.y - spr->f.y*spr->s.x)*f; ftol(tp2.z,&gfrezz.y);
	ftol(-tp.x*tp2.x - tp.y*tp2.y - tp.z*tp2.z,&gfrezp.y); gfrezp.y += 32767;

	tp2.x = (spr->s.y*spr->h.z - spr->s.z*spr->h.y)*f; ftol(tp2.x,&gfrezx.z);
	tp2.y = (spr->s.z*spr->h.x - spr->s.x*spr->h.z)*f; ftol(tp2.y,&gfrezy.z);
	tp2.z = (spr->s.x*spr->h.y - spr->s.y*spr->h.x)*f; ftol(tp2.z,&gfrezz.z);
	ftol(-tp.x*tp2.x - tp.y*tp2.y - tp.z*tp2.z,&gfrezp.z); gfrezp.z += 32768;
}

#define LSC3 8 //2 for testing, 8 is normal
typedef struct
{
	int xo, yo, zo, xu, yu, zu, xv, yv, zv, d, msk, pzi;
	int xmino, ymino, xmaxo, ymaxo, xusc, yusc, xvsc, yvsc;
} gfrezt;
static gfrezt gfrez[6];
typedef struct { char z[2]; int n; } slstype;
void setkv6 (vx5sprite *spr, int dacol)
{
	point3d tp, tp2; float f, det;
	int i, j, k, x, y, z, c, d, x0, y0, z0, x1, y1, z1, xi, yi, zi;
	int xo, yo, zo, xu, yu, zu, xv, yv, zv, stu, stv, tu, tv;
	int xx, yy, xmin, xmax, ymin, ymax, isrhs, ihxi, ihyi, ihzi, syshpit;
	int isx, isy, isz, ihx, ihy, ihz, ifx, ify, ifz, iox, ioy, ioz;
	int sx, sy, sx0, sy0, sz0, rx, ry, rz, pz, dcnt, dcnt2, vismask, xysiz;
	int bx0, by0, bz0, bx1, by1, bz1, *lptr, *shead, shpit, scnt, sstop;
	gfrezt *gf;
	slstype *slst;
	kv6data *kv;
	kv6voxtype *v0, *v1, *v2, *v3;
	void (*modslab)(int *, int, int);

	if (spr->flags&2) return;
	kv = spr->voxnum; if (!kv) return;

		//Calculate top-left-up corner in VXL world coordinates
	tp.x = kv->xpiv + .5;
	tp.y = kv->ypiv + .5;
	tp.z = kv->zpiv + .5;
	tp2.x = spr->p.x - spr->s.x*tp.x - spr->h.x*tp.y - spr->f.x*tp.z;
	tp2.y = spr->p.y - spr->s.y*tp.x - spr->h.y*tp.y - spr->f.y*tp.z;
	tp2.z = spr->p.z - spr->s.z*tp.x - spr->h.z*tp.y - spr->f.z*tp.z;

		//Get bounding x-y box of entire freeze area:
	bx0 = VSID; by0 = VSID; bz0 = MAXZDIM; bx1 = 0; by1 = 0; bz1 = 0;
	for(z=kv->zsiz;z>=0;z-=kv->zsiz)
		for(y=kv->ysiz;y>=0;y-=kv->ysiz)
			for(x=kv->xsiz;x>=0;x-=kv->xsiz)
			{
				ftol(spr->s.x*(float)x + spr->h.x*(float)y + spr->f.x*(float)z + tp2.x,&i);
				if (i < bx0) bx0 = i;
				if (i > bx1) bx1 = i;
				ftol(spr->s.y*(float)x + spr->h.y*(float)y + spr->f.y*(float)z + tp2.y,&i);
				if (i < by0) by0 = i;
				if (i > by1) by1 = i;
				ftol(spr->s.z*(float)x + spr->h.z*(float)y + spr->f.z*(float)z + tp2.z,&i);
				if (i < bz0) bz0 = i;
				if (i > bz1) bz1 = i;
			}
	bx0 -= 2; if (bx0 < 0) bx0 = 0;
	by0 -= 2; if (by0 < 0) by0 = 0;
	bz0 -= 2; if (bz0 < 0) bz0 = 0;
	bx1 += 2; if (bx1 > VSID) bx1 = VSID;
	by1 += 2; if (by1 > VSID) by1 = VSID;
	bz1 += 2; if (bz1 > MAXZDIM) bz1 = MAXZDIM;
	vx5.minx = bx0; vx5.maxx = bx1;
	vx5.miny = by0; vx5.maxy = by1;
	vx5.minz = bz0; vx5.maxz = bz1;

	shpit = bx1-bx0; i = (by1-by0)*shpit*sizeof(shead[0]);
		//Make sure to use array that's big enough: umost is 1MB
	shead = (int *)(((size_t)umost) - (by0*shpit+bx0)*sizeof(shead[0]));
	slst = (slstype *)(((size_t)umost)+i);
	scnt = 1; sstop = (sizeof(umost)-i)/sizeof(slstype);
	memset(umost,0,i);

	f = (float)(1<<LSC3);
	ftol(spr->s.x*f,&isx); ftol(spr->s.y*f,&isy); ftol(spr->s.z*f,&isz);
	ftol(spr->h.x*f,&ihx); ftol(spr->h.y*f,&ihy); ftol(spr->h.z*f,&ihz);
	ftol(spr->f.x*f,&ifx); ftol(spr->f.y*f,&ify); ftol(spr->f.z*f,&ifz);
	ftol(tp2.x*f,&iox);
	ftol(tp2.y*f,&ioy);
	ftol(tp2.z*f,&ioz);

		//Determine whether sprite is RHS(1) or LHS(0)
	det = (spr->h.y*spr->f.z - spr->h.z*spr->f.y)*spr->s.x +
			(spr->h.z*spr->f.x - spr->h.x*spr->f.z)*spr->s.y +
			(spr->h.x*spr->f.y - spr->h.y*spr->f.x)*spr->s.z;
	if ((*(int *)&det) > 0) isrhs = 1;
	else if ((*(int *)&det) < 0) isrhs = 0;
	else return;

	xi = (((ifx*ihy-ihx*ify)>>31)|1);
	yi = (((isx*ify-ifx*isy)>>31)|1);
	zi = (((ihx*isy-isx*ihy)>>31)|1);
	if (xi > 0) { x0 = 0; x1 = kv->xsiz; } else { x0 = kv->xsiz-1; x1 = -1; }
	if (yi > 0) { y0 = 0; y1 = kv->ysiz; } else { y0 = kv->ysiz-1; y1 = -1; }
	if (zi > 0) { z0 = 0; z1 = kv->zsiz; } else { z0 = kv->zsiz-1; z1 = -1; }

	vismask = (zi<<3)+24 + (yi<<1)+6 + (xi>>1)+2;

	dcnt = 0;
	for(j=2;j;j--)
	{
		dcnt2 = dcnt;
		vismask = ~vismask;
		for(i=1;i<64;i+=i)
		{
			if (!(vismask&i)) continue;

			if (i&0x15) { xo = yo = zo = 0; }
			else if (i == 2) { xo = isx; yo = isy; zo = isz; }
			else if (i == 8) { xo = ihx; yo = ihy; zo = ihz; }
			else             { xo = ifx; yo = ify; zo = ifz; }

				  if (i&3)  { xu = ihx; yu = ihy; zu = ihz; xv = ifx; yv = ify; zv = ifz; }
			else if (i&12) { xu = isx; yu = isy; zu = isz; xv = ifx; yv = ify; zv = ifz; }
			else           { xu = isx; yu = isy; zu = isz; xv = ihx; yv = ihy; zv = ihz; }

			if ((yu < 0) || ((!yu) && (xu < 0)))
				{ xo += xu; yo += yu; zo += zu; xu = -xu; yu = -yu; zu = -zu; }
			if ((yv < 0) || ((!yv) && (xv < 0)))
				{ xo += xv; yo += yv; zo += zv; xv = -xv; yv = -yv; zv = -zv; }
			d = xv*yu - xu*yv; if (!d) continue;
			if (d < 0)
			{
				k = xu; xu = xv; xv = k;
				k = yu; yu = yv; yv = k;
				k = zu; zu = zv; zv = k; d = -d;
			}
			xmin = ymin = xmax = ymax = 0;
			if (xu < 0) xmin += xu; else xmax += xu;
			if (yu < 0) ymin += yu; else ymax += yu;
			if (xv < 0) xmin += xv; else xmax += xv;
			if (yv < 0) ymin += yv; else ymax += yv;

			gf = &gfrez[dcnt];
			gf->xo = xo; gf->yo = yo; gf->zo = zo;
			gf->xu = xu; gf->yu = yu;
			gf->xv = xv; gf->yv = yv;
			gf->xmino = xmin; gf->ymino = ymin;
			gf->xmaxo = xmax; gf->ymaxo = ymax;
			gf->xusc = (xu<<LSC3); gf->yusc = (yu<<LSC3);
			gf->xvsc = (xv<<LSC3); gf->yvsc = (yv<<LSC3);
			gf->d = d; gf->msk = i;

			f = 1.0 / (float)d;
			ftol(((float)gf->yusc * (float)zv - (float)gf->yvsc * (float)zu) * f,&gf->pzi);
			f *= 4194304.0;
			ftol((float)zu*f,&gf->zu);
			ftol((float)zv*f,&gf->zv);

			dcnt++;
		}
	}

	ihxi = ihx*yi;
	ihyi = ihy*yi;
	ihzi = ihz*yi;

	if (xi < 0) v0 = kv->vox+kv->numvoxs; else v0 = kv->vox;
	for(x=x0;x!=x1;x+=xi)
	{
		i = (int)kv->xlen[x];
		if (xi < 0) v0 -= i;
		if (yi < 0) v1 = v0+i; else v1 = v0;
		if (xi >= 0) v0 += i;
		xysiz = x*kv->ysiz;
		sx0 = isx*x + ihx*y0 + iox;
		sy0 = isy*x + ihy*y0 + ioy;
		sz0 = isz*x + ihz*y0 + ioz;
		for(y=y0;y!=y1;y+=yi)
		{
			i = (int)kv->ylen[xysiz+y];
			if (yi < 0) v1 -= i;
			if (zi < 0) { v2 = v1+i-1; v3 = v1-1; }
					 else { v2 = v1; v3 = v1+i; }
			if (yi >= 0) v1 += i;
			while (v2 != v3)
			{
				z = v2->z; //c = v2->col;
				rx = ifx*z + sx0;
				ry = ify*z + sy0;
				rz = ifz*z + sz0;
				for(i=0;i<dcnt;i++)
				{
					gf = &gfrez[i]; if (!(v2->vis&gf->msk)) continue;
					xo = gf->xo + rx;
					yo = gf->yo + ry;
					zo = gf->zo + rz;
					xmin = ((gf->xmino + xo)>>LSC3); if (xmin < 0) xmin = 0;
					ymin = ((gf->ymino + yo)>>LSC3); if (ymin < 0) ymin = 0;
					xmax = ((gf->xmaxo + xo)>>LSC3)+1; if (xmax > VSID) xmax = VSID;
					ymax = ((gf->ymaxo + yo)>>LSC3)+1; if (ymax > VSID) ymax = VSID;
					xx = (xmin<<LSC3) - xo;
					yy = (ymin<<LSC3) - yo;
					stu = yy*gf->xu - xx*gf->yu;
					stv = yy*gf->xv - xx*gf->yv - gf->d;
					syshpit = ymin*shpit;
					for(sy=ymin;sy<ymax;sy++,syshpit+=shpit)
					{
						tu = stu; stu += gf->xusc;
						tv = stv; stv += gf->xvsc;
						sx = xmin;
						while ((tu&tv) >= 0)
						{
							sx++; if (sx >= xmax) goto freezesprcont;
							tu -= gf->yusc; tv -= gf->yvsc;
						}
						tu = ~tu; tv += gf->d;
						pz = dmulshr22(tu,gf->zv,tv,gf->zu) + zo; j = syshpit+sx;
						tu -= gf->d; tv = ~tv;
						while ((tu&tv) < 0)
						{
							if (i < dcnt2)
							{
								if (scnt >= sstop) return; //OUT OF BUFFER SPACE!
								slst[scnt].z[0] = (char)lbound0(pz>>LSC3,255);
								slst[scnt].n = shead[j]; shead[j] = scnt; scnt++;
							}
							else slst[shead[j]].z[1] = (char)lbound0(pz>>LSC3,255);
							tu += gf->yusc; tv += gf->yvsc; pz += gf->pzi; j++;
						}
freezesprcont:;
					}
				}
				v2 += zi;
			}
			sx0 += ihxi; sy0 += ihyi; sz0 += ihzi;
		}
	}

	if (dacol == -1) modslab = delslab; else modslab = insslab;

	if (vx5.colfunc == kv6colfunc) kv6colfuncinit(spr,det);

	j = by0*shpit+bx0;
	for(sy=by0;sy<by1;sy++)
		for(sx=bx0;sx<bx1;sx++,j++)
		{
			i = shead[j]; if (!i) continue;
			lptr = scum2(sx,sy);
			do
			{
				modslab(lptr,(int)slst[i].z[isrhs],(int)slst[i].z[isrhs^1]);
				i = slst[i].n;
			} while (i);
		}
	scum2finish();
	updatebbox(vx5.minx,vx5.miny,vx5.minz,vx5.maxx,vx5.maxy,vx5.maxz,dacol);
}

#endif

	//Sprite structure is already allocated
	//kv6, vox, xlen, ylen are all malloced in here!
/**
 * This converts a spherical cut-out of the VXL map into a .KV6 sprite in
 * memory. This function can be used to make walls fall over (with full
 * rotation). It allocates a new vx5sprite sprite structure and you are
 * responsible for freeing the memory using "free" in your own code.
 *
 * @param spr new vx5sprite structure. Position & orientation are initialized
 *        so when you call drawsprite, it exactly matches the VXL map.
 * @param hit center of sphere
 * @param hitrad radius of sphere
 * @param returns 0:bad, >0:mass of captured object (# of voxels)
 */
int meltsphere (vx5sprite *spr, lpoint3d *hit, int hitrad)
{
	int x, y, z, xs, ys, zs, xe, ye, ze, sq, z0, z1;
	size_t i, j;
	int oxvoxs, oyvoxs, numvoxs, cx, cy, cz, cw;
	float f, ff;
	kv6data *kv;
	kv6voxtype *voxptr;
	unsigned int *xlenptr;
	unsigned short *ylenptr;

	xs = MAX(hit->x-hitrad,0); xe = MIN(hit->x+hitrad,VSID-1);
	ys = MAX(hit->y-hitrad,0); ye = MIN(hit->y+hitrad,VSID-1);
	zs = MAX(hit->z-hitrad,0); ze = MIN(hit->z+hitrad,MAXZDIM-1);
	if ((xs > xe) || (ys > ye) || (zs > ze)) return(0);

	if (hitrad >= SETSPHMAXRAD-1) hitrad = SETSPHMAXRAD-2;

	tempfloatbuf[0] = 0.0f;
#if 0
		//Totally unoptimized
	for(i=1;i<=hitrad;i++) tempfloatbuf[i] = pow((float)i,vx5.curpow);
#else
	tempfloatbuf[1] = 1.0f;
	for(i=2;i<=hitrad;i++)
	{
		if (!factr[i][0]) tempfloatbuf[i] = exp(logint[i]*vx5.curpow);
		else tempfloatbuf[i] = tempfloatbuf[factr[i][0]]*tempfloatbuf[factr[i][1]];
	}
#endif
	*(int *)&tempfloatbuf[hitrad+1] = 0x7f7fffff; //3.4028235e38f; //Highest float

// ---------- Need to know how many voxels to allocate... SLOW!!! :( ----------
	cx = cy = cz = 0; //Centroid
	cw = 0;       //Weight (1 unit / voxel)
	numvoxs = 0;
	sq = 0; //pow(fabs(x-hit->x),vx5.curpow) + "y + "z < pow(vx5.currad,vx5.curpow)
	for(x=xs;x<=xe;x++)
	{
		ff = tempfloatbuf[hitrad]-tempfloatbuf[labs(x-hit->x)];
		for(y=ys;y<=ye;y++)
		{
			f = ff-tempfloatbuf[labs(y-hit->y)];
			if (*(int *)&f > 0) //WARNING: make sure to always write ylenptr!
			{
				while (*(int *)&tempfloatbuf[sq] <  *(int *)&f) sq++;
				while (*(int *)&tempfloatbuf[sq] >= *(int *)&f) sq--;
				z0 = MAX(hit->z-sq,zs); z1 = MIN(hit->z+sq+1,ze);
				for(z=z0;z<z1;z++)
				{
					i = (size_t)getcube(x,y,z); //0:air, 1:unexposed solid, 2:vbuf col ptr
					if (i)
					{
						cx += (x-hit->x); cy += (y-hit->y); cz += (z-hit->z); cw++;
					}
					if ((i == 0) || ((i == 1) && (1))) continue; //not_on_border))) continue; //FIX THIS!!!
					numvoxs++;
				}
			}
		}
	}
	if (numvoxs <= 0) return(0); //No voxels found!
// ---------------------------------------------------------------------------

	f = 1.0 / (float)cw; //Make center of sprite the centroid
	spr->p.x = (float)hit->x + (float)cx*f;
	spr->p.y = (float)hit->y + (float)cy*f;
	spr->p.z = (float)hit->z + (float)cz*f;
	spr->s.x = 1.f; spr->h.x = 0.f; spr->f.x = 0.f;
	spr->s.y = 0.f; spr->h.y = 1.f; spr->f.y = 0.f;
	spr->s.z = 0.f; spr->h.z = 0.f; spr->f.z = 1.f;

	x = xe-xs+1; y = ye-ys+1; z = ze-zs+1;

	j = sizeof(kv6data) + numvoxs*sizeof(kv6voxtype) + x*4 + x*y*2;
	i = (size_t)malloc(j); if (!i) return(0); if (i&3) { free((void *)i); return(0); }
	spr->voxnum = kv = (kv6data *)i; spr->flags = 0;
	kv->leng = j;
	kv->xsiz = x;
	kv->ysiz = y;
	kv->zsiz = z;
	kv->xpiv = spr->p.x - xs;
	kv->ypiv = spr->p.y - ys;
	kv->zpiv = spr->p.z - zs;
	kv->numvoxs = numvoxs;
	kv->namoff = 0;
	kv->lowermip = 0;
	kv->vox = (kv6voxtype *)((size_t)spr->voxnum+sizeof(kv6data));
	kv->xlen = (unsigned int *)(((size_t)kv->vox)+numvoxs*sizeof(kv6voxtype));
	kv->ylen = (unsigned short *)(((size_t)kv->xlen) + kv->xsiz*4);

	voxptr = kv->vox; numvoxs = 0;
	xlenptr = kv->xlen; oxvoxs = 0;
	ylenptr = kv->ylen; oyvoxs = 0;

	sq = 0; //pow(fabs(x-hit->x),vx5.curpow) + "y + "z < pow(vx5.currad,vx5.curpow)
	for(x=xs;x<=xe;x++)
	{
		ff = tempfloatbuf[hitrad]-tempfloatbuf[labs(x-hit->x)];
		for(y=ys;y<=ye;y++)
		{
			f = ff-tempfloatbuf[labs(y-hit->y)];
			if (*(int *)&f > 0) //WARNING: make sure to always write ylenptr!
			{
				while (*(int *)&tempfloatbuf[sq] <  *(int *)&f) sq++;
				while (*(int *)&tempfloatbuf[sq] >= *(int *)&f) sq--;
				z0 = MAX(hit->z-sq,zs); z1 = MIN(hit->z+sq+1,ze);
				for(z=z0;z<z1;z++)
				{
					int *ptr=getcube(x,y,z); //0:air, 1:unexposed solid, 2:vbuf col ptr
					if ((ptr==NULL) || ((ptr == (int*)1) && (1))) continue; //not_on_border))) continue; //FIX THIS!!!
					voxptr[numvoxs].col = lightvox(*ptr);
					voxptr[numvoxs].z = z-zs;
					voxptr[numvoxs].vis = 63; //FIX THIS!!!
					voxptr[numvoxs].dir = 0; //FIX THIS!!!
					numvoxs++;
				}
			}
			*ylenptr++ = numvoxs-oyvoxs; oyvoxs = numvoxs;
		}
		*xlenptr++ = numvoxs-oxvoxs; oxvoxs = numvoxs;
	}
	return(cw);
}

	//Sprite structure is already allocated
	//kv6, vox, xlen, ylen are all malloced in here!
/**
 * This function is similar to meltsphere, except you can use any user-
 * defined shape (with some size limits). The user-defined shape is
 * described by a list of vertical columns in the "vspans" format:
 *
 * typedef struct { char z1, z0, x, y; } vspans;
 *
 * The list MUST be ordered first in increasing Y, then in increasing X
 * or else the function will crash! Fortunately, the structure is
 * arranged in a way that the data can be sorted quite easily using a
 * simple trick: if you use a typecast from vspans to "unsigned int",
 * you can use a generic sort code on 32-bit integers to achieve a
 * correct sort. The vspans members are all treated as unsigned chars,
 * so it's usually a good idea to bias your columns by 128, and then
 * reverse-bias them in the "offs" offset.
 *
 * @param spr new vx5sprite structure. Position & orientation are initialized
 *            so when you call drawsprite, it exactly matches the VXL map.
 * @param lst list in "vspans" format
 * @param lstnum number of columns on list
 * @param offs offset of top-left corner in VXL coordinates
 * @return mass (in voxel units), returns 0 if error (or no voxels)
 */
int meltspans (vx5sprite *spr, vspans *lst, int lstnum, lpoint3d *offs)
{
	float f;
	int j, x, y, z, xs, ys, zs, xe, ye, ze, z0, z1;
	size_t i;
	int ox, oy, oxvoxs, oyvoxs, numvoxs, cx, cy, cz, cw;
	kv6data *kv;
	kv6voxtype *voxptr;
	unsigned int *xlenptr;
	unsigned short *ylenptr;

	if (lstnum <= 0) return(0);
// ---------- Need to know how many voxels to allocate... SLOW!!! :( ----------
	cx = cy = cz = 0; //Centroid
	cw = 0;       //Weight (1 unit / voxel)
	numvoxs = 0;
	xs = xe = ((int)lst[0].x)+offs->x;
	ys = ((int)lst[       0].y)+offs->y;
	ye = ((int)lst[lstnum-1].y)+offs->y;
	zs = ze = ((int)lst[0].z0)+offs->z;
	for(j=0;j<lstnum;j++)
	{
		x = ((int)lst[j].x)+offs->x;
		y = ((int)lst[j].y)+offs->y; if ((x|y)&(~(VSID-1))) continue;
			  if (x < xs) xs = x;
		else if (x > xe) xe = x;
		z0 = ((int)lst[j].z0)+offs->z;   if (z0 < 0) z0 = 0;
		z1 = ((int)lst[j].z1)+offs->z+1; if (z1 > MAXZDIM) z1 = MAXZDIM;
		if (z0 < zs) zs = z0;
		if (z1 > ze) ze = z1;
		for(z=z0;z<z1;z++) //getcube too SLOW... FIX THIS!!!
		{
			i = (size_t)getcube(x,y,z); //0:air, 1:unexposed solid, 2:vbuf col ptr
			if (i) { cx += x-offs->x; cy += y-offs->y; cz += z-offs->z; cw++; }
			if (i&~1) numvoxs++;
		}
	}
	if (numvoxs <= 0) return(0); //No voxels found!
// ---------------------------------------------------------------------------

	f = 1.0 / (float)cw; //Make center of sprite the centroid
	spr->p.x = (float)offs->x + (float)cx*f;
	spr->p.y = (float)offs->y + (float)cy*f;
	spr->p.z = (float)offs->z + (float)cz*f;
	spr->x.x = 0.f; spr->y.x = 1.f; spr->z.x = 0.f;
	spr->x.y = 1.f; spr->y.y = 0.f; spr->z.y = 0.f;
	spr->x.z = 0.f; spr->y.z = 0.f; spr->z.z = 1.f;

	x = xe-xs+1; y = ye-ys+1; z = ze-zs;

	j = sizeof(kv6data) + numvoxs*sizeof(kv6voxtype) + y*4 + x*y*2;
	i = (size_t)malloc(j); if (!i) return(0); if (i&3) { free((void *)i); return(0); }
	spr->voxnum = kv = (kv6data *)i; spr->flags = 0;
	kv->leng = j;
	kv->xsiz = y;
	kv->ysiz = x;
	kv->zsiz = z;
	kv->xpiv = spr->p.y - ys;
	kv->ypiv = spr->p.x - xs;
	kv->zpiv = spr->p.z - zs;
	kv->numvoxs = numvoxs;
	kv->namoff = 0;
	kv->lowermip = 0;
	kv->vox = (kv6voxtype *)((size_t)spr->voxnum+sizeof(kv6data));
	kv->xlen = (unsigned int *)(((size_t)kv->vox)+numvoxs*sizeof(kv6voxtype));
	kv->ylen = (unsigned short *)(((size_t)kv->xlen) + kv->xsiz*4);

	voxptr = kv->vox; numvoxs = 0;
	xlenptr = kv->xlen; oxvoxs = 0;
	ylenptr = kv->ylen; oyvoxs = 0;
	ox = xs; oy = ys;
	for(j=0;j<lstnum;j++)
	{
		x = ((int)lst[j].x)+offs->x;
		y = ((int)lst[j].y)+offs->y; if ((x|y)&(~(VSID-1))) continue;
		while ((ox != x) || (oy != y))
		{
			*ylenptr++ = numvoxs-oyvoxs; oyvoxs = numvoxs; ox++;
			if (ox > xe)
			{
				*xlenptr++ = numvoxs-oxvoxs; oxvoxs = numvoxs;
				ox = xs; oy++;
			}
		}
		z0 = ((int)lst[j].z0)+offs->z;   if (z0 < 0) z0 = 0;
		z1 = ((int)lst[j].z1)+offs->z+1; if (z1 > MAXZDIM) z1 = MAXZDIM;
		for(z=z0;z<z1;z++) //getcube TOO SLOW... FIX THIS!!!
		{
			int *colptr=getcube(x,y,z); //0:air, 1:unexposed solid, 2:vbuf col ptr
			if (!(((size_t)colptr)&~1)) continue;
			voxptr[numvoxs].col = lightvox(*colptr);
			voxptr[numvoxs].z = z-zs;

			voxptr[numvoxs].vis = 63; //FIX THIS!!!
			//if (!isvoxelsolid(x-1,y,z)) voxptr[numvoxs].vis |= 1;
			//if (!isvoxelsolid(x+1,y,z)) voxptr[numvoxs].vis |= 2;
			//if (!isvoxelsolid(x,y-1,z)) voxptr[numvoxs].vis |= 4;
			//if (!isvoxelsolid(x,y+1,z)) voxptr[numvoxs].vis |= 8;
			//if (!isvoxelsolid(x,y,z-1)) voxptr[numvoxs].vis |= 16;
			//if (!isvoxelsolid(x,y,z+1)) voxptr[numvoxs].vis |= 32;

			voxptr[numvoxs].dir = 0; //FIX THIS!!!
			numvoxs++;
		}
	}
	while (1)
	{
		*ylenptr++ = numvoxs-oyvoxs; oyvoxs = numvoxs; ox++;
		if (ox > xe)
		{
			*xlenptr++ = numvoxs-oxvoxs; oxvoxs = numvoxs;
			ox = xs; oy++; if (oy > ye) break;
		}
	}
	return(cw);
}

static void setlighting (int x0, int y0, int z0, int x1, int y1, int z1, int lval)
{
	int i, x, y;
	char *v;

	x0 = MAX(x0,0); x1 = MIN(x1,VSID);
	y0 = MAX(y0,0); y1 = MIN(y1,VSID);
	z0 = MAX(z0,0); z1 = MIN(z1,MAXZDIM);

	lval <<= 24;

		//Set 4th byte of colors to full intensity
	for(y=y0;y<y1;y++)
		for(x=x0;x<x1;x++)
		{
			for(v=sptr[y*VSID+x];v[0];v+=v[0]*4)
				for(i=1;i<v[0];i++)
					(*(int *)&v[i<<2]) = (((*(int *)&v[i<<2])&0xffffff)|lval);
			for(i=1;i<=v[2]-v[1]+1;i++)
				(*(int *)&v[i<<2]) = (((*(int *)&v[i<<2])&0xffffff)|lval);
		}
}

	//Updates Lighting, Mip-mapping, and Floating objects list
typedef struct { int x0, y0, z0, x1, y1, z1, csgdel; } bboxtyp;
#define BBOXSIZ 256
static bboxtyp bbox[BBOXSIZ];
static int bboxnum = 0;
void updatevxl ()
{
	int i;
	for(i=bboxnum-1;i>=0;i--)
	{
		if (vx5.lightmode)
			updatelighting(bbox[i].x0,bbox[i].y0,bbox[i].z0,bbox[i].x1,bbox[i].y1,bbox[i].z1);
		if (vx5.vxlmipuse > 1)
			genmipvxl(bbox[i].x0,bbox[i].y0,bbox[i].x1,bbox[i].y1);
		if ((vx5.fallcheck) && (bbox[i].csgdel))
			checkfloatinbox(bbox[i].x0,bbox[i].y0,bbox[i].z0,bbox[i].x1,bbox[i].y1,bbox[i].z1);
	}
	bboxnum = 0;
}

void updatebbox (int x0, int y0, int z0, int x1, int y1, int z1, int csgdel)
{
	int i;

	if ((x0 >= x1) || (y0 >= y1) || (z0 >= z1)) return;
	for(i=bboxnum-1;i>=0;i--)
	{
		if ((x0 >= bbox[i].x1) || (bbox[i].x0 >= x1)) continue;
		if ((y0 >= bbox[i].y1) || (bbox[i].y0 >= y1)) continue;
		if ((z0 >= bbox[i].z1) || (bbox[i].z0 >= z1)) continue;
		if (bbox[i].x0 < x0) x0 = bbox[i].x0;
		if (bbox[i].y0 < y0) y0 = bbox[i].y0;
		if (bbox[i].z0 < z0) z0 = bbox[i].z0;
		if (bbox[i].x1 > x1) x1 = bbox[i].x1;
		if (bbox[i].y1 > y1) y1 = bbox[i].y1;
		if (bbox[i].z1 > z1) z1 = bbox[i].z1;
		csgdel |= bbox[i].csgdel;
		bboxnum--; bbox[i] = bbox[bboxnum];
	}
	bbox[bboxnum].x0 = x0; bbox[bboxnum].x1 = x1;
	bbox[bboxnum].y0 = y0; bbox[bboxnum].y1 = y1;
	bbox[bboxnum].z0 = z0; bbox[bboxnum].z1 = z1;
	bbox[bboxnum].csgdel = csgdel; bboxnum++;
	if (bboxnum >= BBOXSIZ) updatevxl();
}

static int lightlst[MAXLIGHTS];
static float lightsub[MAXLIGHTS];
	//Re-calculates lighting byte #4 of all voxels inside bounding box
void updatelighting (int x0, int y0, int z0, int x1, int y1, int z1)
{
	point3d tp;
	float f, g, h, fx, fy, fz;
	int i, j, x, y, z, sz0, sz1, offs, cstat, lightcnt;
	int x2, y2, x3, y3;
	char *v;

	if (!vx5.lightmode) return;
	xbsox = -17;

	x0 = MAX(x0-ESTNORMRAD,0); x1 = MIN(x1+ESTNORMRAD,VSID);
	y0 = MAX(y0-ESTNORMRAD,0); y1 = MIN(y1+ESTNORMRAD,VSID);
	z0 = MAX(z0-ESTNORMRAD,0); z1 = MIN(z1+ESTNORMRAD,MAXZDIM);

	x2 = x0; y2 = y0;
	x3 = x1; y3 = y1;
	for(y0=y2;y0<y3;y0=y1)
	{
		y1 = MIN(y0+64,y3);  //"justfly -" (256 lights): +1024:41sec 512:29 256:24 128:22 64:21 32:21 16:21
		for(x0=x2;x0<x3;x0=x1)
		{
			x1 = MIN(x0+64,x3);


			if (vx5.lightmode == 2)
			{
				lightcnt = 0; //Find which lights are close enough to affect rectangle
				for(i=vx5.numlights-1;i>=0;i--)
				{
					ftol(vx5.lightsrc[i].p.x,&x);
					ftol(vx5.lightsrc[i].p.y,&y);
					ftol(vx5.lightsrc[i].p.z,&z);
					if (x < x0) x -= x0; else if (x > x1) x -= x1; else x = 0;
					if (y < y0) y -= y0; else if (y > y1) y -= y1; else y = 0;
					if (z < z0) z -= z0; else if (z > z1) z -= z1; else z = 0;
					f = vx5.lightsrc[i].r2;
					if ((float)(x*x+y*y+z*z) < f)
					{
						lightlst[lightcnt] = i;
						lightsub[lightcnt] = 1/(sqrt(f)*f);
						lightcnt++;
					}
				}
			}

			for(y=y0;y<y1;y++)
				for(x=x0;x<x1;x++)
				{
					v = sptr[y*VSID+x]; cstat = 0;
					while (1)
					{
						if (!cstat)
						{
							sz0 = ((int)v[1]); sz1 = ((int)v[2])+1; offs = 7-(sz0<<2);
							cstat = 1;
						}
						else
						{
							sz0 = ((int)v[2])-((int)v[1])-((int)v[0])+2;
							if (!v[0]) break; v += v[0]*4;
							sz1 = ((int)v[3]); sz0 += sz1; offs = 3-(sz1<<2);
							cstat = 0;
						}
						if (z0 > sz0) sz0 = z0;
						if (z1 < sz1) sz1 = z1;
						if (vx5.lightmode < 2)
						{
							for(z=sz0;z<sz1;z++)
							{
								estnorm(x,y,z,&tp);
								ftol((tp.y*.5+tp.z)*64.f+103.5f,&i);
								v[(z<<2)+offs] = *(char *)&i;
							}
						}
						else
						{
							for(z=sz0;z<sz1;z++)
							{
								estnorm(x,y,z,&tp);
								f = (tp.y*.5+tp.z)*16+47.5;
								for(i=lightcnt-1;i>=0;i--)
								{
									j = lightlst[i];
									fx = vx5.lightsrc[j].p.x-(float)x;
									fy = vx5.lightsrc[j].p.y-(float)y;
									fz = vx5.lightsrc[j].p.z-(float)z;
									h = tp.x*fx+tp.y*fy+tp.z*fz; if (*(int *)&h >= 0) continue;
									g = fx*fx+fy*fy+fz*fz; if (g >= vx5.lightsrc[j].r2) continue;

									g = 1.0/(g*sqrt(g))-lightsub[i]; //1.0/g;
									f -= g*h*vx5.lightsrc[j].sc;
								}
								if (*(int *)&f > 0x437f0000) f = 255; //0x437f0000 is 255.0
								ftol(f,&i);
								v[(z<<2)+offs] = *(char *)&i;
							}
						}
					}
				}
		}
	}
}

//float detection & falling code begins --------------------------------------
//How to use this section of code:
//Step 1: Call checkfloatinbox after every "deleting" set* call
//Step 2: Call dofalls(); at a constant rate in movement code

	//Adds all slabs inside box (inclusive) to "float check" list
void checkfloatinbox (int x0, int y0, int z0, int x1, int y1, int z1)
{
	int x, y;
	char *ov, *v;

	if (flchkcnt >= FLCHKSIZ) return;

		//Make all off-by-1 hacks in other code unnecessary
	x0 = MAX(x0-1,0); x1 = MIN(x1+1,VSID);
	y0 = MAX(y0-1,0); y1 = MIN(y1+1,VSID);
	z0 = MAX(z0-1,0); z1 = MIN(z1+1,MAXZDIM);

		//Add local box's slabs to flchk list - checked in next dofalls()
	for(y=y0;y<y1;y++)
		for(x=x0;x<x1;x++)
		{
			v = sptr[y*VSID+x];
			while (1)
			{
				ov = v; if ((z1 <= v[1]) || (!v[0])) break;
				v += v[0]*4; if (z0 >= v[3]) continue;
				flchk[flchkcnt].x = x;
				flchk[flchkcnt].y = y;
				flchk[flchkcnt].z = ov[1];
				flchkcnt++; if (flchkcnt >= FLCHKSIZ) return;
			}
		}
}

void isnewfloatingadd (size_t f)
{
	int v = (((f>>(LOGHASHEAD+3))-(f>>3)) & ((1<<LOGHASHEAD)-1));
	vlst[vlstcnt].b = hhead[v]; hhead[v] = vlstcnt;
	vlst[vlstcnt].v = f; vlstcnt++;
}

int isnewfloatingot (size_t f)
{
	int v = hhead[((f>>(LOGHASHEAD+3))-(f>>3)) & ((1<<LOGHASHEAD)-1)];
	while (1)
	{
		if (v < 0) return(-1);
		if (vlst[v].v == f) return(v);
		v = vlst[v].b;
	}
}

	//removes a & adds b while preserving index; used only by meltfall(...)
	//Must do nothing if 'a' not in hash
void isnewfloatingchg (size_t a, size_t b)
{
	int ov, v, i, j;

	i = (((a>>(LOGHASHEAD+3))-(a>>3)) & ((1<<LOGHASHEAD)-1));
	j = (((b>>(LOGHASHEAD+3))-(b>>3)) & ((1<<LOGHASHEAD)-1));

	v = hhead[i]; ov = -1;
	while (v >= 0)
	{
		if (vlst[v].v == a)
		{
			vlst[v].v = b; if (i == j) return;
			if (ov < 0) hhead[i] = vlst[v].b; else vlst[ov].b = vlst[v].b;
			vlst[v].b = hhead[j]; hhead[j] = v;
			return;
		}
		ov = v; v = vlst[v].b;
	}
}

int isnewfloating (flstboxtype *flb)
{
	float f;
	lpoint3d p, cen;
	int i, j, nx, ny, z0, z1, fend, ovlstcnt, mass;
	char *v, *ov;

	p.x = flb->chk.x; p.y = flb->chk.y; p.z = flb->chk.z;
	v = sptr[p.y*VSID+p.x];
	while (1)
	{
		ov = v;
		if ((p.z < v[1]) || (!v[0])) return(0);
		v += v[0]*4;
		if (p.z < v[3]) break;
	}

	if (isnewfloatingot((size_t)ov) >= 0) return(0);
	ovlstcnt = vlstcnt;
	isnewfloatingadd((size_t)ov);
	if (vlstcnt >= VLSTSIZ) return(0); //EVIL HACK TO PREVENT CRASH!

		//Init: centroid, mass, bounding box
	cen = p; mass = 1;
	flb->x0 = p.x-1; flb->y0 = p.y-1; flb->z0 = p.z-1;
	flb->x1 = p.x+1; flb->y1 = p.y+1; flb->z1 = p.z+1;

	fend = 0;
	while (1)
	{
		z0 = ov[1];         if (z0 < flb->z0) flb->z0 = z0;
		z1 = ov[ov[0]*4+3]; if (z1 > flb->z1) flb->z1 = z1;

		i = z1-z0;
		cen.x += p.x*i;
		cen.y += p.y*i;
		cen.z += (((z0+z1)*i)>>1); //sum(z0 to z1-1)
		mass += i;

		for(i=0;i<4;i++) //26-connectivity
		{
			switch(i)
			{
				case 0: nx = p.x-1; ny = p.y  ; if (nx < flb->x0) flb->x0 = nx; break;
				case 1: nx = p.x  ; ny = p.y-1; if (ny < flb->y0) flb->y0 = ny; break;
				case 2: nx = p.x+1; ny = p.y  ; if (nx > flb->x1) flb->x1 = nx; break;
				case 3: nx = p.x  ; ny = p.y+1; if (ny > flb->y1) flb->y1 = ny; break;
				case 8: nx = p.x-1; ny = p.y-1; break;
				case 5: nx = p.x+1; ny = p.y-1; break;
				case 6: nx = p.x-1; ny = p.y+1; break;
				case 7: nx = p.x+1; ny = p.y+1; break;
			}
			if ((unsigned int)(nx|ny) >= VSID) continue;

			v = sptr[ny*VSID+nx];
			while (1)
			{
				if (!v[0])
				{
					if (v[1] <= z1) return(0);  //This MUST be <=, (not <) !!!
					break;
				}
				ov = v; v += v[0]*4; //NOTE: this is a 'different' ov
				if ((ov[1] > z1) || (z0 > v[3])) continue; //26-connectivity
				j = isnewfloatingot((size_t)ov);
				if (j < 0)
				{
					isnewfloatingadd((size_t)ov);
					if (vlstcnt >= VLSTSIZ) return(0); //EVIL HACK TO PREVENT CRASH!
					fstk[fend].x = nx; fstk[fend].y = ny; fstk[fend].z = (size_t)ov;
					fend++; if (fend >= FSTKSIZ) return(0); //EVIL HACK TO PREVENT CRASH!
					continue;
				}
				if ((unsigned int)j < ovlstcnt) return(0);
			}
		}

		if (!fend)
		{
			flb->i0 = ovlstcnt;
			flb->i1 = vlstcnt;
			flb->mass = mass; f = 1.0 / (float)mass;
			flb->centroid.x = (float)cen.x*f;
			flb->centroid.y = (float)cen.y*f;
			flb->centroid.z = (float)cen.z*f;
			return(1);
		}
		fend--;
		p.x = fstk[fend].x; p.y = fstk[fend].y; ov = (char *)fstk[fend].z;
	}
}

void startfalls ()
{
	int i, z;

		//This allows clear to be MUCH faster when there isn't much falling
	if (vlstcnt < ((1<<LOGHASHEAD)>>1))
	{
		for(i=vlstcnt-1;i>=0;i--)
		{
			z = vlst[i].v;
			hhead[((z>>(LOGHASHEAD+3))-(z>>3)) & ((1<<LOGHASHEAD)-1)] = -1;
		}
	}
	else { for(z=0;z<(1<<LOGHASHEAD);z++) hhead[z] = -1; }

		//Float detection...
		//flstcnt[].i0/i1 tell which parts of vlst are floating
	vlstcnt = 0;

		//Remove any current pieces that are no longer floating
	for(i=vx5.flstnum-1;i>=0;i--)
		if (!isnewfloating(&vx5.flstcnt[i])) //Modifies flstcnt,vlst[],vlstcnt
			vx5.flstcnt[i] = vx5.flstcnt[--vx5.flstnum]; //onground, so delete flstcnt[i]

		//Add new floating pieces (while space is left on flstcnt)
	if (vx5.flstnum < FLPIECES)
		for(i=flchkcnt-1;i>=0;i--)
		{
			vx5.flstcnt[vx5.flstnum].chk = flchk[i];
			if (isnewfloating(&vx5.flstcnt[vx5.flstnum])) //Modifies flstcnt,vlst[],vlstcnt
			{
				vx5.flstcnt[vx5.flstnum].userval = -1; //New piece: let game programmer know
				vx5.flstnum++; if (vx5.flstnum >= FLPIECES) break;
			}
		}
	flchkcnt = 0;
}

	//Call 0 or 1 times (per flstcnt) between startfalls&finishfalls
void dofall (int i)
{
	int j, z;
	char *v;

		//Falling code... call this function once per piece
	vx5.flstcnt[i].chk.z++;
	for(z=vx5.flstcnt[i].i1-1;z>=vx5.flstcnt[i].i0;z--)
	{
		v = (char *)vlst[z].v; v[1]++; v[2]++;
		v = &v[v[0]*4];
		v[3]++;
		if ((v[3] == v[1]) && (vx5.flstcnt[i].i1 >= 0))
		{
			j = isnewfloatingot((size_t)v);
				//Make sure it's not part of the same floating object
			if ((j < vx5.flstcnt[i].i0) || (j >= vx5.flstcnt[i].i1))
				vx5.flstcnt[i].i1 = -1; //Mark flstcnt[i] for scum2 fixup
		}
	}

	if (vx5.vxlmipuse > 1)
	{
		int x0, y0, x1, y1;
		x0 = MAX(vx5.flstcnt[i].x0,0); x1 = MIN(vx5.flstcnt[i].x1+1,VSID);
		y0 = MAX(vx5.flstcnt[i].y0,0); y1 = MIN(vx5.flstcnt[i].y1+1,VSID);
		//FIX ME!!!
		//if ((x1 > x0) && (y1 > y0)) genmipvxl(x0,y0,x1,y1); //Don't replace with bbox!
	}
}
	//Sprite structure is already allocated
	//kv6, vox, xlen, ylen are all malloced in here!
int meltfall (vx5sprite *spr, int fi, int delvxl)
{
	int j, k, x, y, z, xs, ys, zs, xe, ye, ze;
	size_t i;
	int oxvoxs, oyvoxs, numvoxs;
	char *v, *ov, *nv;
	kv6data *kv;
	kv6voxtype *voxptr;
	unsigned int *xlenptr;
	unsigned short *ylenptr;

	if (vx5.flstcnt[fi].i1 < 0) return(0);

	xs = MAX(vx5.flstcnt[fi].x0,0); xe = MIN(vx5.flstcnt[fi].x1,VSID-1);
	ys = MAX(vx5.flstcnt[fi].y0,0); ye = MIN(vx5.flstcnt[fi].y1,VSID-1);
	zs = MAX(vx5.flstcnt[fi].z0,0); ze = MIN(vx5.flstcnt[fi].z1,MAXZDIM-1);
	if ((xs > xe) || (ys > ye) || (zs > ze)) return(0);

		//Need to know how many voxels to allocate... SLOW :(
	numvoxs = vx5.flstcnt[fi].i0-vx5.flstcnt[fi].i1;
	for(i=vx5.flstcnt[fi].i0;i<vx5.flstcnt[fi].i1;i++)
		numvoxs += ((char *)vlst[i].v)[0];
	if (numvoxs <= 0) return(0); //No voxels found!

	spr->p = vx5.flstcnt[fi].centroid;
	spr->s.x = 1.f; spr->h.x = 0.f; spr->f.x = 0.f;
	spr->s.y = 0.f; spr->h.y = 1.f; spr->f.y = 0.f;
	spr->s.z = 0.f; spr->h.z = 0.f; spr->f.z = 1.f;

	x = xe-xs+1; y = ye-ys+1; z = ze-zs+1;

	j = sizeof(kv6data) + numvoxs*sizeof(kv6voxtype) + x*4 + x*y*2;
	i = (size_t)malloc(j); if (!i) return(0); if (i&3) { free((void *)i); return(0); }
	spr->voxnum = kv = (kv6data *)i; spr->flags = 0;
	kv->leng = j;
	kv->xsiz = x;
	kv->ysiz = y;
	kv->zsiz = z;
	kv->xpiv = spr->p.x - xs;
	kv->ypiv = spr->p.y - ys;
	kv->zpiv = spr->p.z - zs;
	kv->numvoxs = numvoxs;
	kv->namoff = 0;
	kv->lowermip = 0;
	kv->vox = (kv6voxtype *)((size_t)spr->voxnum+sizeof(kv6data));
	kv->xlen = (unsigned int *)(((size_t)kv->vox)+numvoxs*sizeof(kv6voxtype));
	kv->ylen = (unsigned short *)(((size_t)kv->xlen) + kv->xsiz*4);

	voxptr = kv->vox; numvoxs = 0;
	xlenptr = kv->xlen; oxvoxs = 0;
	ylenptr = kv->ylen; oyvoxs = 0;

	for(x=xs;x<=xe;x++)
	{
		for(y=ys;y<=ye;y++)
		{
			for(v=sptr[y*VSID+x];v[0];v=nv)
			{
				nv = v+v[0]*4;

				i = isnewfloatingot((size_t)v);
				if (((unsigned int)i >= vx5.flstcnt[fi].i1) || (i < vx5.flstcnt[fi].i0))
					continue;

				for(z=v[1];z<=v[2];z++)
				{
					voxptr[numvoxs].col = lightvox(*(int *)&v[((z-v[1])<<2)+4]);
					voxptr[numvoxs].z = z-zs;

					voxptr[numvoxs].vis = 0; //OPTIMIZE THIS!!!
					if (!isvoxelsolid(x-1,y,z)) voxptr[numvoxs].vis |= 1;
					if (!isvoxelsolid(x+1,y,z)) voxptr[numvoxs].vis |= 2;
					if (!isvoxelsolid(x,y-1,z)) voxptr[numvoxs].vis |= 4;
					if (!isvoxelsolid(x,y+1,z)) voxptr[numvoxs].vis |= 8;
					//if (z == v[1]) voxptr[numvoxs].vis |= 16;
					//if (z == nv[3]-1) voxptr[numvoxs].vis |= 32;
					if (!isvoxelsolid(x,y,z-1)) voxptr[numvoxs].vis |= 16;
					if (!isvoxelsolid(x,y,z+1)) voxptr[numvoxs].vis |= 32;

					voxptr[numvoxs].dir = 0; //FIX THIS!!!
					numvoxs++;
				}
				for(z=nv[3]+v[2]-v[1]-v[0]+2;z<nv[3];z++)
				{
					voxptr[numvoxs].col = lightvox(*(int *)&nv[(z-nv[3])<<2]);
					voxptr[numvoxs].z = z-zs;

					voxptr[numvoxs].vis = 0; //OPTIMIZE THIS!!!
					if (!isvoxelsolid(x-1,y,z)) voxptr[numvoxs].vis |= 1;
					if (!isvoxelsolid(x+1,y,z)) voxptr[numvoxs].vis |= 2;
					if (!isvoxelsolid(x,y-1,z)) voxptr[numvoxs].vis |= 4;
					if (!isvoxelsolid(x,y+1,z)) voxptr[numvoxs].vis |= 8;
					//if (z == v[1]) voxptr[numvoxs].vis |= 16;
					//if (z == nv[3]-1) voxptr[numvoxs].vis |= 32;
					if (!isvoxelsolid(x,y,z-1)) voxptr[numvoxs].vis |= 16;
					if (!isvoxelsolid(x,y,z+1)) voxptr[numvoxs].vis |= 32;

					voxptr[numvoxs].dir = 0; //FIX THIS!!!
					numvoxs++;
				}

#if 0
				if (delvxl) //Quick&dirty dealloc from VXL (bad for holes!)
				{
						//invalidate current vptr safely
					isnewfloatingchg((int)v,0);

					k = nv-v; //perform slng(nv) and adjust vlst at same time
					for(ov=nv;ov[0];ov+=ov[0]*4)
						isnewfloatingchg((int)ov,((int)ov)-k);

					j = (int)ov-(int)nv+(ov[2]-ov[1]+1)*4+4;

						//shift end of RLE column up
					v[0] = nv[0]; v[1] = nv[1]; v[2] = nv[2];
					for(i=4;i<j;i+=4) *(int *)&v[i] = *(int *)&nv[i];

						//remove end of RLE column from vbit
					i = ((((int)(&v[i]))-(int)vbuf)>>2); j = (k>>2)+i;
#if 0
					while (i < j) { vbit[i>>5] &= ~(1<<i); i++; }
#else
					if (!((j^i)&~31))
						vbit[i>>5] &= ~(p2m[j&31]^p2m[i&31]);
					else
					{
						vbit[i>>5] &=   p2m[i&31];  i >>= 5;
						vbit[j>>5] &= (~p2m[j&31]); j >>= 5;
						for(j--;j>i;j--) vbit[j] = 0;
					}
#endif
					nv = v;
				}
#endif
			}
			*ylenptr++ = numvoxs-oyvoxs; oyvoxs = numvoxs;
		}
		*xlenptr++ = numvoxs-oxvoxs; oxvoxs = numvoxs;
	}

	if (delvxl)
		for(x=xs;x<=xe;x++)
			for(y=ys;y<=ye;y++)
				for(v=sptr[y*VSID+x];v[0];v=nv)
				{
					nv = v+v[0]*4;

					i = isnewfloatingot((size_t)v);
					if (((unsigned int)i >= vx5.flstcnt[fi].i1) || (i < vx5.flstcnt[fi].i0))
						continue;

						//Quick&dirty dealloc from VXL (bad for holes!)

						//invalidate current vptr safely
					isnewfloatingchg((size_t)v,0);

					k = nv-v; //perform slng(nv) and adjust vlst at same time
					for(ov=nv;ov[0];ov+=ov[0]*4)
						isnewfloatingchg((size_t)ov,((size_t)ov)-k);

					j = (ptrdiff_t)ov-(ptrdiff_t)nv+(ov[2]-ov[1]+1)*4+4;

						//shift end of RLE column up
					v[0] = nv[0]; v[1] = nv[1]; v[2] = nv[2];
					for(i=4;i<j;i+=4) *(int *)&v[i] = *(int *)&nv[i];

						//remove end of RLE column from vbit
					i = ((((ptrdiff_t)(&v[i]))-(ptrdiff_t)vbuf)>>2); j = (k>>2)+i;
#if 0
					while (i < j) { vbit[i>>5] &= ~(1<<i); i++; }
#else
					if (!((j^i)&~31))
						vbit[i>>5] &= ~(p2m[j&31]^p2m[i&31]);
					else
					{
						vbit[i>>5] &=   p2m[i&31];  i >>= 5;
						vbit[j>>5] &= (~p2m[j&31]); j >>= 5;
						for(j--;j>i;j--) vbit[j] = 0;
					}
#endif
					nv = v;
				}

	vx5.flstcnt[fi].i1 = -2; //Mark flstcnt[i] invalid; no scum2 fixup

	if (vx5.vxlmipuse > 1) genmipvxl(xs,ys,xe+1,ye+1);

	return(vx5.flstcnt[fi].mass);
}

int Vox_DeleteFloatingBlock (int fi)
{
	int j, k, x, y, z, xs, ys, zs, xe, ye, ze;
	size_t i;
	int oxvoxs, oyvoxs, numvoxs;
	char *v, *ov, *nv;
	kv6voxtype *voxptr;
	unsigned int *xlenptr;
	unsigned short *ylenptr;

	if (vx5.flstcnt[fi].i1 < 0) return(0);

	xs = MAX(vx5.flstcnt[fi].x0,0); xe = MIN(vx5.flstcnt[fi].x1,VSID-1);
	ys = MAX(vx5.flstcnt[fi].y0,0); ye = MIN(vx5.flstcnt[fi].y1,VSID-1);
	zs = MAX(vx5.flstcnt[fi].z0,0); ze = MIN(vx5.flstcnt[fi].z1,MAXZDIM-1);
	if ((xs > xe) || (ys > ye) || (zs > ze)) return(0);

		//Need to know how many voxels to allocate... SLOW :(
	numvoxs = vx5.flstcnt[fi].i0-vx5.flstcnt[fi].i1;
	for(i=vx5.flstcnt[fi].i0;i<vx5.flstcnt[fi].i1;i++)
		numvoxs += ((char *)vlst[i].v)[0];
	if (numvoxs <= 0) return(0); //No voxels found!

	x = xe-xs+1; y = ye-ys+1; z = ze-zs+1;

	j = sizeof(kv6data) + numvoxs*sizeof(kv6voxtype) + x*4 + x*y*2;
	i = (size_t)malloc(j); if (!i) return(0); if (i&3) { free((void *)i); return(0); }
		for(x=xs;x<=xe;x++)
			for(y=ys;y<=ye;y++)
				for(v=sptr[y*VSID+x];v[0];v=nv)
				{
					nv = v+v[0]*4;

					i = isnewfloatingot((size_t)v);
					if (((unsigned int)i >= vx5.flstcnt[fi].i1) || (i < vx5.flstcnt[fi].i0))
						continue;

						//Quick&dirty dealloc from VXL (bad for holes!)

						//invalidate current vptr safely
					isnewfloatingchg((size_t)v,0);

					k = nv-v; //perform slng(nv) and adjust vlst at same time
					for(ov=nv;ov[0];ov+=ov[0]*4)
						isnewfloatingchg((size_t)ov,((size_t)ov)-k);

					j = (ptrdiff_t)ov-(ptrdiff_t)nv+(ov[2]-ov[1]+1)*4+4;

						//shift end of RLE column up
					v[0] = nv[0]; v[1] = nv[1]; v[2] = nv[2];
					for(i=4;i<j;i+=4) *(int *)&v[i] = *(int *)&nv[i];

						//remove end of RLE column from vbit
					i = ((((ptrdiff_t)(&v[i]))-(ptrdiff_t)vbuf)>>2); j = (k>>2)+i;
#if 0
					while (i < j) { vbit[i>>5] &= ~(1<<i); i++; }
#else
					if (!((j^i)&~31))
						vbit[i>>5] &= ~(p2m[j&31]^p2m[i&31]);
					else
					{
						vbit[i>>5] &=   p2m[i&31];  i >>= 5;
						vbit[j>>5] &= (~p2m[j&31]); j >>= 5;
						for(j--;j>i;j--) vbit[j] = 0;
					}
#endif
					nv = v;
				}

	vx5.flstcnt[fi].i1 = -2; //Mark flstcnt[i] invalid; no scum2 fixup

	if (vx5.vxlmipuse > 1) genmipvxl(xs,ys,xe+1,ye+1);

	return(vx5.flstcnt[fi].mass);
}

void finishfalls ()
{
	int i, x, y;

		//Scum2 box fixup: refreshes rle voxel data inside a bounding rectangle
	for(i=vx5.flstnum-1;i>=0;i--)
		if (vx5.flstcnt[i].i1 < 0)
		{
			if (vx5.flstcnt[i].i1 == -1)
			{
				for(y=vx5.flstcnt[i].y0;y<=vx5.flstcnt[i].y1;y++)
					for(x=vx5.flstcnt[i].x0;x<=vx5.flstcnt[i].x1;x++)
						scum2(x,y);
				scum2finish();
				updatebbox(vx5.flstcnt[i].x0,vx5.flstcnt[i].y0,vx5.flstcnt[i].z0,vx5.flstcnt[i].x1,vx5.flstcnt[i].y1,vx5.flstcnt[i].z1,0);
			}
			vx5.flstcnt[i] = vx5.flstcnt[--vx5.flstnum]; //onground, so delete flstcnt[i]
		}
}

//float detection & falling code ends ----------------------------------------

//----------------------------------------------------------------------------

/**
 * Since voxlap is currently a software renderer and I don't have any system
 * dependent code in it, you must provide it with the frame buffer. You
 * MUST call this once per frame, AFTER startdirectdraw(), but BEFORE any
 * functions that access the frame buffer.
 * @param p pointer to the top-left corner of the frame
 * @param b pitch (bytes per line)
 * @param x frame width
 * @param y frame height
 */

VOXLAP_DLL_FUNC void voxsetframebuffer (int *p, int b, int x, int y)
{
	int i,j;

	frameplace = (unsigned char*)p;
	if (x > MAXXDIM) x = MAXXDIM; //This sucks, but it crashes without it
	if (y > MAXYDIM) y = MAXYDIM;

		//Set global variables used by kv6draw's PIII asm (drawboundcube)
	qsum1[3] = qsum1[1] = 0x7fff-y; qsum1[2] = qsum1[0] = 0x7fff-x;
	kv6bytesperline = qbplbpp[1] = b; qbplbpp[0] = 4;
	kv6frameplace = (size_t)(p - (qsum1[0]*qbplbpp[0] + qsum1[1]*qbplbpp[1]));
	if ((b != ylookup[1]) || (x != xres_voxlap) || (y != yres_voxlap))
	{
		bytesperline = b; xres_voxlap = x; yres_voxlap = y; xres4_voxlap = (xres_voxlap<<2);
		ylookup[0] = 0; for(i=0;i<yres_voxlap;i++) ylookup[i+1] = ylookup[i]+bytesperline;
		//gihx = gihz = (float)xres_voxlap*.5f; gihy = (float)yres_voxlap*.5f; //BAD!!!
#if (USEZBUFFER == 1)
		if ((ylookup[yres_voxlap]+256 > zbuffersiz) || (!zbuffermem))  //Increase Z buffer size if too small
		{
			if (zbuffermem) { free(zbuffermem); zbuffermem = 0; }
			zbuffersiz = ylookup[yres_voxlap]+256;
			if (!(zbuffermem = (int *)malloc(zbuffersiz))) evilquit("voxsetframebuffer: allocation too big");
		}
#endif
	}
#if (USEZBUFFER == 1)
		//zbuffer aligns its memory to the same pixel boundaries as the screen!
		//WARNING: Pentium 4's L2 cache has severe slowdowns when 65536-64 <= (zbufoff&65535) < 64
	#if (__64BIT_SYSTEM__==0)
	zbufoff = (((((unsigned char*)zbuffermem)-frameplace-128)+255)&~255)+128;
	#else
	zbufoff=((unsigned char*)zbuffermem)-frameplace;
	#endif
	vx5.zbufoff=zbufoff;
#endif

	if (vx5.fogcol >= 0)
	{
		fogcol = (((int64_t)(vx5.fogcol&0xff0000))<<16) +
					(((int64_t)(vx5.fogcol&0x00ff00))<< 8) +
					(((int64_t)(vx5.fogcol&0x0000ff))    );

		if (vx5.maxscandist > 1023) vx5.maxscandist = 1023;
		if ((vx5.maxscandist != ofogdist) && (vx5.maxscandist > 0))
		{
			ofogdist = vx5.maxscandist;

			//foglut[?>>20] = MIN(?*32767/vx5.maxscandist,32767)
			int k, l;
			j = 0; l = 0x7fffffff/vx5.maxscandist;
			for(i=0;i<2048;i++)
			{
				k = (j>>16); j += l;
				if (k < 0) break;
				foglut[i] = (((int64_t)k)<<32)+(((int64_t)k)<<16)+((int64_t)k);
			}
			while (i < 2048) foglut[i++] = all32767;
		}
	} else ofogdist = -1;

	if (cputype&(1<<25)) drawboundcubesseinit(); else drawboundcube3dninit();
}

//------------------------ Simple PNG OUT code begins ------------------------
FILE *pngofil=NULL;
int pngoxplc=0, pngoyplc=0, pngoxsiz=0, pngoysiz=0;
unsigned int pngocrc=0, pngoadcrc=0;

static int crctab32[256];  //SEE CRC32.C
#define updatecrc32(c,crc) crc=(crctab32[(crc^c)&255]^(((unsigned)crc)>>8))
#define updateadl32(c,crc) \
{  c += (crc&0xffff); if (c   >= 65521) c   -= 65521; \
	crc = (crc>>16)+c; if (crc >= 65521) crc -= 65521; \
	crc = (crc<<16)+c; \
} \

void fputbytes (unsigned int v, int n)
	{ for(;n;v>>=8,n--) { fputc(v,pngofil); updatecrc32(v,pngocrc); } }

void pngoutopenfile (const char *fnam, int xsiz, int ysiz)
{
	int i, j, k;
	char a[40];

	pngoxsiz = xsiz; pngoysiz = ysiz; pngoxplc = pngoyplc = 0;
	for(i=255;i>=0;i--)
	{
		k = i; for(j=8;j;j--) k = ((unsigned int)k>>1)^((-(k&1))&0xedb88320);
		crctab32[i] = k;
	}
	pngofil = fopen(fnam,"wb");
	*(int *)&a[0] = 0x474e5089; *(int *)&a[4] = 0x0a1a0a0d;
	*(int *)&a[8] = 0x0d000000; *(int *)&a[12] = 0x52444849;
	*(int *)&a[16] = bswap(xsiz); *(int *)&a[20] = bswap(ysiz);
	*(int *)&a[24] = 0x00000208; *(int *)&a[28] = 0;
	for(i=12,j=-1;i<29;i++) updatecrc32(a[i],j);
	*(int *)&a[29] = bswap(j^-1);
	fwrite(a,37,1,pngofil);
	pngocrc = 0xffffffff; pngoadcrc = 1;
	fputbytes(0x54414449,4); fputbytes(0x0178,2);
}

void pngoutputpixel (int rgbcol)
{
	int a[4];

	if (!pngoxplc)
	{
		fputbytes(pngoyplc==pngoysiz-1,1);
		fputbytes(((pngoxsiz*3+1)*0x10001)^0xffff0000,4);
		fputbytes(0,1); a[0] = 0; updateadl32(a[0],pngoadcrc);
	}
	fputbytes(bswap(rgbcol<<8),3);
	a[0] = (rgbcol>>16)&255; updateadl32(a[0],pngoadcrc);
	a[0] = (rgbcol>> 8)&255; updateadl32(a[0],pngoadcrc);
	a[0] = (rgbcol    )&255; updateadl32(a[0],pngoadcrc);
	pngoxplc++; if (pngoxplc < pngoxsiz) return;
	pngoxplc = 0; pngoyplc++; if (pngoyplc < pngoysiz) return;
	fputbytes(bswap(pngoadcrc),4);
	a[0] = bswap(pngocrc^-1); a[1] = 0; a[2] = 0x444e4549; a[3] = 0x826042ae;
	fwrite(a,1,16,pngofil);
	a[0] = bswap(ftell(pngofil)-(33+8)-16);
	fseek(pngofil,33,SEEK_SET); fwrite(a,1,4,pngofil);
	fclose(pngofil);
}
//------------------------- Simple PNG OUT code ends -------------------------

/**
 * Captures a screenshot of the current frame to disk. The current frame
 * is defined by the last call to the voxsetframebuffer function. NOTE:
 * you MUST call this function while video memory is accessible. In
 * DirectX, that means it must be between a call to startdirectdraw and
 * stopdirectdraw.
 * @param fname filename to write to (writes uncompressed .PNG format)
 * @return 0:always
 */
int screencapture32bit (const char *fname)
{
	int x, y;
	unsigned char *p;

	pngoutopenfile(fname,xres_voxlap,yres_voxlap);
	p = frameplace;
	for(y=0;y<yres_voxlap;y++,p+=bytesperline)
		for(x=0;x<xres_voxlap;x++)
			pngoutputpixel(*(int *)(p+(x<<2)));

	return(0);
}

/**
 * Generates a cubic panorama (skybox) from the given position
 * This is an old function that is very slow, but it is pretty cool
 * being able to view a full panorama screenshot. Unfortunately, it
 * doesn't draw sprites or the sky.
 *
 * @param pos VXL map position of camera
 * @param fname filename to write to (writes uncompressed .PNG format)
 * @param boxsiz length of side of square. I recommend using 256 or 512 for this.
 * @return 0:always
 */
int surroundcapture32bit (dpoint3d *pos, const char *fname, int boxsiz)
{
	lpoint3d hit;
	dpoint3d d;
	int x, y, hboxsiz, *hind, hdir;
	float f;

	//Picture layout:
	//   ÛÛÛÛÛÛúúúú
	//   úúúúÛÛÛÛÛÛ

	f = 2.0 / (float)boxsiz; hboxsiz = (boxsiz>>1);
	pngoutopenfile(fname,boxsiz*5,boxsiz*2);
	for(y=-hboxsiz;y<hboxsiz;y++)
	{
		for(x=-hboxsiz;x<hboxsiz;x++) //(1,1,-1) - (-1,1,1)
		{
			d.x = -(x+.5)*f; d.y = 1; d.z = (y+.5)*f;
			hitscan(pos,&d,&hit,&hind,&hdir);
			if (hind) pngoutputpixel(lightvox(*hind)); else pngoutputpixel(0);
		}
		for(x=-hboxsiz;x<hboxsiz;x++) //(-1,1,-1) - (-1,-1,1)
		{
			d.x = -1; d.y = -(x+.5)*f; d.z = (y+.5)*f;
			hitscan(pos,&d,&hit,&hind,&hdir);
			if (hind) pngoutputpixel(lightvox(*hind)); else pngoutputpixel(0);
		}
		for(x=-hboxsiz;x<hboxsiz;x++) //(-1,-1,-1) - (1,-1,1)
		{
			d.x = (x+.5)*f; d.y = -1; d.z = (y+.5)*f;
			hitscan(pos,&d,&hit,&hind,&hdir);
			if (hind) pngoutputpixel(lightvox(*hind)); else pngoutputpixel(0);
		}
		for(x=(boxsiz<<1);x>0;x--) pngoutputpixel(0);
	}
	for(y=-hboxsiz;y<hboxsiz;y++)
	{
		for(x=(boxsiz<<1);x>0;x--) pngoutputpixel(0);
		for(x=-hboxsiz;x<hboxsiz;x++) //(-1,-1,1) - (1,1,1)
		{
			d.x = (x+.5)*f; d.y = (y+.5)*f; d.z = 1;
			hitscan(pos,&d,&hit,&hind,&hdir);
			if (hind) pngoutputpixel(lightvox(*hind)); else pngoutputpixel(0);
		}
		for(x=-hboxsiz;x<hboxsiz;x++) //(1,-1,1) - (1,1,-1)
		{
			d.x = 1; d.y = (y+.5)*f; d.z = -(x+.5)*f;
			hitscan(pos,&d,&hit,&hind,&hdir);
			if (hind) pngoutputpixel(lightvox(*hind)); else pngoutputpixel(0);
		}
		for(x=-hboxsiz;x<hboxsiz;x++) //(1,-1,-1) - (-1,1,-1)
		{
			d.x = -(x+.5)*f; d.y = (y+.5)*f; d.z = -1;
			hitscan(pos,&d,&hit,&hind,&hdir);
			if (hind) pngoutputpixel(lightvox(*hind)); else pngoutputpixel(0);
		}
	}
	return(0);
}

/**
 * If you generate any sprites using one of the melt* functions, and then
 * generate mip-maps for it, you can use this function to de-allocate
 * all mip-maps of the .KV6 safely. You don't need to use this for
 * kv6data objects that were loaded by getkv6,getkfa, or getspr since
 * these functions automatically de-allocate them using this function.
 *
 * @param kv6 pointer to kv6 voxel sprite
 */
void freekv6 (kv6data *kv6)
{
	if (kv6->lowermip) freekv6(kv6->lowermip); //NOTE: dangerous - recursive!
	free((void *)kv6);
}

VOXLAP_DLL_FUNC void uninitvoxlap ()
{
	//if (sxlbuf) { free(sxlbuf); sxlbuf = 0; }

	if (vbuf) { free(vbuf); vbuf = 0; }
	if (vbit) { free(vbit); vbit = 0; }

	if (khashbuf)
	{     //Free all KV6&KFA on hash list
		int i, j;
		kfatype *kfp;
		for(i=0;i<khashpos;i+=strlen(&khashbuf[i+9])+10)
		{
			switch (khashbuf[i+8])
			{
				case 0: //KV6
					freekv6(*(kv6data **)&khashbuf[i+4]);
					break;
				case 1: //KFA
					kfp = *(kfatype **)&khashbuf[i+4];
					if (!kfp) continue;
					if (kfp->seq) free((void *)kfp->seq);
					if (kfp->frmval) free((void *)kfp->frmval);
					if (kfp->hingesort) free((void *)kfp->hingesort);
					if (kfp->hinge) free((void *)kfp->hinge);
					if (kfp->spr)
					{
						for(j=kfp->numspr-1;j>=0;j--)
							if (kfp->spr[j].voxnum)
								freekv6((kv6data *)kfp->spr[j].voxnum);
						free((void *)kfp->spr);
					}
					free((void *)kfp);
					break;
				default: _gtfo(); //tells MSVC default can't be reached
			}
		}
		free(khashbuf); khashbuf = 0; khashpos = khashsiz = 0;
	}

	if (skylng) { free((void *)skylng); skylng = 0; }
	if (skylat) { free((void *)skylat); skylat = 0; }
	if (skypic) { free((void *)skypic); skypic = skyoff = 0; }

	if (vx5.pic) { free(vx5.pic); vx5.pic = 0; }
#if (USEZBUFFER == 1)
	if (zbuffermem) { free(zbuffermem); zbuffermem = 0; }
#endif
	if (radarmem) { free(radarmem); radarmem = 0; radar = 0; }
}

VOXLAP_DLL_FUNC int initvoxlap ()
{
	int64_t q;
	int j, k, z, zz;
	volatile unsigned int i;
	float f, ff;
	
	//This probably is better done via macros, but I wanted to be 100% sure
	if(sizeof(size_t)!=sizeof(char*)){
		evilquit("size_t doesn't have the size of a pointer on the target system!");
		return -1;
	}
	if(sizeof(ptrdiff_t)!=sizeof(size_t)){
		evilquit("ptrdiff_t is not big enough (sizeof(ptrdiff_t)!=sizeof(size_t))!");
		return -1;
	}
	if(sizeof(unsigned int)!=4){
		evilquit("unsigned int isn't 4 bytes big!");
		return -1;
	}
		//unlocking code memory for self-modifying code
	#if (defined(USEV5ASM) && (USEV5ASM != 0)) //if true
	code_rwx_unlock((void *)dep_protect_start, (void *)dep_protect_end);
	#endif

		//CPU Must have: FPU,RDTSC,CMOV,MMX,MMX+
	/*if ((cputype&((1<<0)|(1<<4)|(1<<15)|(1<<22)|(1<<23))) !=
					 ((1<<0)|(1<<4)|(1<<15)|(1<<22)|(1<<23))) return(-1);*/
		//Useless since every modern CPU has MMX, a FPU (and the other stuff above too Im sure)
		
		//CPU UNSUPPORTED!
	/*if ((!(cputype&(1<<25))) && //SSE
		(!((cputype&((1<<30)|(1<<31))) == ((1<<30)|(1<<31))))) //3DNow!+
		return(-2); Read above*/
	//if (cputype&(1<<25)) fixsse(); //SSE

	  //WARNING: xres_voxlap & yres_voxlap are local to VOXLAP5.C so don't rely on them here!
	if (!(radarmem = (int *)malloc(MAX((((MAXXDIM*MAXYDIM*27)>>1)+7)&~7,(VSID+4)*3*SCPITCH*4+8))))
		return(-3);
	radar = (int *)((((size_t)radarmem)+7)&~7);

	for(i=0;i<32;i++) { xbsflor[i] = (-1<<i); xbsceil[i] = ~xbsflor[i]; }

		//Setsphere precalculations (factr[] tables) (Derivation in POWCALC.BAS)
		//   if (!factr[z][0]) z's prime else factr[z][0]*factr[z][1] == z
	factr[2][0] = 0; i = 1; j = 9; k = 0;
	for(z=3;z<SETSPHMAXRAD;z+=2)
	{
		if (z == j) { j += (i<<2)+12; i += 2; }
		factr[z][0] = 0; factr[k][1] = z;
		for(zz=3;zz<=i;zz=factr[zz][1])
			if (!(z%zz)) { factr[z][0] = zz; factr[z][1] = z/zz; break; }
		if (!factr[z][0]) k = z;
		if(z<SETSPHMAXRAD){
			factr[z+1][0] = ((z+1)>>1);
			factr[z+1][1] = 2;
		}
	}
	for(z=1;z<SETSPHMAXRAD;z++) logint[z] = log((double)z);

#if (ESTNORMRAD == 2)
		//LUT for ESTNORM
	fsqrecip[0] = 0.f; fsqrecip[1] = 1.f;
	fsqrecip[2] = (float)(1.f/sqrt(2.f)); fsqrecip[3] = (float)1.f/sqrt(3.f);
	for(z=4,i=3;z<sizeof(fsqrecip)/sizeof(fsqrecip[0]);z+=6) //fsqrecip[z] = 1/sqrt(z);
	{
		fsqrecip[z+0] = fsqrecip[(z+0)>>1]*fsqrecip[2];
		fsqrecip[z+2] = fsqrecip[(z+2)>>1]*fsqrecip[2];
		fsqrecip[z+4] = fsqrecip[(z+4)>>1]*fsqrecip[2];
		fsqrecip[z+5] = fsqrecip[i]*fsqrecip[3]; i += 2;

		f = (fsqrecip[z+0]+fsqrecip[z+2])*.5f;
		if (z <= 22) f = (1.5f-(.5f*((float)(z+1))) * f*f)*f;
		fsqrecip[z+1] = (1.5f-(.5f*((float)(z+1))) * f*f)*f;

		f = (fsqrecip[z+2]+fsqrecip[z+4])*.5f;
		if (z <= 22) f = (1.5f-(.5f*((float)(z+3))) * f*f)*f;
		fsqrecip[z+3] = (1.5f-(.5f*((float)(z+3))) * f*f)*f;
	}
#endif

		//Lookup table to save 1 divide for gline()
	for(i=1;i<CMPRECIPSIZ;i++) cmprecip[i] = CMPPREC/(float)i;

		//Flashscan equal-angle compare table
	for(i=0;i<(1<<LOGFLASHVANG)*8;i++)
	{
		if (!(i&((1<<LOGFLASHVANG)-1)))
			j = (gfclookup[i>>LOGFLASHVANG]<<4)+8 - (1<<LOGFLASHVANG)*64;
		gfc[i].y = j; j += 64*2;
		ftol(sqrt((1<<(LOGFLASHVANG<<1))*64.f*64.f-gfc[i].y*gfc[i].y),&gfc[i].x);
	}

		//Init norm flash variables:
	ff = (float)GSIZ*.5f; // /(1);
	for(z=1;z<(GSIZ>>1);z++)
	{
		ffxptr = &ffx[(z+1)*z-1];
		f = ff; ff = (float)GSIZ*.5f/((float)z+1);
		for(zz=-z;zz<=z;zz++)
		{
			if (zz <= 0) i = (int)(((float)zz-.5f)*f); else i = (int)(((float)zz-.5f)*ff);
			if (zz >= 0) j = (int)(((float)zz+.5f)*f); else j = (int)(((float)zz+.5f)*ff);
			ffxptr[zz].x = (unsigned short)MAX(i+(GSIZ>>1),0);
			ffxptr[zz].y = (unsigned short)MIN(j+(GSIZ>>1),GSIZ);
		}
	}
	for(i=0;i<=25*5;i+=5) xbsbuf[i] = 0x00000000ffffffff;
	for(z=0;z<32;z++) { p2c[z] = (1<<z); p2m[z] = p2c[z]-1; }

		//Drawtile lookup table:
	//q = 0;
	//for(i=0;i<256;i++) { alphalookup[i] = q; q += 0x1000100010; }

		//Initialize univec normals (for KV6 lighting)
	equivecinit(255);
	//for(i=0;i<255;i++)
	//{
	//   univec[i].z = ((float)((i<<1)-254))/255.0;
	//   f = sqrt(1.0 - univec[i].z*univec[i].z);
	//   fcossin((float)i*(GOLDRAT*PI*2),&univec[i].x,&univec[i].y);
	//   univec[i].x *= f; univec[i].y *= f;
	//}
	//univec[255].x = univec[255].y = univec[255].z = 0;
	for(i=0;i<256;i++)
	{
		iunivec[i][0] = (short)(univec[i].x*4096.0);
		iunivec[i][1] = (short)(univec[i].y*4096.0);
		iunivec[i][2] = (short)(univec[i].z*4096.0);
		iunivec[i][3] = 4096;
	}
	ucossininit();

	memset(mixn,0,sizeof(mixn));

		//Initialize hash table for getkv6()
	memset(khashead,-1,sizeof(khashead));
	if (!(khashbuf = (char *)malloc(KHASHINITSIZE))) return(-1);
	khashsiz = KHASHINITSIZE;

	vx5.anginc = 1; //Higher=faster (1:full,2:half)
	vx5.sideshademode = 0; setsideshades(0,0,0,0,0,0);
	vx5.mipscandist = 128;
	vx5.maxscandist = 256; //must be <= 1023
	vx5.colfunc = curcolfunc; //This prevents omission bugs from crashing voxlap5
	vx5.curcol = 0x80804c33;
	vx5.currad = 8;
	vx5.curhei = 0;
	vx5.curpow = 2.0;
	vx5.amount = 0x70707;
	vx5.pic = 0;
	vx5.cliphitnum = 0;
	vx5.xplanemin = 0;
	vx5.xplanemax = 0x7fffffff;
	vx5.flstnum = 0;
	vx5.lightmode = 0;
	vx5.numlights = 0;
	vx5.kv6mipfactor = 96;
	vx5.kv6col = 0x808080;
	vx5.vxlmipuse = 1;
	vx5.fogcol = -1;
	vx5.fallcheck = 0;

	gmipnum = 0;
	return(0);
}

#if 0 //ndef _WIN32
	int i, j, k, l;
	char *v;

	j = k = l = 0;
	for(i=0;i<VSID*VSID;i++)
	{
		for(v=sptr[i];v[0];v+=v[0]*4) { j++; k += v[2]-v[1]+1; l += v[0]-1; }
		k += v[2]-v[1]+1; l += v[2]-v[1]+1;
	}

	printf("VOXLAP5 programmed by Ken Silverman (www.advsys.net/ken)\n");
	printf("Please DO NOT DISTRIBUTE! If this leaks, I will not be happy.\n\n");
	//printf("This copy licensed to:  \n\n");
	printf("Memory statistics upon exit: (all numbers in bytes)");
	printf("\n");
	if (screen) printf("   screen: %8ld\n",imageSize);
	printf("    radar: %8ld\n",MAX((((MAXXDIM*MAXYDIM*27)>>1)+7)&~7,(VSID+4)*3*SCPITCH*4+8));
	printf("  bacsptr: %8ld\n",sizeof(bacsptr));
	printf("     sptr: %8ld\n",(VSID*VSID)<<2);
	printf("     vbuf: %8ld(%8ld)\n",(j+VSID*VSID+l)<<2,VOXSIZ);
	printf("     vbit: %8ld(%8ld)\n",VOXSIZ>>5);
	printf("\n");
	printf("vbuf head: %8ld\n",(j+VSID*VSID)<<2);
	printf("vbuf cols: %8ld\n",l<<2);
	printf("     fcol: %8ld\n",k<<2);
	printf("     ccol: %8ld\n",(l-k)<<2);
	printf("\n");
	printf("%.2f bytes/column\n",(float)((j+VSID*VSID+l)<<2)/(float)(VSID*VSID));
#endif

/*Now some stuff that is used, but somehow not defined*/
void evilquit(char const* message){
	printf("[VOXLAP]ERROR: %s\n", message);
	fflush(stdout);
	uninitvoxlap();
	exit(-1);
}

//Bit numbers of return value:
//0:FPU, 4:RDTSC, 15:CMOV, 22:MMX+, 23:MMX, 25:SSE, 26:SSE2, 30:3DNow!+, 31:3DNow!
static int getcputype(){
	//return 0|4|15|23|25|26|31; //Don't feel like trying to code complex stuff now, just emulating
	return 255;
}

int cputype=255;

void drawboundcubesse(kv6voxtype *voxtype, int face){
	return;
}

void drawboundcubesseinit(){
}

void drawboundcube3dninit(){
	drawboundcubesseinit();
	return;
}

void drawboundcube3dn(kv6voxtype *voxtype, int var){
	drawboundcubesse(voxtype,var);
	return;
}

/*Another version of project2D, because the original one seems not to work as it should.
If the return value < 0, it's invisible, else it's the distance*/
float Vox_Project2D (float x, float y, float z, int *screenx, int *screeny){
	float dx, dy, dz, dist;
	dx=x-gipos.x; dy=y-gipos.y; dz=z-gipos.z;
	dist=dx*gifor.x+dy*gifor.y+dz*gifor.z; if(dist<.000001) return -1.f;

	x=(dx*gistr.x+dy*gistr.y+dz*gistr.z)*gihz;
	y=(dx*gihei.x+dy*gihei.y+dz*gihei.z)*gihz;

	ftol(x/dist+gihx-.5f,screenx);// if(*screenx>=xres_voxlap) return -1.f;
	ftol(y/dist+gihy-.5f,screeny);// if(*screeny>=yres_voxlap) return -1.f;
	return dist;
	
}

#define Vox_CalcLen(x, y, z) sqrt((x)*(x)+(y)*(y)+(z)*(z))


Vox_VX5Sprite *Vox_InitSprite(Vox_VX5Sprite *sprite){
	sprite->x=0; sprite->y=0; sprite->z=0; sprite->rst=0; sprite->rhe=0;
	sprite->rti=0; sprite->colorkey=0; sprite->color=0; sprite->density=8.0;
	sprite->zbufdist=0.0; sprite->xscreenoffset=0; sprite->yscreenoffset=0;
	sprite->xpivoffset=0.0; sprite->ypivoffset=0.0; sprite->zpivoffset=0.0;
	sprite->lx=0.0; sprite->ly=0.0; sprite->lz=0.0;
	sprite->ldx=0.0; sprite->ldy=0.0; sprite->ldz=0.0;
	return sprite;
}

float Vox_CalcDist(float x, float y, float z, float *voxdist){
	float dx, dy, dz;
	dx=x-gipos.x; dy=y-gipos.y; dz=z-gipos.z;
	if(voxdist)
		*voxdist=dx*gifor.x+dy*gifor.y+dz*gifor.z;
	return sqrt(dx*dx+dy*dy+dz*dz);
}

#define Vox_CalcLen(x, y, z) sqrt((x)*(x)+(y)*(y)+(z)*(z))

/*__FORCE_INLINE__ float Vox_CalcLen(float x, float y, float z){
	return sqrt(x*x+y*y+z*z);
}*/

float Vox_SqrLen(float x, float y, float z){return x*x+y*y+z*z;}

__FORCE_INLINE__ float _Vox_KV6Project2D(float x, float y, float z, int *screenx, int *screeny){
	float dx=x-gipos.x, dy=y-gipos.y, dz=z-gipos.z, dist;
	dist=(dx*gifor.x+dy*gifor.y+dz*gifor.z)/gihz; if(dist<=0) return -1.f;
	*screenx=(dx*gistr.x+dy*gistr.y+dz*gistr.z)/dist+gihx-.5f; if(*screenx>=xres_voxlap) return -1.f;
	*screeny=(dx*gihei.x+dy*gihei.y+dz*gihei.z)/dist+gihy-.5f; if(*screeny>=yres_voxlap) return -1.f;
	return Vox_CalcLen(dx, dy, dz);
}

__FORCE_INLINE__ void  _Calculate_Fog(__REGISTER unsigned char *color, float *dist){
	const float invpwr_maxscandist=1.0/(vx5.maxscandist*vx5.maxscandist/255.0);
	__REGISTER unsigned int fog_alpha=255-*dist**dist*invpwr_maxscandist;
	__REGISTER unsigned char *fogptr=(unsigned char*)&vx5.fogcol;
	color[0]=((unsigned int)fogptr[0])+(((((unsigned int)color[0])-fogptr[0])*fog_alpha)>>8);
	color[1]=((unsigned int)fogptr[1])+(((((unsigned int)color[1])-fogptr[1])*fog_alpha)>>8);
	color[2]=((unsigned int)fogptr[2])+(((((unsigned int)color[2])-fogptr[2])*fog_alpha)>>8);
	color[3]=255;
	return;
}

VOXLAP_DLL_FUNC void Vox_Calculate_2DFog(unsigned char *color, float xdist, float ydist){
	const float invpwr_maxscandist=1.0/(vx5.maxscandist*vx5.maxscandist/255.0);
	__REGISTER float xydist=xdist*xdist+ydist*ydist;
	__REGISTER unsigned int fog_alpha=xydist*invpwr_maxscandist;
	fog_alpha-=(fog_alpha-255)*(fog_alpha>255);
	fog_alpha=255-fog_alpha;
	__REGISTER unsigned char *fogptr=(unsigned char*)&vx5.fogcol;
	color[0]=((unsigned int)fogptr[0])+(((((unsigned int)color[0])-fogptr[0])*fog_alpha)>>8);
	color[1]=((unsigned int)fogptr[1])+(((((unsigned int)color[1])-fogptr[1])*fog_alpha)>>8);
	color[2]=((unsigned int)fogptr[2])+(((((unsigned int)color[2])-fogptr[2])*fog_alpha)>>8);
	//color[3]=((unsigned int)fogptr[3])+(((((unsigned int)color[3])-fogptr[3])*fog_alpha)>>8);
	color[3]=255;
	return;
}

VOXLAP_DLL_FUNC void Calculate_Fog(__REGISTER unsigned char *color, float *dist){
	return _Calculate_Fog(color, dist);
}

__FORCE_INLINE__ int _Vox_DrawRect2D(int sx, int sy, unsigned int w, unsigned int h, 
unsigned int color, __REGISTER float dist){
			__REGISTER unsigned int x, *pty, y, draw_pixel;
		__REGISTER float distdiff, *zbufptr;
		if(sx>xres_voxlap || sy>yres_voxlap)
			return -1;
		if(dist>vx5.maxscandist)
			return 1;
		if(sx<0){ if(w<-sx) return -1; w+=sx; sx=0;}
		if(sy<0){ if(h<-sy) return -1; h+=sy; sy=0; }
		if(sx==xres_voxlap || sy==yres_voxlap)
			return 0;
		w=MIN(xres_voxlap-sx, w); h=MIN(yres_voxlap-sy, h);
		sx<<=2;
		for(y=sy;y<sy+h;++y){
			pty=(unsigned int*)(ylookup[y]+sx+frameplace);
			zbufptr=(float*)(((unsigned char*)pty)+zbufoff);
			for(x=0; x<w; ++x){
				distdiff=dist-zbufptr[x];
				draw_pixel=distdiff<0.0;
				zbufptr[x]+=distdiff*draw_pixel;
				pty[x]+=(color-pty[x])*draw_pixel;
			}
		}
		return 0;
}

VOXLAP_DLL_FUNC int Vox_DrawRect2D(int sx, int sy, unsigned int w, unsigned int h, 
unsigned int color, float dist){
	_Calculate_Fog((unsigned char*)&color, &dist);
	return _Vox_DrawRect2D(sx, sy, w, h, color, dist);
}

void Vox_Rotate3D(float *xp, float *yp, float *zp, float z_angle, float x_angle, float y_angle){
	float x=*xp, y=*yp, z=*zp;
	static float sz, cz, sx, cx, sy, cy;
	static unsigned int old_z_angle, old_x_angle, old_y_angle;
	if(old_z_angle!=z_angle+1){
		old_z_angle=z_angle+1;
		sz=sin(z_angle*M_PI/180.f); cz=cos(z_angle*M_PI/180.f);
	}
	if(old_x_angle!=x_angle+1){
		old_x_angle=x_angle+1;
		sx=sin(x_angle*M_PI/180.f); cx=cos(x_angle*M_PI/180.f);
	}
	if(old_y_angle!=y_angle+1){
		old_y_angle=y_angle+1;
		sy=sin(y_angle*M_PI/180.f); cy=cos(y_angle*M_PI/180.f);
	}
	/*X axis rotation*/
	*yp=y*cx - z*sx; *zp=y*sx + z*cx;
	x=*xp, y=*yp, z=*zp;
	/*Y axis rotation*/
	*zp=z*cy - x*sy; *xp=z*sy + x*cy;
	x=*xp, y=*yp, z=*zp;
	/*Z axis rotation*/
	*xp=x*cz - y*sz; *yp=x*sz + y*cz;
	x=*xp, y=*yp, z=*zp;
}

/*Main KV6 rendering function*/
/*The current algorithm uses the Z buffer for box-box overlapping (no actual ray casting).
The original version seems to use kv6voxtype.vis and a special rendering order for overdrawing boxes.
For int, it would be a good idea to implement Ken's algorithm instead of the current one.
All possible experiments can be done here, since even this slow and unoptimized algorithm doesn't 
consume any actual time at all! The only possible botteleneck is Vox_DrawRect2D*/
int Vox_RayCastKV6(Vox_VX5Sprite *sprite){
	kv6data *kv=sprite->vox;
	if(!kv)
		return -1;
	float xpiv=kv->xpiv+sprite->xpivoffset, ypiv=kv->ypiv+sprite->ypivoffset, zpiv=kv->zpiv+sprite->zpivoffset;
	float sx=sprite->x-xpiv, sy=sprite->y-ypiv, sz=sprite->z-zpiv, spritedist=Vox_CalcDist(sx, sy, sz, NULL);
	if(spritedist>vx5.maxscandist+Vox_CalcLen(kv->xsiz, kv->ysiz, kv->zsiz))
		return 1;
	signed int x,y;
	unsigned int hxsiz=kv->xsiz/2, hysiz=kv->ysiz/2, hzsiz=kv->zsiz/2, color, light, rlight=255-__KV6_MLIGHT__;
	float fnx, fny, fnz, snx, sny, snz, dist, voxl_dist, vxdist, spritelen, lightdist;
	float dxpv=pow(__PIXELS_PER_KVVXL__,3)*gihz*2/800/sprite->density;	int sppx, sppy, kvw, kvh;
	
	float rot_sx, rot_cx, rot_sy, rot_cy, rot_sz, rot_cz, rot_x, rot_y, rot_z;
	
	unsigned int rfog=vx5.fogcol&255, gfog=(vx5.fogcol>>8)&255, bfog=(vx5.fogcol>>16)&255, fog_alpha;
	kv6voxtype  *sblk, *blk, *eblk;
	
	rot_sx=sin(sprite->rhe*M_PI/180.f); rot_cx=cos(sprite->rhe*M_PI/180.f);
	rot_sy=sin(sprite->rti*M_PI/180.f); rot_cy=cos(sprite->rti*M_PI/180.f);
	rot_sz=sin(sprite->rst*M_PI/180.f); rot_cz=cos(sprite->rst*M_PI/180.f);

	fnx=sx; fny=sy; fnz=sz;
	fnx-=sprite->lx; fny-=sprite->ly; fnz-=sprite->lz;
	lightdist=sqrt(fnx*fnx+fny*fny+fnz*fnz)/sprite->density;
	lightdist+=!lightdist;
	fnx=kv->xsiz; fny=kv->ysiz; fnz=kv->zsiz;
	spritelen=sqrt(fnx*fnx+fny*fny+fnz*fnz);
	if(!(sprite->ldx || sprite->ldy || sprite->ldz)){
		float len;
		sprite->ldx=sprite->x-sprite->lx;
		sprite->ldy=sprite->y-sprite->ly;
		sprite->ldz=sprite->z-sprite->lz;
		len=Vox_CalcLen(sprite->ldx, sprite->ldy, sprite->ldz);
		sprite->ldx/=len; sprite->ldy/=len; sprite->ldz/=len;
	}
	if(!sprite->density)
		return 2;
	for(x=0;x<kv->xsiz;++x){
		for(y=0;y<kv->ysiz;++y){
			sblk=getvptr(kv,x,y);
			eblk=&sblk[kv->ylen[x*kv->ysiz+y]];
			for(blk=sblk;blk<eblk;++blk){

				fnx=x-xpiv; fny=y-ypiv; fnz=blk->z-zpiv;
				
				/*Does the same as Vox_Rotate3D, but this is a bit faster*/
				rot_y=fny; rot_z=fnz;
				fny=rot_y*rot_cx - rot_z*rot_sx; fnz=rot_y*rot_sx + rot_z*rot_cx;
				rot_x=fnx; rot_z=fnz;
				fnz=rot_z*rot_cy - rot_x*rot_sy; fnx=rot_z*rot_sy + rot_x*rot_cy;
				rot_x=fnx; rot_y=fny;
				fnx=rot_x*rot_cz - rot_y*rot_sz; fny=rot_x*rot_sz + rot_y*rot_cz;
			
				fnx=fnx/sprite->density+xpiv+sx; fny=fny/sprite->density+ypiv+sy; fnz=fnz/sprite->density+zpiv+sz;
				
				dist=_Vox_KV6Project2D(fnx, fny, fnz, &sppx, &sppy);
				if(dist==-1 || dist>vx5.maxscandist)
					continue;
				color=blk->col;
				if((color&0x00ffffff)==sprite->colorkey)
					color=sprite->color;
				snx=fnx-sprite->lx; sny=fny-sprite->ly; snz=fnz-sprite->lz;
				lightdist=Vox_CalcLen(snx, sny, snz); lightdist+=!lightdist;
				light=Vox_CalcLen(snx/lightdist-sprite->ldx, sny/lightdist-sprite->ldy, snz/lightdist-sprite->ldz)
				*__KV6_SHADE_FACTOR__*__KV6_FSHADE__;
				if(light>rlight)
					light=rlight;
				light=rlight-light+__KV6_MLIGHT__;
				color=__KVDARKEN__(color, light);

				_Calculate_Fog((unsigned char*)&color, &dist);

				sppx+=sprite->xscreenoffset; sppy+=sprite->yscreenoffset;
				
				/*Prevents gaps when not directly looking at the sprite*/
				vxdist=dist;
				dist=((fnx-gipos.x)*gifor.x+(fny-gipos.y)*gifor.y+(fnz-gipos.z)*gifor.z);

				kvw=dxpv/dist+1.0; kvh=dxpv/dist+1.0;
				kvw+=!kvw; kvh+=!kvh;
				
				if(sprite->zbufdist>0.0)
					vxdist-=sprite->zbufdist;
					

				_Vox_DrawRect2D((int)sppx, (int)sppy, kvw, kvh, color, vxdist);
			}
		}
	}
	return 0;
}

/*
Converts normal degress into Voxlap's weird Euclid coordinate system
xdeg, ydeg=degrees
tilt=0.0-1.0
xv, yv, zv=pointers to destination vectors
*/

void Vox_ConvertToEucl(float xdeg, float ydeg, float tilt, dpoint3d *xv, dpoint3d *yv, dpoint3d *zv){
	xv->x=0; xv->y=0; xv->z=0;
	yv->x=0; yv->y=0; yv->z=0;
	zv->x=0; zv->y=0; zv->z=0;
	xv->x=1; yv->z=1; zv->y=-1;
	dorthorotate(tilt, ydeg*M_PI/180.0, xdeg*M_PI/180.0, xv, yv, zv);
	dorthonormalize(xv, yv, zv);
}

/*Return value: -32=something is broken, 0=false, -1=inside bounding box, but didn't hit any actual KV6, 1=hit a KV6 voxel*/
/*Ignores screen+pivoffsets !*/
int Vox_SpriteHitScan(Vox_VX5Sprite *spr, point3d *pos, point3d *dir, point3d *voxpos, kv6voxtype **voxptr){
	if(!dir->x && !dir->y && !dir->z)
		return -32;
	unsigned int x, y;
	point3d dirdist, vdist;
	kv6voxtype *v, *ve;
	float xpiv=spr->vox->xpiv, ypiv=spr->vox->ypiv, zpiv=spr->vox->zpiv;
	float sx=spr->x-xpiv, sy=spr->y-ypiv, sz=spr->z-zpiv;
	float fnx, fny, fnz;
	float vxdist, vxdirdist;
	float max_kvsize=1.0/spr->density*gihz*2/800;

	for(x=0;x<spr->vox->xsiz;++x){
		for(y=0;y<spr->vox->ysiz;++y){
			v=getvptr(spr->vox,x,y);
			ve=&v[spr->vox->ylen[x*spr->vox->ysiz+y]];
			for(;v<ve;++v){
				fnx=x-xpiv; fny=y-ypiv; fnz=v->z-zpiv;
				Vox_Rotate3D(&fnx, &fny, &fnz, spr->rst, spr->rhe, spr->rti);
				fnx=fnx/spr->density+xpiv+sx;
				fny=fny/spr->density+ypiv+sy;
				fnz=fnz/spr->density+zpiv+sz;
				vdist.x=fnx-pos->x; vdist.y=fny-pos->y; vdist.z=fnz-pos->z;
				vxdist=sqrt(vdist.x*vdist.x+vdist.y*vdist.y+vdist.z*vdist.z);
				vdist.x/=vxdist; vdist.y/=vxdist; vdist.z/=vxdist;
				dirdist.x=vdist.x-dir->x; dirdist.y=vdist.y-dir->y; dirdist.z=vdist.z-dir->z;
				vxdirdist=Vox_CalcLen(dirdist.x, dirdist.y, dirdist.z);
				if(vxdirdist<max_kvsize/vxdist){
					if(voxpos){
						voxpos->x=fnx; voxpos->y=fny; voxpos->z=fnz;
					}
					if(voxptr)
						*voxptr=v;
					return 1;
				}
			}
		}
	}
	return 0;
}
