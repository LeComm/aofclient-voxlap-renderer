extern(C) struct lpoint3d{ int x, y, z; }
extern(C) struct point3d{ float x, y, z; }
extern(C) struct dpoint3d{ double x, y, z; }

extern(C) int initvoxlap();
extern(C) int uninitvoxlap();
extern(C) void voxsetframebuffer(int, int, int, int);
extern(C) void setcamera(dpoint3d *, dpoint3d *, dpoint3d *, dpoint3d *, float, float, float);
extern(C) void opticast();


extern(C) int Vox_vloadvxl(const char*, uint);
extern(C) int Vox_ConvertToEucl(float, float, float, dpoint3d *, dpoint3d *, dpoint3d *);
extern(C) int loadvxl(const char*);

extern(C) void Vox_SetSideShades(ubyte, ubyte, ubyte, ubyte, ubyte, ubyte);


extern(C) int isvoxelsolid(int, int, int);
extern(C) int getfloorz(int, int, int);
extern(C) int getcube(int, int, int);
extern(C) void setcube(int, int, int, int);
extern(C) void Vox_Calculate_2DFog(ubyte*, float, float);


extern(C) float Vox_Project2D(float, float, float, int*, int*);
extern(C) int Vox_DrawRect2D(int, int, uint, uint, uint, float);

extern(C) vx5_interface *Vox_GetVX5();

immutable uint FLPIECES=256; /*Max # of separate falling pieces*/
extern(C) struct flstboxtype/*(68 bytes)*/
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
}

	/*Lighting variables: (used by updatelighting)*/
immutable uint MAXLIGHTS=256;
extern(C) struct lightsrctype{ point3d p; float r2, sc; char Light_KV6;}

	/*Used by setspans/meltspans. Ordered this way to allow sorting as longs!*/
extern(C) struct vspans{ char z1, z0, x, y; }

immutable uint MAXFRM=1024; /*MUST be even number for alignment!*/

	/*Voxlap5 shared global variables:*/
extern(C) struct vx5_interface
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
	
	/*Stuff added by LeCom*/
	/*0.0 - 1.0*/
	float KV6_Darkness;
	int zbufoff;
	uint kv6_anginc;
	uint CPU_Pos;

		/*Map modification function data:*/
	int curcol, currad, curhei;
	float curpow;

		/*Procedural texture function data:*/
	int function(lpoint3d*) colfunc;
	int cen, amount;
	int *pic;
	int bpl, xsiz, ysiz, xoru, xorv, picmode;
	point3d fpico, fpicu, fpicv, fpicw;
	lpoint3d pico, picu, picv;
	float daf;

		/*Lighting variables: (used by updatelighting)*/
	int lightmode; /*0 (default), 1:simple lighting, 2:lightsrc lighting*/
	lightsrctype lightsrc[MAXLIGHTS]; /*(?,?,?),128*128,262144*/
	int numlights;

	int fallcheck;
	
};
