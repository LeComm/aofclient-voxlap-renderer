/* VOXLAP engine by Ken Silverman (http://advsys.net/ken)*/
// This file has been modified from Ken Silverman's original release

/***********************************************************************************************
Edited by lecom
***********************************************************************************************/

#ifndef __VOXLAP5_H__
#define __VOXLAP5_H__ 1

#ifndef VOXLAP_DLL_FUNC
#define VOXLAP_DLL_FUNC
#endif
#include <stddef.h>

/* For SSE2 byte-vectors*/
#ifdef _MSC_VER
#include <xmmintrin.h>
#endif

/*Max screen resolution*/
#define MAXXDIM 1920
#define MAXYDIM 1080
#define PI 3.141592653589793
#define VSID 512   /*Maximum .VXL dimensions in both x & y direction*/
#define MAXZDIM 64 /*Maximum .VXL dimensions in z direction (height)*/

#pragma pack(push,1)

/*#if !defined(VOXLAP_C) && !defined(__cplusplus) && !defined(VOXLAP5)
#error "Cannot link C frontend to C++ Backend"
#endif*/

#ifdef __x86_64__
#define __64BIT_SYSTEM__ 1
#else
#define __64BIT_SYSTEM__ 0
#endif

#ifdef __clang__
#if (__clang__==1)
#define __CLANG_GLOBALVAR__ volatile
#endif
#endif

#ifndef __CLANG_GLOBALVAR__
#define __CLANG_GLOBALVAR__ static
#endif

#if defined(VOXLAP_C) && defined(__cplusplus)
	extern "C" {
	#define EXTERN_VOXLAP extern "C"
#else
	#define EXTERN_VOXLAP extern
#endif

/* 3 dimensional points don't have power of 2 vector*/
typedef struct { int x, y, z; } lpoint3d;
typedef struct { float x, y, z; } point3d;
typedef struct { double x, y, z; } dpoint3d;

enum Voxlap_LightModes{
	Vox_LightMode_None=0, Vox_LightMode_Standart=1, Vox_LightMode_LightSources=2, Vox_LightMode_VoxelShades=4
};

typedef union
{
	struct { unsigned short x, y; };
	short array[2];
	#ifdef __GNUC__
	short vec __attribute__ ((vector_size (4)));
	#endif
	#ifdef _MSC_VER
	__ALIGN(16) __m32 vec;
	#endif
} uspoint2d;
typedef union
{
	struct { int x, y; };
	int array[2];
	#ifdef __GNUC__
	int vec __attribute__ ((vector_size (8)));
	#endif
	#ifdef _MSC_VER
	__ALIGN(16) __m64 vec;
	#endif
} lpoint2d;
typedef union
{
	struct { float x, y; };
	float array[2];
	#ifdef __GNUC__
	float vec __attribute__ ((vector_size (8)));
	#endif
	#ifdef _MSC_VER
	__ALIGN(16) __m64 vec;
	#endif
} point2d;
typedef union
{
	struct { float x, y, z, z2; };
	float array[4];
	#ifdef __GNUC__
	float vec __attribute__ ((vector_size (16)));
	//float svec[2] __attribute__ ((vector_size (8)));
	#endif
	#ifdef _MSC_VER
	__ALIGN(16) __m128 vec;
	__ALIGN(16) __m64 svec[2];
	#endif
} point4d;

	/*Sprite structures:*/
typedef struct { int col; unsigned short z; char vis, dir; } kv6voxtype;

typedef struct kv6data
{
	int leng, xsiz, ysiz, zsiz;
	float xpiv, ypiv, zpiv;
	unsigned int numvoxs;
	int namoff;
	struct kv6data *lowermip;
	kv6voxtype *vox;      /*numvoxs*sizeof(kv6voxtype)*/
	unsigned int *xlen;  /*xsiz*sizeof(int)*/
	unsigned short *ylen; /*xsiz*ysiz*sizeof(short)*/
} kv6data;

typedef struct
{
	int parent;      /*index to parent sprite (-1=none)*/
	point3d p[2];     /*"velcro" point of each object*/
	point3d v[2];     /*axis of rotation for each object*/
	short vmin, vmax; /*min value / max value*/
	char htype, filler[7];
} hingetype;

typedef struct { int tim, frm; } seqtyp;

typedef struct
{
	int numspr, numhin, numfrm, seqnum;
	int namoff;
	kv6data *basekv6;      /*Points to original unconnected KV6 (maybe helpful?)*/
	struct vx5sprite *spr; /*[numspr]*/
	hingetype *hinge;      /*[numhin]*/
	int *hingesort;       /*[numhin]*/
	short *frmval;         /*[numfrm][numhin]*/
	seqtyp *seq;           /*[seqnum]*/
} kfatype;

	/*Notice that I aligned each point3d on a 16-byte boundary. This will be*/
	/*   helpful when I get around to implementing SSE instructions someday...*/
typedef struct vx5sprite
{
	point3d p; /*position in VXL coordinates*/
	int flags; /*flags bit 0:0=use normal shading, 1=disable normal shading*/
					/*flags bit 1:0=points to kv6data, 1=points to kfatype*/
					/*flags bit 2:0=normal, 1=invisible sprite*/
	union { point3d s, x; }; /*kv6data.xsiz direction in VXL coordinates*/
	union
	{
		kv6data *voxnum; /*pointer to KV6 voxel data (bit 1 of flags = 0)*/
		kfatype *kfaptr; /*pointer to KFA animation  (bit 1 of flags = 1)*/
	};
	union { point3d h, y; }; /*kv6data.ysiz direction in VXL coordinates*/
	int kfatim;        /*time (in milliseconds) of KFA animation*/
	union { point3d f, z; }; /*kv6data.zsiz direction in VXL coordinates*/
	int okfatim;       /*make vx5sprite exactly 64 bytes :)*/
} vx5sprite;

typedef enum{
	VOXLAP_CPU_POS_NONE=0, VOXLAP_CPU_POS_OPTICAST=1, VOXLAP_CPU_POS_GLINE=2, VOXLAP_CPU_POS_HREND=3,
	VOXLAP_CPU_POS_VREND=4, VOXLAP_CPU_POS_KV6REND=5 
}Voxlap_CPU_Pos;

/*Replacement for the vx5sprite struct*/
/*The original Voxlap sprite code doesn't support specific things*/
/*Especially the density(="resolution") plays a role*/
typedef struct Vox_VX5Sprite{
	float x, y, z;
	float lx, ly, lz; /*Light source position in global voxel coords*/
	float ldx, ldy, ldz; /*Light source direction (normalized vector)*/
	float brightness;
	/*rst=x and y , around z axis; rhe=(x+y) and z , up/down; rti=tilting*/
	float rst, rhe, rti;
	/*colorkey is replaced with color when rendering and if !(colorkey&0xff000000)*/
	unsigned int colorkey, color;
	/*offsets are mainly for first person rendering, like player holding something*/
	signed int xscreenoffset, yscreenoffset;
	float xpivoffset, ypivoffset, zpivoffset;
	/*zbufdist is only partly implemented, ignore it. Density is the "resolution"*/
	float density, zbufdist;
	kv6data *vox;
}Vox_VX5Sprite;

	/*Falling voxels shared data: (flst = float list)*/
#define FLPIECES 256 /*Max # of separate falling pieces*/
typedef struct /*(68 bytes)*/
{
	lpoint3d chk; /*a solid point on piece (x,y,pointer) (don't touch!)*/
	int i0, i1; /*indices to start&end of slab list (don't touch!)*/
	int x0, y0, z0, x1, y1, z1; /*bounding box, written by startfalls*/
	int mass; /*mass of piece, written by startfalls (1 unit per voxel)*/
	point3d centroid; /*centroid of piece, written by startfalls*/

		/*userval is set to -1 when a new piece is spawned. Voxlap does not*/
		/*read or write these values after that point. You should use these to*/
		/*play an initial sound and track velocity*/
	int userval, userval2;
} flstboxtype;

	/*Lighting variables: (used by updatelighting)*/
#define MAXLIGHTS 256
typedef struct { point3d p; float r2, sc; char Light_KV6;} lightsrctype;

	/*Used by setspans/meltspans. Ordered this way to allow sorting as longs!*/
typedef struct { char z1, z0, x, y; } vspans;

#pragma pack(pop)

#define MAXFRM 1024 /*MUST be even number for alignment!*/

	/*Voxlap5 shared global variables:*/
struct vx5_interface
{
	/*------------------------ DATA coming from VOXLAP5 ------------------------*/

		/*Clipmove hit point info (use this after calling clipmove):*/
	double clipmaxcr; /*clipmove always calls findmaxcr even with no movement*/
	dpoint3d cliphit[3];
	int cliphitnum;

		/*Bounding box written by last set* VXL writing call*/
	int minx, miny, minz, maxx, maxy, maxz;

		/*Falling voxels shared data:*/
	int flstnum;
	flstboxtype flstcnt[FLPIECES];

		/*Total count of solid voxels in .VXL map (included unexposed voxels)*/
	int globalmass;

		/*Temp workspace for KFA animation (hinge angles)*/
		/*Animsprite writes these values&you may modify them before drawsprite*/
	short kfaval[MAXFRM];

	/*------------------------ DATA provided to VOXLAP5 ------------------------*/

		/*Opticast variables:*/
	float anginc;
	int sideshademode, mipscandist, maxscandist, vxlmipuse, fogcol;

		/*Drawsprite variables:*/
	int kv6mipfactor, kv6col;
		/*Drawsprite x-plane clipping (reset to 0,(high int) after use!)*/
		/*For example min=8,max=12 permits only planes 8,9,10,11 to draw*/
	int xplanemin, xplanemax;
	
	/*Stuff added by lecom*/
	/*0.0 - 1.0*/
	float KV6_Darkness;
	signed int zbufoff;
	unsigned int kv6_anginc;
	unsigned int CPU_Pos;

		/*Map modification function data:*/
	int curcol, currad, curhei;
	float curpow;

		/*Procedural texture function data:*/
	int (*colfunc)(lpoint3d *);
	int cen, amount, *pic, bpl, xsiz, ysiz, xoru, xorv, picmode;
	point3d fpico, fpicu, fpicv, fpicw;
	lpoint3d pico, picu, picv;
	float daf;

		/*Lighting variables: (used by updatelighting)*/
	int lightmode; /*0 (default), 1:simple lighting, 2:lightsrc lighting*/
	lightsrctype lightsrc[MAXLIGHTS]; /*(?,?,?),128*128,262144*/
	int numlights;

	int fallcheck;
	
};

#ifdef VOXLAP5
static VOXLAP_DLL_FUNC struct vx5_interface vx5;
#else
extern VOXLAP_DLL_FUNC struct  vx5_interface vx5;
#endif

	/*Initialization functions:*/
extern VOXLAP_DLL_FUNC int initvoxlap ();
extern VOXLAP_DLL_FUNC void uninitvoxlap ();

	/*File related functions:*/
extern VOXLAP_DLL_FUNC int loadsxl (const char *, char **, char **, char **);
extern VOXLAP_DLL_FUNC char *parspr (vx5sprite *, char **);
extern VOXLAP_DLL_FUNC void loadnul (dpoint3d *, dpoint3d *, dpoint3d *, dpoint3d *);
extern VOXLAP_DLL_FUNC int loaddta (const char *, dpoint3d *, dpoint3d *, dpoint3d *, dpoint3d *);
extern VOXLAP_DLL_FUNC int loadpng (const char *, dpoint3d *, dpoint3d *, dpoint3d *, dpoint3d *);
extern VOXLAP_DLL_FUNC void loadbsp (const char *, dpoint3d *, dpoint3d *, dpoint3d *, dpoint3d *);
extern VOXLAP_DLL_FUNC int loadvxl (const char *);
extern VOXLAP_DLL_FUNC int savevxl (const char *, dpoint3d *, dpoint3d *, dpoint3d *, dpoint3d *);
extern VOXLAP_DLL_FUNC int loadsky (const char *);

	/*Screen related functions:*/
extern VOXLAP_DLL_FUNC void voxsetframebuffer (int*, int, int, int);
extern VOXLAP_DLL_FUNC void setsideshades (char, char, char, char, char, char);
extern VOXLAP_DLL_FUNC void setcamera (dpoint3d *, dpoint3d *, dpoint3d *, dpoint3d *, float, float, float);
extern VOXLAP_DLL_FUNC void opticast (unsigned int, unsigned int, unsigned int, unsigned int, void**);
extern VOXLAP_DLL_FUNC void drawpoint2d (int, int, int);
extern VOXLAP_DLL_FUNC void drawpoint3d (float, float, float, int);
extern VOXLAP_DLL_FUNC void drawline2d (float, float, float, float, int);
extern VOXLAP_DLL_FUNC void drawline3d (float, float, float, float, float, float, int);
extern VOXLAP_DLL_FUNC int project2d (float, float, float, float *, float *, float *);
extern VOXLAP_DLL_FUNC void drawspherefill (float, float, float, float, int);
extern VOXLAP_DLL_FUNC void drawpicinquad (int, int, int, int, int, int, int, int, float, float, float, float, float, float, float, float);
extern VOXLAP_DLL_FUNC void drawpolyquad (int, int, int, int, float, float, float, float, float, float, float, float, float, float, float, float, float, float, float, float, float, float);
extern VOXLAP_DLL_FUNC void print4x6 (int, int, int, int, const char *, ...);
extern VOXLAP_DLL_FUNC void print6x8 (int, int, int, int, const char *, ...);
extern VOXLAP_DLL_FUNC void drawtile (int, int, int, int, int, int, int, int, int, int, int, int);
extern VOXLAP_DLL_FUNC int screencapture32bit (const char *);
extern VOXLAP_DLL_FUNC int surroundcapture32bit (dpoint3d *, const char *, int);

	/*Sprite related functions:*/
extern VOXLAP_DLL_FUNC kv6data *getkv6 (const char *);
extern VOXLAP_DLL_FUNC kv6data *loadkv6 (const char *);
extern VOXLAP_DLL_FUNC kfatype *getkfa (const char *);
extern VOXLAP_DLL_FUNC void freekv6 (kv6data *kv6);
extern VOXLAP_DLL_FUNC void savekv6 (const char *, kv6data *);
extern VOXLAP_DLL_FUNC void getspr (vx5sprite *, const char *);
extern VOXLAP_DLL_FUNC kv6data *genmipkv6 (kv6data *);
extern VOXLAP_DLL_FUNC char *getkfilname (int);
extern VOXLAP_DLL_FUNC void animsprite (vx5sprite *, int);
extern VOXLAP_DLL_FUNC void drawsprite (vx5sprite *);
extern VOXLAP_DLL_FUNC int meltsphere (vx5sprite *, lpoint3d *, int);
extern VOXLAP_DLL_FUNC int meltspans (vx5sprite *, vspans *, int, lpoint3d *);

	/*Physics helper functions:*/
extern VOXLAP_DLL_FUNC void orthonormalize (point3d *, point3d *, point3d *);
extern VOXLAP_DLL_FUNC void dorthonormalize (dpoint3d *, dpoint3d *, dpoint3d *);
extern VOXLAP_DLL_FUNC void orthorotate (float, float, float, point3d *, point3d *, point3d *);
extern VOXLAP_DLL_FUNC void dorthorotate (double, double, double, dpoint3d *, dpoint3d *, dpoint3d *);
extern VOXLAP_DLL_FUNC void axisrotate (point3d *, point3d *, float);
extern VOXLAP_DLL_FUNC void slerp (point3d *, point3d *, point3d *, point3d *, point3d *, point3d *, point3d *, point3d *, point3d *, float);
extern VOXLAP_DLL_FUNC int cansee (point3d *, point3d *, lpoint3d *);
extern VOXLAP_DLL_FUNC void hitscan (dpoint3d *, dpoint3d *, lpoint3d *, int **, int *);
extern VOXLAP_DLL_FUNC void sprhitscan (dpoint3d *, dpoint3d *, vx5sprite *, lpoint3d *, kv6voxtype **, float *vsc);
extern VOXLAP_DLL_FUNC double findmaxcr (double, double, double, double);
extern VOXLAP_DLL_FUNC void clipmove (dpoint3d *, dpoint3d *, double);
extern VOXLAP_DLL_FUNC int triscan (point3d *, point3d *, point3d *, point3d *, lpoint3d *);
extern VOXLAP_DLL_FUNC void estnorm (int, int, int, point3d *);

	/*VXL reading functions (fast!):*/
extern VOXLAP_DLL_FUNC int isvoxelsolid (int, int, int);
extern VOXLAP_DLL_FUNC int anyvoxelsolid (int, int, int, int);
extern VOXLAP_DLL_FUNC int anyvoxelempty (int, int, int, int);
extern VOXLAP_DLL_FUNC int getfloorz (int, int, int);
extern VOXLAP_DLL_FUNC int getnextfloorz(int, int, int);
extern VOXLAP_DLL_FUNC int* getcube (int, int, int);

	/*VXL writing functions (optimized & bug-free):*/
extern VOXLAP_DLL_FUNC void setcube (int, int, int, int);
extern VOXLAP_DLL_FUNC void setsphere (lpoint3d *, int, int);
extern VOXLAP_DLL_FUNC void setellipsoid (lpoint3d *, lpoint3d *, int, int, int);
extern VOXLAP_DLL_FUNC void setcylinder (lpoint3d *, lpoint3d *, int, int, int);
extern VOXLAP_DLL_FUNC void setrect (lpoint3d *, lpoint3d *, int);
extern VOXLAP_DLL_FUNC void settri (point3d *, point3d *, point3d *, int);
extern VOXLAP_DLL_FUNC void setsector (point3d *, int *, int, float, int, int);
extern VOXLAP_DLL_FUNC void setspans (vspans *, int, lpoint3d *, int);
extern VOXLAP_DLL_FUNC void setheightmap (const unsigned char *, int, int, int, int, int, int, int);
extern VOXLAP_DLL_FUNC void setkv6 (vx5sprite *, int dacol);

	/*VXL writing functions (slow or buggy):*/
extern VOXLAP_DLL_FUNC void sethull3d (point3d *, int, int, int);
extern VOXLAP_DLL_FUNC void setlathe (point3d *, int, int, int);
extern VOXLAP_DLL_FUNC void setblobs (point3d *, int, int, int);
extern VOXLAP_DLL_FUNC void setfloodfill3d (int, int, int, int, int, int, int, int, int);
extern VOXLAP_DLL_FUNC void sethollowfill ();
extern VOXLAP_DLL_FUNC void setkvx (const char *, int, int, int, int, int);
extern VOXLAP_DLL_FUNC void setflash (float, float, float, int, int, int);
extern VOXLAP_DLL_FUNC void setnormflash (float, float, float, int, int);

	/*VXL MISC functions:*/
extern VOXLAP_DLL_FUNC void updatebbox (int, int, int, int, int, int, int);
extern VOXLAP_DLL_FUNC void updatevxl ();
extern VOXLAP_DLL_FUNC void genmipvxl (int, int, int, int);
extern VOXLAP_DLL_FUNC void updatelighting (int, int, int, int, int, int);

	/*Falling voxels functions:*/
extern VOXLAP_DLL_FUNC void checkfloatinbox (int, int, int, int, int, int);
extern VOXLAP_DLL_FUNC void startfalls ();
extern VOXLAP_DLL_FUNC void dofall (int);
extern VOXLAP_DLL_FUNC int meltfall (vx5sprite *, int, int);
extern VOXLAP_DLL_FUNC void finishfalls ();

	/*Procedural texture functions:*/
extern VOXLAP_DLL_FUNC int curcolfunc (lpoint3d *);
extern VOXLAP_DLL_FUNC int floorcolfunc (lpoint3d *);
extern VOXLAP_DLL_FUNC int jitcolfunc (lpoint3d *);
extern VOXLAP_DLL_FUNC int manycolfunc (lpoint3d *);
extern VOXLAP_DLL_FUNC int sphcolfunc (lpoint3d *);
extern VOXLAP_DLL_FUNC int woodcolfunc (lpoint3d *);
extern VOXLAP_DLL_FUNC int pngcolfunc (lpoint3d *);
extern VOXLAP_DLL_FUNC int kv6colfunc (lpoint3d *);

	/*Editing backup/restore functions*/
extern VOXLAP_DLL_FUNC void voxbackup (int, int, int, int, int);
extern VOXLAP_DLL_FUNC void voxdontrestore ();
extern VOXLAP_DLL_FUNC void voxrestore ();
extern VOXLAP_DLL_FUNC void voxredraw ();

	/*Functions that aren't in the old Voxlap*/
extern VOXLAP_DLL_FUNC int Vox_Lightning(float fxp, float fyp, float fzp, float radius, float intensity);
extern VOXLAP_DLL_FUNC void Vox_SetSideShades(unsigned char low_x, unsigned char low_y, 
unsigned char high_x, unsigned char high_y, unsigned char b, unsigned char t);
extern VOXLAP_DLL_FUNC float Vox_Project2D (float, float, float, int*, int*);
extern VOXLAP_DLL_FUNC int Vox_DrawRect2D(int, int, unsigned int, unsigned int, unsigned int, float);
float _Vox_KV6Project2D(float, float, float, int*, int*);
extern VOXLAP_DLL_FUNC int Vox_Estimate_Near_KV6_Shade(Vox_VX5Sprite *spr);
extern VOXLAP_DLL_FUNC int Vox_RayCastKV6(Vox_VX5Sprite*);
extern VOXLAP_DLL_FUNC int Vox_SpriteHitScan(Vox_VX5Sprite *, point3d *, point3d *, point3d *, kv6voxtype **);
extern VOXLAP_DLL_FUNC struct vx5_interface *Vox_GetVX5();
extern VOXLAP_DLL_FUNC int Vox_vloadvxl(const char*, unsigned int);
extern VOXLAP_DLL_FUNC Vox_VX5Sprite *Vox_InitSprite(Vox_VX5Sprite *);
extern VOXLAP_DLL_FUNC void Vox_ConvertToEucl(float, float, float, dpoint3d *, dpoint3d *, dpoint3d *);
extern VOXLAP_DLL_FUNC int Vox_DeleteFloatingBlock (int);
extern VOXLAP_DLL_FUNC int Vox_RenderPolygon (unsigned int color, int ulx, int uly, int urx, 
int ury, int llx, int lly, int lrx, int lry);


#if defined(VOXLAP_C) && defined(__cplusplus)
}
#endif

#endif
