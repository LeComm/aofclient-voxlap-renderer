//Replace this file by any other .d file to change the renderer module

import derelict.sdl2.sdl;
import voxlap;
import gfx;
import misc;

dpoint3d CameraPos;
dpoint3d Cam_ist, Cam_ihe, Cam_ifo;

SDL_Surface *scrn_surface;

vx5_interface *VoxlapInterface;

void Init_Renderer(){
	scrn_surface=SDL_CreateRGBSurface(0, ScreenXSize, ScreenYSize, 32, 0, 0, 0, 0);
	initvoxlap();
	VoxlapInterface=Vox_GetVX5();
	Set_Fog(0x0000ffff, 128);
	Vox_SetSideShades(64, 32, 16, 0, 64, 0);
}

void Load_Map(ubyte[] map){
	Vox_vloadvxl(cast(const char*)map.ptr, map.length);
}

void SetCamera(float xrotation, float yrotation, float tilt, float xfov, float yfov, float xpos, float ypos, float zpos){
	CameraPos.x=xpos; CameraPos.y=zpos; CameraPos.z=ypos;
	Vox_ConvertToEucl(xrotation, yrotation, tilt, &Cam_ist, &Cam_ihe, &Cam_ifo);
	setcamera(&CameraPos, &Cam_ist, &Cam_ihe, &Cam_ifo, scrn_surface.w/2, scrn_surface.h/2, scrn_surface.w*45.0/xfov); //Or w*xfov/180.0
}

void Render_Voxels(){
	voxsetframebuffer(cast(int)scrn_surface.pixels, scrn_surface.pitch, scrn_surface.w, scrn_surface.h);
	opticast();
	SDL_UpdateTexture(scrn_texture, null, scrn_surface.pixels, scrn_surface.pitch);
}

void UnInit_Renderer(){
	uninitvoxlap();
}

void Set_Fog(uint fogcolor, uint fogrange){
	VoxlapInterface.fogcol=fogcolor;
	VoxlapInterface.maxscandist=fogrange;
}
