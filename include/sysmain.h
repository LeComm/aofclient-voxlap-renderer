/**************************************************************************************************
 * WINMAIN.CPP & SYSMAIN.H                                                                        *
 *                                                                                                *
 * Windows layer code written by Ken Silverman (http://advsys.net/ken) (1997-2009)                *
 * Additional modifications by Tom Dobrowolski (http://ged.ax.pl/~tomkh)                          *
 * You may use this code for non-commercial purposes as int as credit is maintained.             *
 **************************************************************************************************/
// This file has been modified from Ken Silverman's original release
//(Maybe it actually hasn't, but just in case)

#pragma once

/*#if !defined(SYSMAIN_C) && !defined(__cplusplus) && !defined(SYSMAIN)
#error "Cannot link C frontend to C++ Backend"
#endif*/


#if defined(SYSMAIN_C) && defined(__cplusplus)
	extern "C" {
	#define EXTERN_SYSMAIN extern "C"
#else
	#define EXTERN_SYSMAIN extern
#endif

	//System specific:
#ifdef _WIN32
	#define NOMAXMIN
	#define WIN32_LEAN_AND_MEAN
	#include <windows.h>
	extern HWND ghwnd;
	extern HINSTANCE ghinst;
	extern HDC startdc ();
	extern void stopdc ();
	extern void ddflip2gdi ();
	extern void setalwaysactive (int);
	extern void smartsleep (int timeoutms);
	extern int canrender ();
	extern int ddrawuseemulation;
	extern int ddrawdebugmode; // -1 = off, old ddrawuseemulation = on
	extern void debugdirectdraw (); //toggle debug mode
	extern int (CALLBACK *peekwindowproc)(HWND,UINT,WPARAM,LPARAM);
#else
	#pragma pack(push,1)
	typedef struct { unsigned char peRed, peGreen, peBlue, peFlags; } PALETTEENTRY;
	#pragma pack(pop)
	#define MAX_PATH 260
#endif
extern int cputype;
extern void setacquire (int mouse, int kbd);

	//Program Flow:
extern const char *prognam;
extern int progresiz;
int initapp (int argc, char **argv);
void doframe ();
extern void quitloop ();
void uninitapp ();

	//Video:
typedef struct { int x, y; char c, r0, g0, b0, a0, rn, gn, bn, an; } validmodetype;
extern validmodetype curvidmodeinfo;
extern int xres, yres, colbits, fullscreen, maxpages;
extern PALETTEENTRY pal[256];
extern int startdirectdraw (int *, int *, int *, int *);
extern void stopdirectdraw ();
extern void nextpage ();
extern int clearscreen (int fillcolor);
extern void updatepalette (int start, int danum);
extern int getvalidmodelist (validmodetype **validmodelist);
extern int changeres (int, int, int, int);

	//Sound:
#define KSND_3D 1 //Use LOGICAL OR (|) to combine flags
#define KSND_MOVE 2
#define KSND_LOOP 4
#define KSND_LOPASS 8
#define KSND_MEM 16
extern void playsound (const char *, int, float, void *, int);
extern void playsoundupdate (void *, void *);
extern void setears3d (float, float, float, float, float, float, float, float, float);
extern void setvolume (int);
extern int umixerstart (void (*mixfunc)(void *, int), int, int, int);
extern void umixerkill (int);
extern void umixerbreathe ();

	//Keyboard:
extern char keystatus[256];     //bit0=1:down
extern char ext_keystatus[256]; //bit0=1:down,bit1=1:was down
extern void readkeyboard ();
extern int keyread ();         //similar to kbhit()&getch() combined

	//Mouse:
extern char ext_mbstatus[8];    //bit0=1:down,bit1=1:was down, bits6&7=mwheel
extern int ext_mwheel;
extern int mousmoth;           //1:mouse smoothing (default), 0 otherwise
extern float mousper;           //Estimated mouse interrupt rate
extern void readmouse (float *, float *, int *);
#if !defined(SYSMAIN_C) && defined(__cplusplus)
extern void readmouse (float *, float *, float *, int *);
#endif
extern int ismouseout (int, int);
extern void setmouseout (void (*)(int, int), int, int);

	//Timer:
extern void readklock (double *);

	//code to change memory permissions now moved here because used in slab6 and voxlap
extern void code_rwx_unlock ( void * dep_protect_start, void * dep_protect_end);

#if defined(SYSMAIN_C) && defined(__cplusplus)
}
#endif
