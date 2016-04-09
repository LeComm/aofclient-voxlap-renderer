//Replace this file by any other .d file to change the renderer module

import derelict.sdl2.sdl;
import core.stdc.stdio : cstdio_fread=fread;
import std.stdio;
import std.math;
import voxlap;
import protocol;
import gfx;
import world;
import misc;
import vector;

dpoint3d RenderCameraPos;
dpoint3d Cam_ist, Cam_ihe, Cam_ifo;

SDL_Surface *scrn_surface;

vx5_interface *VoxlapInterface;

void Init_Renderer(){
	scrn_surface=SDL_CreateRGBSurface(0, ScreenXSize, ScreenYSize, 32, 0, 0, 0, 0);
	initvoxlap();
	VoxlapInterface=Vox_GetVX5();
	Set_Fog(0x0000ffff, 128);
	Vox_SetSideShades(32, 16, 8, 4, 32, 64);
}

void Load_Map(ubyte[] map){
	Vox_vloadvxl(cast(const char*)map.ptr, map.length);
}

void SetCamera(float xrotation, float yrotation, float tilt, float xfov, float yfov, float xpos, float ypos, float zpos){
	RenderCameraPos.x=xpos; RenderCameraPos.y=zpos; RenderCameraPos.z=ypos;
	Vox_ConvertToEucl(xrotation, yrotation, tilt, &Cam_ist, &Cam_ihe, &Cam_ifo);
	setcamera(&RenderCameraPos, &Cam_ist, &Cam_ihe, &Cam_ifo, scrn_surface.w/2, scrn_surface.h/2, scrn_surface.w*45.0/xfov); //Or w*xfov/180.0
}

void Prepare_Render(){
	
}

void Render_Voxels(){
	voxsetframebuffer(cast(int)scrn_surface.pixels, scrn_surface.pitch, scrn_surface.w, scrn_surface.h);
	opticast();
}

void Render_FinishRendering(){
	SDL_UpdateTexture(scrn_texture, null, scrn_surface.pixels, scrn_surface.pitch);
}

void UnInit_Renderer(){
	uninitvoxlap();
}

void Set_Fog(uint fogcolor, uint fogrange){
	VoxlapInterface.fogcol=fogcolor;
	VoxlapInterface.maxscandist=fogrange;
}

//Actually these don't belong here, but a renderer can bring its own map memory format
bool Voxel_IsSolid(uint x, uint y, uint z){
	return cast(bool)isvoxelsolid(x, z, y);
}

void Voxel_SetColor(uint x, uint y, uint z, uint col){
	setcube(x, z, y, (col&0x00ffffff)|0x80000000);
}

uint Voxel_GetColor(uint x, uint y, uint z){
	int address=getcube(x, z, y);
	if(!address)
		return 0;
	if(address==1)
		return 0x01000000;
	return *(cast(uint*)address);
}

void Voxel_Remove(uint x, uint y, uint z){
	setcube(x, z, y, -1);
}


extern(C) struct KV6Voxel_t{
	uint color;
	ushort zpos;
	char visiblefaces, normalindex;
}

extern(C) struct KV6Model_t{
	int xsize, ysize, zsize;
	float xpivot, ypivot, zpivot;
	int voxelcount;
	extern(C) KV6Model_t *lowermip;
	extern(C) KV6Voxel_t[] voxels;
	extern(C) uint[] xlength;
	extern(C) ushort[][] ylength;
}

extern(C) struct KV6Sprite_t{
	float rhe, rti, rst;
	float xpos, ypos, zpos;
	float xdensity, ydensity, zdensity;
	KV6Model_t *model;
}

int freadptr(void *buf, uint bytes, File f){
	if(!buf){
		writeflnlog("freadptr called with void buffer");
		return 0;
	}
	return cstdio_fread(buf, bytes, 1u, f.getFP());
}

KV6Model_t *Load_KV6(string fname){
	File f=File(fname, "rb");
	if(!f.isOpen()){
		writeflnerr("Couldn't open %s", fname);
		return null;
	}
	
	string fileid;
	fileid.length=4;
	freadptr(cast(void*)fileid.ptr, 4, f);
	if(fileid!="Kvxl"){
		writeflnerr("Model file %s is not a valid KV6 file (wrong header)", fname);
		return null;
	}
	KV6Model_t *model=new KV6Model_t;
	freadptr(&model.xsize, 4, f); freadptr(&model.ysize, 4, f); freadptr(&model.zsize, 4, f);
	if(model.xsize<0 || model.ysize<0 || model.zsize<0){
		writeflnerr("Model file %s has invalid size (%d|%d|%d)", fname, model.xsize, model.ysize, model.zsize);
		return null;
	}
	freadptr(&model.xpivot, 4, f); freadptr(&model.ypivot, 4, f); freadptr(&model.zpivot, 4, f);
	freadptr(&model.voxelcount, 4, f);
	if(model.voxelcount<0){
		writeflnerr("Model file %s has invalid voxel count (%d)", fname, model.voxelcount);
		return null;
	}
	model.voxels=new KV6Voxel_t[](model.voxelcount);
	for(uint i=0; i<model.voxelcount; i++){
		freadptr(&model.voxels[i], model.voxels[i].sizeof, f);
	}
	model.xlength=new uint[](model.xsize);
	for(uint x=0; x<model.xsize; x++)
		freadptr(&model.xlength[x], 4, f);
	model.ylength=new ushort[][](model.xsize, model.ysize);
	for(uint x=0; x<model.xsize; x++)
		for(uint y=0; y<model.ysize; y++)
			freadptr(&model.ylength[x][y], 2, f);
	string palette;
	palette.length=4;
	freadptr(cast(void*)palette.ptr, 4, f);
	if(!f.eof()){
		if(palette=="SPal"){
			writeflnlog("Note: File %s contains a useless suggested palette block (SLAB6)", fname);
		}
		else{
			writeflnlog("Warning: File %s contains invalid data after its ending (corrupted file?)", fname);
			writeflnlog("KV6 size: (%d|%d|%d), pivot: (%d|%d|%d), amount of voxels: %d", model.xsize, model.ysize, model.zsize, 
			model.xpivot, model.ypivot, model.zpivot, model.voxelcount);
		}
	}
	f.close();
	return model;
}

uint Count_KV6Blocks(KV6Model_t *model, uint dstx, uint dsty){
	uint index=0;
	for(uint x=0; x<dstx; x++)
		index+=model.xlength[x];
	uint xy=dstx*model.ysize;
	for(uint y=0; y<dsty; y++)
		index+=model.ylength[dstx][y];
	return index;
}

int[2] Project2D(float xpos, float ypos, float zpos, float *dist){
	int[2] scrpos;
	float dst=Vox_Project2D(xpos, zpos, ypos, &scrpos[0], &scrpos[1]);
	if(dist)
		*dist=dst;
	return scrpos;
}

void Render_Sprite(KV6Sprite_t *spr){
	uint x, y;
	KV6Voxel_t *sblk, blk, eblk;
	{
		float xdiff=spr.xpos-RenderCameraPos.x, ydiff=spr.ypos-RenderCameraPos.z, zdiff=spr.zpos-RenderCameraPos.y;
		float l=sqrt(xdiff*xdiff+ydiff*ydiff+zdiff*zdiff);
		if(l>VoxlapInterface.maxscandist)
			return;
	}
	float sprdensity=Vector3_t(spr.xdensity, spr.ydensity, spr.zdensity).length;
	float rot_sx, rot_cx, rot_sy, rot_cy, rot_sz, rot_cz;
	rot_sx=sin(spr.rhe*PI/180.0); rot_cx=cos(spr.rhe*PI/180.0);
	rot_sy=sin(spr.rti*PI/180.0); rot_cy=cos(spr.rti*PI/180.0);
	rot_sz=sin(spr.rst*PI/180.0); rot_cz=cos(spr.rst*PI/180.0);
	for(x=0; x<spr.model.xsize; ++x){
		for(y=0; y<spr.model.ysize; ++y){
			uint index=Count_KV6Blocks(spr.model, x, y);
			if(index>=spr.model.voxelcount)
				continue;
			sblk=&spr.model.voxels[index];
			eblk=&sblk[cast(uint)spr.model.ylength[x][y]];
			for(blk=sblk; blk<eblk; ++blk){
				float fnx=(x-spr.model.xpivot)*spr.xdensity;
				float fny=(y-spr.model.ypivot)*spr.ydensity;
				float fnz=((spr.model.zsize-2-blk.zpos)-(spr.model.zsize-spr.model.zpivot))*spr.zdensity;
				float rot_y=fny, rot_z=fnz, rot_x;
				fny=rot_y*rot_cx - rot_z*rot_sx; fnz=rot_y*rot_sx + rot_z*rot_cx;
				rot_x=fnx; rot_z=fnz;
				fnz=rot_z*rot_cy - rot_x*rot_sy; fnx=rot_z*rot_sy + rot_x*rot_cy;
				rot_x=fnx; rot_y=fny;
				fnx=rot_x*rot_cz - rot_y*rot_sz; fny=rot_x*rot_sz + rot_y*rot_cz;
				fnx+=spr.xpos; fny+=spr.ypos; fnz+=spr.zpos;
				/*if(x==spr.model.xsize/2 && y==spr.model.ysize/2 && blk==sblk && spr.xdensity==.2f){
					writeflnlog("%s", (Vector3_t(fnx, fny, fnz)-Vector3_t(RenderCameraPos.x, RenderCameraPos.z, RenderCameraPos.y)).length);
				}*/
				int screenx, screeny;
				float renddist=Vox_Project2D(fnx, fnz, fny, &screenx, &screeny);
				if(renddist<0.0)
					continue;
				if(screenx<0 || screeny<0)
					continue;
				const float s=350.0;
				uint w=cast(uint)(s*sprdensity/renddist)+1, h=cast(uint)(s*sprdensity/renddist)+1;
				screenx-=w>>1; screeny-=h>>1;
				Vox_DrawRect2D(screenx, screeny, w, h, blk.color, renddist);
			}
		}
	}
}
