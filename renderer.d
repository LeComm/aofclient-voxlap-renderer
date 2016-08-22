//Replace this file by any other .d file to change the renderer module

import derelict.sdl2.sdl;
import core.stdc.stdio : cstdio_fread=fread;
import std.algorithm;
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

vx5_interface *VoxlapInterface;

SDL_Surface *FrameBuf;

void Init_Renderer(){
	scrn_surface=SDL_CreateRGBSurface(0, ScreenXSize, ScreenYSize, 32, 0, 0, 0, 0);
	initvoxlap();
	VoxlapInterface=Vox_GetVX5();
	Set_Fog(0x0000ffff, 128);
	Vox_SetSideShades(32, 16, 8, 4, 32, 64);
}

void Load_Map(ubyte[] map){
	Vox_vloadvxl(cast(const char*)map.ptr, cast(uint)map.length);
}

float XFOV_Ratio=1.0, YFOV_Ratio=1.0;

void SetCamera(float xrotation, float yrotation, float tilt, float xfov, float yfov, float xpos, float ypos, float zpos){
	RenderCameraPos.x=xpos; RenderCameraPos.y=zpos; RenderCameraPos.z=ypos;
	Vox_ConvertToEucl(xrotation+90.0, yrotation, tilt, &Cam_ist, &Cam_ihe, &Cam_ifo);
	YFOV_Ratio=XFOV_Ratio=45.0/xfov;
	setcamera(&RenderCameraPos, &Cam_ist, &Cam_ihe, &Cam_ifo, FrameBuf.w/2, FrameBuf.h/2, FrameBuf.w*XFOV_Ratio);
}

void Prepare_Render(){
	
}

void Set_Frame_Buffer(SDL_Surface *srfc){
	voxsetframebuffer(cast(int)srfc.pixels, srfc.pitch, srfc.w, srfc.h);
	FrameBuf=srfc;
}

void Render_Voxels(){
	opticast();
}

void Render_FinishRendering(){
	SDL_UpdateTexture(vxrend_texture, null, scrn_surface.pixels, scrn_surface.pitch);
}

void UnInit_Renderer(){
	uninitvoxlap();
}

void Set_Renderer_Fog(uint fogcolor, uint fogrange){
	VoxlapInterface.fogcol=fogcolor|0xff000000;
	VoxlapInterface.maxscandist=fogrange;
}

//Note: It's ok if you don't even plan on implementing blur in your renderer
void Set_Blur(float amount){
	VoxlapInterface.anginc=1.0+amount*2.0;
}

uint Voxel_FindFloorZ(uint x, uint y, uint z){
	return getfloorz(x, z, y);
	/*for(y=0; y<MapYSize; y++){
		if(Voxel_IsSolid(x, y, z)){
			return y;
		}
	}*/
}

//Actually these don't belong here, but a renderer can bring its own map memory format
bool Voxel_IsSolid(uint x, uint y, uint z){
	return cast(bool)isvoxelsolid(x, z, y);
}

void Voxel_SetColor(uint x, uint y, uint z, uint col){
	setcube(x, z, y, (col&0x00ffffff)|0xfe000000);
}

void Voxel_SetShade(uint x, uint y, uint z, ubyte shade){
	if(shade>254)
		shade=254;
	setcube(x, z, y, (Voxel_GetColor(x, y, z)&0x00ffffff)|(shade<<24));
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
	ushort ypos;
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
	KV6Model_t *copy(){
		KV6Model_t *newmodel=new KV6Model_t;
		newmodel.xsize=xsize; newmodel.ysize=ysize; newmodel.zsize=zsize;
		newmodel.xpivot=xpivot; newmodel.ypivot=ypivot; newmodel.zpivot=zpivot;
		newmodel.voxelcount=voxelcount; newmodel.lowermip=lowermip;
		newmodel.voxels.length=voxels.length; newmodel.voxels[]=voxels[];
		newmodel.xlength.length=xlength.length; newmodel.xlength[]=xlength[];
		newmodel.ylength.length=ylength.length; newmodel.ylength[]=ylength[];
		return newmodel;
	}
}

extern(C) struct KV6Sprite_t{
	float rhe, rti, rst;
	float xpos, ypos, zpos;
	float xdensity, ydensity, zdensity;
	uint color_mod, replace_black;
	ubyte check_visibility;
	KV6Model_t *model;
}

int freadptr(void *buf, uint bytes, File f){
	if(!buf){
		writeflnlog("freadptr called with void buffer");
		return 0;
	}
	return cast(int)cstdio_fread(buf, bytes, 1u, f.getFP());
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
	freadptr(&model.xsize, 4, f); freadptr(&model.zsize, 4, f); freadptr(&model.ysize, 4, f);
	if(model.xsize<0 || model.ysize<0 || model.zsize<0){
		writeflnerr("Model file %s has invalid size (%d|%d|%d)", fname, model.xsize, model.ysize, model.zsize);
		return null;
	}
	freadptr(&model.xpivot, 4, f); freadptr(&model.zpivot, 4, f); freadptr(&model.ypivot, 4, f);
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
	model.ylength=new ushort[][](model.xsize, model.zsize);
	for(uint x=0; x<model.xsize; x++)
		for(uint z=0; z<model.zsize; z++)
			freadptr(&model.ylength[x][z], 2, f);
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

bool Project2D(float xpos, float ypos, float zpos, float *dist, out int scrx, out int scry){
	float dst=Vox_Project2D(xpos, zpos, ypos, &scrx, &scry);
	if(dist)
		*dist=dst;
	return xpos>=0 && ypos>=0;
}

alias Render_Rectangle=Vox_DrawRect2D;

/*void Render_Rectangle(int x, int y, int w, int h, uint col, float zdist){
	Vox_DrawRect2D(x, y, w, h, col, zdist);
}*/

void Render_Sprite(KV6Sprite_t *spr){
	if(spr.color_mod){
		if(spr.replace_black)
			_Render_Sprite!(true, true)(spr);
		else
			_Render_Sprite!(false, true)(spr);
	}
	else{
		if(spr.replace_black)
			_Render_Sprite!(true, false)(spr);
		else
			_Render_Sprite!(false, false)(spr);
	}
}

void _Render_Sprite(alias Enable_Black_Color_Replace, alias Enable_Color_Mod)(KV6Sprite_t *spr){
	if(!Sprite_Visible(spr))
		return;
	uint blkx, blkz;
	KV6Voxel_t *sblk, blk, eblk;
	uint blockadvance=1;
	{
		float xdiff=spr.xpos-RenderCameraPos.x, ydiff=spr.ypos-RenderCameraPos.z, zdiff=spr.zpos-RenderCameraPos.y;
		//Change this and make it consider ydiff too when not using Voxlap
		float l=sqrt(xdiff*xdiff+zdiff*zdiff);
		if(l>VoxlapInterface.maxscandist)
			return;
		if(!spr.xdensity || !spr.ydensity || !spr.zdensity)
			return;
		blockadvance=cast(uint)(l*l/(VoxlapInterface.maxscandist*VoxlapInterface.maxscandist)*2.0)+1;
	}
	int screen_w=FrameBuf.w, screen_h=FrameBuf.h;
	float KVRectW=(cast(float)FrameBuf.w)/2.0*XFOV_Ratio*2.0, KVRectH=(cast(float)FrameBuf.h)/2.0*YFOV_Ratio*2.0;
	float sprdensity=Vector3_t(spr.xdensity, spr.ydensity, spr.zdensity).length;
	float rot_sx, rot_cx, rot_sy, rot_cy, rot_sz, rot_cz;
	uint color_mod_alpha, color_mod_r, color_mod_g, color_mod_b;
	static if(Enable_Color_Mod){
		color_mod_alpha=(spr.color_mod>>24)&255;
		color_mod_r=(spr.color_mod>>16)&255;
		color_mod_g=(spr.color_mod>>8)&255;
		color_mod_b=(spr.color_mod>>0)&255;
	}
	rot_sx=sin((spr.rhe)*PI/180.0); rot_cx=cos((spr.rhe)*PI/180.0);
	rot_sy=sin(-(spr.rti+90.0)*PI/180.0); rot_cy=cos(-(spr.rti+90.0)*PI/180.0);
	rot_sz=sin(-spr.rst*PI/180.0); rot_cz=cos(-spr.rst*PI/180.0);
	for(blkx=0; blkx<spr.model.xsize; blkx+=blockadvance){
		for(blkz=0; blkz<spr.model.zsize; blkz+=blockadvance){
			uint index=Count_KV6Blocks(spr.model, blkx, blkz);
			if(index>=spr.model.voxelcount)
				continue;
			sblk=&spr.model.voxels[index];
			eblk=&sblk[cast(uint)spr.model.ylength[blkx][blkz]];
			for(blk=sblk; blk<eblk; blk+=blockadvance){
				if(!blk.visiblefaces)
					continue;
				float fnx=(blkx-spr.model.xpivot+.5)*spr.xdensity;
				float fny=(blk.ypos-spr.model.ypivot+.5)*spr.ydensity;
				float fnz=(blkz-spr.model.zpivot-.5)*spr.zdensity;
				float rot_y=fny, rot_z=fnz, rot_x=fnx;
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
				if(renddist<0.0 || isNaN(renddist))
					continue;
				int w=cast(int)(KVRectW*sprdensity/renddist)+1, h=cast(int)(KVRectH*sprdensity/renddist)+1;
				screenx-=w>>1; screeny-=h>>1;
				if(screenx+w<0 || screeny+h<0 || screenx>=screen_w || screeny>=screen_h){
					continue;
				}
				uint vxcolor=blk.color;
				static if(Enable_Black_Color_Replace){
					if((vxcolor&0x00ffffff)==0x00040404)
						vxcolor=spr.replace_black;
				}
				static if(Enable_Color_Mod){
					uint color_alpha, color_r, color_g, color_b;
					color_alpha=255-color_mod_alpha;
					color_r=(vxcolor>>>16)&255; 
					color_g=(vxcolor>>>8)&255; 
					color_b=(vxcolor>>>0)&255;
					color_r=(color_r*color_alpha+color_mod_r*color_mod_alpha)>>>8;
					color_g=(color_g*color_alpha+color_mod_g*color_mod_alpha)>>>8;
					color_b=(color_b*color_alpha+color_mod_b*color_mod_alpha)>>>8;
					vxcolor=(color_r<<16) | (color_g<<8) | (color_b);
				}
				Vox_Calculate_2DFog(cast(ubyte*)&vxcolor, fnx-RenderCameraPos.x, fnz-RenderCameraPos.y);
				Vox_DrawRect2D(screenx, screeny, w, h, vxcolor|0xff000000, renddist);
			}
		}
	}
}

void Draw_Smoke_Circle(int sx, int sy, int radius, uint color, uint alpha, float dist){
	if(dist>VoxlapInterface.maxscandist)
		return;
	int w=radius*2, h=radius*2;
	immutable uint fb_w=FrameBuf.w, fb_h=FrameBuf.h;
	uint neg_alpha=256-alpha;
	w=min(fb_w-sx, w); h=min(fb_h-sy, h);
	uint *pty=cast(uint*)((cast(ubyte*)(FrameBuf.pixels))+(sx<<2)+(sy*FrameBuf.pitch));
	int zbufoff=VoxlapInterface.zbufoff;
	float *zbufptr=cast(float*)((cast(ubyte*)pty)+zbufoff);
	uint cr=((color>>16)&255)*alpha, cg=((color>>8)&255)*alpha, cb=((color>>0)&255)*alpha;
	int pow_r=radius*radius;
	int min_x=sx<0 ? -sx : 0, min_y=sy<0 ? -sy : 0;
	int max_x=sx+w<fb_w ? w : fb_w-sx-1, max_y=sy+h<fb_h ? h : fb_h-sy-1;
	for(int y=min_y; y<max_y;++y){
		if(y<min_y)
			continue;
		int cy=y-radius;
		int powr=pow_r-cy*cy;
		for(int x=min_x; x<max_x; ++x){
			int cx=x-radius;
			if(cx*cx>powr)
				continue;
			if(dist<zbufptr[x]){
				zbufptr[x]=dist;
				pty[x]=0xff000000 | (((touint((pty[x]>>16)&255)*neg_alpha+cr)>>8)<<16) |
				(((touint((pty[x]>>8)&255)*neg_alpha+cg)>>8)<<8) | ((touint((pty[x]&255)*neg_alpha+cb)>>8)<<0);
			}
		}
		pty+=fb_w;
		zbufptr+=fb_w;
	}
	return;
}
