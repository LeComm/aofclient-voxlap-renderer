//Replace this file by any other renderer.d file to change the renderer module
version(LDC){
	import ldc_stdlib;
}
version(GNU){
	import gdc_stdlib;
}
import derelict.sdl2.sdl;
import core.stdc.stdio : cstdio_fread=fread;
import std.algorithm;
import std.stdio;
import std.math;
import std.conv;
import std.string;
import std.random;
import std.traits;
import voxlap;
import protocol;
import gfx;
import world;
import misc;
import vector;
import ui;

alias RendererTexture_t=SDL_Texture *;
uint Renderer_WindowFlags=0;
immutable float Renderer_SmokeRenderSpeed=2.0*0.0;

dpoint3d RenderCameraPos, RenderCameraRot;
dpoint3d Cam_ist, Cam_ihe, Cam_ifo;
vx5_interface *VoxlapInterface;

SDL_Surface *vxrend_framebuf=null;
int *vxrend_framebuf_pixels;
uint vxrend_framebuf_pitch;
int vxrend_framebuf_w, vxrend_framebuf_h;
SDL_Texture *vxrend_texture=null;

SDL_Renderer *scrn_renderer=null;

bool lighting_update_flag=false;
uint global_lighting_update_pos=0;
uint global_lighting_update_size=16;
float global_lighting_update_timer=0.0f;
float global_lighting_update_interval=1.0f;

ubyte RendererBlurAlpha=0;
float RendererBlurAnginc=0.0;

bool[][] BBoxes_Set;

float RendererBrightness=1.0f;

float Renderer_BaseQuality=1.0f;

void Renderer_Init(){
	initvoxlap();
	VoxlapInterface=Vox_GetVX5();
	Renderer_SetFog(0x0000ffff, 128);
	{
		SDL_Surface *smoke_circle_srfc=__SmokeCircle_Generate(smoke_circle_tex_w, smoke_circle_tex_h);
		smoke_circle_tex=Renderer_NewTexture(smoke_circle_tex_w, smoke_circle_tex_h);
		Renderer_UploadToTexture(smoke_circle_srfc, smoke_circle_tex);
		SDL_SetTextureBlendMode(smoke_circle_tex, SDL_BLENDMODE_BLEND);
		SDL_FreeSurface(smoke_circle_srfc);
	}
}

void Renderer_SetUp(uint screen_xsize, uint screen_ysize){
	if(vxrend_framebuf)
		SDL_FreeSurface(vxrend_framebuf);
	vxrend_framebuf=SDL_CreateRGBSurface(0, screen_xsize, screen_ysize, 32, 0, 0, 0, 0);
	vxrend_framebuf_pixels=cast(int*)vxrend_framebuf.pixels;
	vxrend_framebuf_pitch=vxrend_framebuf.pitch; vxrend_framebuf_w=vxrend_framebuf.w; vxrend_framebuf_h=vxrend_framebuf.h;
	SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, Config_Read!bool("anti_aliasing") ? "1" : "0");
	if(!scrn_renderer)
		scrn_renderer=SDL_CreateRenderer(scrn_window, -1, (Config_Read!bool("hwaccel") ? SDL_RENDERER_ACCELERATED : SDL_RENDERER_SOFTWARE) | SDL_RENDERER_PRESENTVSYNC*Config_Read!bool("vsync"));
	if(vxrend_texture)
		SDL_DestroyTexture(vxrend_texture);
	vxrend_texture=SDL_CreateTexture(scrn_renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, vxrend_framebuf_w, vxrend_framebuf_h);
}

void Renderer_SetQuality(float quality){
	Renderer_BaseQuality=quality;
}

float max_h_brightness=192.0f/2.0;
//X- Z- X+ Z+ Y+ Y-
void Renderer_SetBlockFaceShading(Vector3_t shading){
	shading=shading.abs();
	shading=shading.vecabs();
	Vox_SetSideShades(
	to!ubyte((1.0+shading.x)*max_h_brightness),
	to!ubyte((1.0+shading.z)*max_h_brightness),
	to!ubyte((1.0-shading.x)*max_h_brightness),
	to!ubyte((1.0-shading.z)*max_h_brightness),
	to!ubyte((1.0-shading.y)*max_h_brightness),
	to!ubyte((1.0+shading.y)*max_h_brightness));
}

void Renderer_SetBrightness(float brightness){
	RendererBrightness=brightness;
}

uint surfaceconv=0, textureupload=0;

RendererTexture_t Renderer_NewTexture(uint xsize, uint ysize, bool streaming_texture=false){
	return SDL_CreateTexture(scrn_renderer, SDL_PIXELFORMAT_ARGB8888, streaming_texture ? SDL_TEXTUREACCESS_STREAMING : SDL_TEXTUREACCESS_STATIC,
	xsize, ysize);
}

RendererTexture_t Renderer_TextureFromSurface(SDL_Surface *srfc){
	return SDL_CreateTextureFromSurface(scrn_renderer, srfc);
}

void Renderer_UploadToTexture(SDL_Surface *srfc, SDL_Texture *tex){
	SDL_UpdateTexture(tex, null, srfc.pixels, srfc.pitch);
}

void Renderer_DestroyTexture(RendererTexture_t tex){
	SDL_DestroyTexture(tex);
}

void Renderer_LoadMap(ubyte[] map){
	Vox_vloadvxl(cast(const char*)map.ptr, cast(uint)map.length);
	BBoxes_Set=new bool[][](MapXSize, MapZSize);
	VoxlapInterface.lightmode=1;
	updatevxl();
	VoxlapInterface.lightmode=0;
}

void Renderer_StartRendering(bool Render_3D){
	voxsetframebuffer(vxrend_framebuf_pixels, vxrend_framebuf_pitch, vxrend_framebuf_w, vxrend_framebuf_h);
}

void _Register_Lighting_BBox(int xpos, int ypos, int zpos){
	updatebbox(xpos-1, zpos-1, ypos-1, xpos+1, zpos+1, ypos+1, 0);
}

void Renderer_DrawVoxels(){
	global_lighting_update_timer+=WorldSpeed;
	if(global_lighting_update_timer>=global_lighting_update_interval){
		uint newpos=global_lighting_update_pos+global_lighting_update_size;
		if(newpos>MapZSize)
			newpos=MapZSize;
		updatebbox(0, global_lighting_update_pos, 0, MapXSize, newpos, MapYSize, 0);
		if(newpos<MapZSize)
			global_lighting_update_pos=newpos;
		else
			global_lighting_update_pos=0;
		lighting_update_flag=true;
		global_lighting_update_timer=0.0f;
	}
	if(lighting_update_flag){
		VoxlapInterface.lightmode=1;
		updatevxl();
		VoxlapInterface.lightmode=0;
		lighting_update_flag=false;
		foreach(bdmg; BlockDamage){
			bdmg.UpdateVoxel();
		}
	}
	VoxlapInterface.anginc=Renderer_BaseQuality+RendererBlurAnginc;
	opticast();
}

void Renderer_Start2D(){
	ubyte brightness=to!ubyte(RendererBrightness*255.0f);
	SDL_UpdateTexture(vxrend_texture, null, vxrend_framebuf_pixels, vxrend_framebuf_pitch);
	SDL_SetTextureColorMod(vxrend_texture, brightness, brightness, brightness);
	if(RendererBlurAlpha>0){
		SDL_SetTextureBlendMode(vxrend_texture, SDL_BLENDMODE_BLEND);
		SDL_SetTextureAlphaMod(vxrend_texture, RendererBlurAlpha);
	}
	else{
		SDL_SetTextureBlendMode(vxrend_texture, SDL_BLENDMODE_NONE);
	}
	SDL_SetRenderTarget(scrn_renderer, null);
	SDL_RenderCopy(scrn_renderer, vxrend_texture, null, null);
	if(RendererSmokeCircleQueue.length){
		SDL_SetRenderTarget(scrn_renderer, null);
		SDL_SetTextureBlendMode(smoke_circle_tex, SDL_BLENDMODE_BLEND);
		foreach(ref smc; RendererSmokeCircleQueue){
			SDL_SetTextureColorMod(smoke_circle_tex, smc.bcol[0], smc.bcol[1], smc.bcol[2]);
			SDL_SetTextureAlphaMod(smoke_circle_tex, smc.alpha);
			SDL_RenderCopy(scrn_renderer, smoke_circle_tex, null, &smc.rect);
		}
		RendererSmokeCircleQueue.length=0;
	}
}

void Renderer_Blit2D(RendererTexture_t tex, uint[2]* size, SDL_Rect *dstr, ubyte alpha=255, ubyte[3] *ColorMod=null, SDL_Rect *srcr=null){
	ubyte[3] orig_cmod;
	ubyte orig_alphamod;
	SDL_BlendMode orig_blendmode;
	bool blend=alpha<255;
	if(blend){
		SDL_GetTextureBlendMode(tex, &orig_blendmode);
		SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
	}
	if(ColorMod){
		SDL_GetTextureColorMod(tex, &orig_cmod[0], &orig_cmod[1], &orig_cmod[2]);
		SDL_SetTextureColorMod(tex, (*ColorMod)[0], (*ColorMod)[1], (*ColorMod)[2]);
	}
	if(alpha<255){
		SDL_GetTextureAlphaMod(tex, &orig_alphamod);
		SDL_SetTextureAlphaMod(tex, alpha);
	}
	SDL_RenderCopy(scrn_renderer, tex, srcr, dstr);
	if(alpha<255){
		SDL_SetTextureAlphaMod(tex, orig_alphamod);
	}
	if(ColorMod){
		SDL_SetTextureColorMod(tex, orig_cmod[0], orig_cmod[1], orig_cmod[2]);
	}
	if(blend){
		SDL_SetTextureBlendMode(tex, orig_blendmode);
	}
}

void Renderer_Finish2D(){
}

void Renderer_UnInit(){
	uninitvoxlap();
}

void Renderer_ShowInfo(){}

void Renderer_FinishRendering(){
	SDL_RenderPresent(scrn_renderer);
}

float XFOV_Ratio=1.0f, YFOV_Ratio=1.0f;
void Renderer_SetCamera(float xrotation, float yrotation, float tilt, float xfov, float yfov, float xpos, float ypos, float zpos){
	RenderCameraPos.x=xpos; RenderCameraPos.y=zpos; RenderCameraPos.z=ypos;
	RenderCameraRot.x=xrotation; RenderCameraRot.y=yrotation; RenderCameraRot.z=tilt;
	Vox_ConvertToEucl(xrotation+90.0f, yrotation, tilt, &Cam_ist, &Cam_ihe, &Cam_ifo);
	YFOV_Ratio=45.0f/yfov; XFOV_Ratio=45.0f/xfov;
	setcamera(&RenderCameraPos, &Cam_ist, &Cam_ihe, &Cam_ifo, vxrend_framebuf_w/2, vxrend_framebuf_h/2, vxrend_framebuf_w*XFOV_Ratio);
}

void Renderer_SetFog(uint fogcolor, uint fogrange){
	VoxlapInterface.fogcol=fogcolor|0xff000000;
	VoxlapInterface.maxscandist=fogrange;
}

void Renderer_FillRect(SDL_Rect *rct, ubyte[4] *color){
	SDL_SetRenderDrawColor(scrn_renderer, (*color)[0], (*color)[1], (*color)[2], (*color)[3]);
	SDL_RenderFillRect(scrn_renderer, rct);
}

void Renderer_FillRect(SDL_Rect *rct, uint color){
	SDL_SetRenderDrawColor(scrn_renderer, (color>>16)&255, (color>>8)&255, (color)&255, (color>>24)&255);
	SDL_RenderFillRect(scrn_renderer, rct);
}

//NOTE: It's ok if you don't even plan on implementing blur in your renderer if it's some 100% CPU-based scrap that would only get lagged down
void Renderer_SetBlur(float amount){
	RendererBlurAlpha=cast(ubyte)(32+(tofloat(255-32)/(1.0f+amount)));
	RendererBlurAnginc=floor(amount*5.0f)/5.0f*2.0f;
}

//NOTE: Actually these don't belong here, but a renderer can bring its own map memory format
bool Voxel_IsSolid(Tx, Ty, Tz)(Tx x, Ty y, Tz z){
	return cast(bool)isvoxelsolid(cast(uint)x, cast(uint)z, cast(uint)y);
}

bool Voxel_IsSolid(T)(T pos) if(__traits(hasMember, T, "x") && __traits(hasMember, T, "y") && __traits(hasMember, T, "z")){
	return cast(bool)isvoxelsolid(cast(uint)pos.x, cast(uint)pos.z, cast(uint)pos.y);
}

bool Voxel_IsSolid(T)(T pos) if(isArray!T){
	return cast(bool)isvoxelsolid(cast(uint)pos[0], cast(uint)pos[0], cast(uint)pos[0]);
}

uint Voxel_GetHighestY(Tx, Ty, Tz)(Tx xpos, Ty ypos, Tz zpos){
	return getfloorz(cast(uint)xpos, cast(uint)zpos, cast(uint)ypos);
}

void Voxel_SetColor(Tx, Ty, Tz)(Tx xpos, Ty ypos, Tz zpos, uint col){
	bool update_minimap=false;
	if(Render_MiniMap){
		if(!Voxel_IsSolid(xpos, ypos, zpos)){
			if(Voxel_GetHighestY(xpos, 0, zpos)>ypos){
				update_minimap=true;
			}
		}
	}
	uint *oldcol=cast(uint*)getcube(cast(uint)xpos, cast(uint)zpos, cast(uint)ypos);
	if(oldcol==null){
		_Register_Lighting_BBox(xpos, ypos, zpos);
		lighting_update_flag=true;
	}
	else{
		if(oldcol>cast(uint*)1){
			col|=*oldcol&0xff000000;
		}
	}
	setcube(cast(uint)xpos, cast(uint)zpos, cast(uint)ypos, col&0x00ffffff);
	if(update_minimap){
		*Pixel_Pointer(minimap_srfc, xpos, zpos)=Voxel_GetColor(xpos, ypos, zpos)&0x00ffffff;
		MiniMap_SurfaceChanged=true;
	}
}

void Voxel_SetShade(uint x, uint y, uint z, ubyte shade){
	if(shade>254)
		shade=254;
	FlashVoxel_t *flash=Hash_Coordinates(x, y, z) in FlashVoxels;
	if(flash)
		flash.original_shade=shade;
	setcube(x, z, y, (Voxel_GetColor(x, y, z)&0x00ffffff)|(shade<<24));
}

ubyte Voxel_GetShade(uint x, uint y, uint z){
	return Voxel_GetColor(x, y, z)>>24;
}

uint Voxel_GetColor(uint x, uint y, uint z){
	uint* address=cast(uint*)getcube(x, z, y);
	if(address==null)
		return 0;
	if(address==cast(uint*)1)
		return VoxlapInterface.curcol;
	return *address;
}

FlashVoxel_t*[] VoxelRemoveFlashes=[null];
uint[] VoxelRemoveHashes=[0];
uint VoxelRemoveFlashesLength=1;
void Voxel_Remove(uint xpos, uint ypos, uint zpos){
	for(uint i=0; i<5; i++){
		int x=max(0, min(to!int(xpos)+((i<2)*((i&1)*2-1)), MapXSize-1));
		int z=max(0, min(to!int(zpos)+((i>1 && i<4)*((i&1)*2-1)), MapZSize-1));
		for(uint y=0; y<MapYSize; y++){
			uint hash=Hash_Coordinates(x, y, z);
			FlashVoxel_t *flash=hash in FlashVoxels;
			if(flash){
				*flash.shade=flash.original_shade;
				VoxelRemoveFlashes[VoxelRemoveFlashesLength-1]=flash;
				VoxelRemoveHashes[VoxelRemoveFlashesLength-1]=hash;
				VoxelRemoveFlashesLength++;
				if(VoxelRemoveFlashesLength>VoxelRemoveFlashes.length){
					VoxelRemoveFlashes.length=VoxelRemoveHashes.length=VoxelRemoveFlashesLength+10;
				}
			}
		}
	}
	if(getcube(cast(uint)xpos, cast(uint)zpos, cast(uint)ypos)){
		lighting_update_flag=true;
		_Register_Lighting_BBox(xpos, zpos, ypos);
	}
	setcube(xpos, zpos, ypos, -1);
	if(VoxelRemoveFlashesLength){
		foreach(ind, flash; VoxelRemoveFlashes[0..VoxelRemoveFlashesLength-1]){
			if(flash.x!=xpos || flash.y!=ypos || flash.z!=zpos){
					uint *addr=cast(uint*)getcube(flash.x, flash.y, flash.z);
					if(addr>cast(uint*)1)
						flash.shade=&(cast(ubyte*)addr)[3];
					else
						FlashVoxels.remove(VoxelRemoveHashes[ind]);
			}
			else{
				FlashVoxels.remove(VoxelRemoveHashes[ind]);
			}
		}
		VoxelRemoveFlashesLength=1;
	}
	//NOTE: No Update_Flashes(0.0) here, because of too much lag (but would fix non-fitting shadows around blocks that get destroyed in a flash spam)
	if(Render_MiniMap){
		if(Voxel_GetHighestY(xpos, 0, zpos)==ypos){
			for(uint y=ypos; y<MapYSize; y++){
				if(Voxel_IsSolid(xpos, y, zpos)){
					*Pixel_Pointer(minimap_srfc, xpos, zpos)=Voxel_GetColor(xpos, y, zpos);
				}
			}
			MiniMap_SurfaceChanged=true;
		}
	}
}

alias RendererParticleSize_t=uint;

RendererParticleSize_t[3] Renderer_GetParticleSize(float xsize, float ysize, float zsize){
	return [to!uint(sqrt(xsize*xsize*.5+zsize*zsize*.5)*ScreenXSize*.25), to!uint(ysize*ScreenYSize/3.0), 0];
}

enum ProjectionTypes{
	VoxlapProjection, WeirdMatrixStuffThatDoesntWorkForShit, RotationAndWeakPerspective, ScreenPlane
}

immutable ProjectionTypes ProjectionType=ProjectionTypes.VoxlapProjection;

static if(ProjectionType==ProjectionTypes.WeirdMatrixStuffThatDoesntWorkForShit){

dpoint3d last_camera_rot;
QMatrix_t mult_matrix;

float __Project2D(float xpos, float ypos, float zpos, int *scrx, int *scry){
	immutable float z_near=float.min_normal, z_far=VoxlapInterface.maxscandist;
	//Vector3_t dst=Vector3_t(RenderCameraPos.x, RenderCameraPos.z, RenderCameraPos.y)+Vector3_t(RenderCameraRot.y, RenderCameraRot.x, 0.0).RotationAsDirection()*2.0;
	//xpos=dst.x; ypos=dst.y; zpos=dst.z;
	if(RenderCameraRot!=last_camera_rot){
		QMatrix_t projection_mat=[
			[cast(double)atan(90.0*.5*PI/180.0), 0.0, 0.0, 0.0],
			[0.0, cast(double)atan(90.0*.5*PI/180.0), 0.0, 0.0],
			[0.0, 0.0, -(z_far+z_near)/(z_far-z_near)-1.0, -2.0*(z_near*z_far)/(z_far-z_near)],
			[0.0, 0.0, -1.0, 0.0]
		];
		immutable float xrot=-RenderCameraRot.x, yrot=RenderCameraRot.y;
		Vector3_t forward=Vector3_t(degcos(xrot)*degcos(yrot), degsin(yrot), degsin(xrot)*degcos(yrot));
		QMatrix_t lookat_mat=GluLookAt_Matrix(forward);
		mult_matrix=lookat_mat*projection_mat;
		last_camera_rot=RenderCameraRot;
	}
	Vector4_t dist=Vector4_t(xpos-RenderCameraPos.x, ypos-RenderCameraPos.z, zpos-RenderCameraPos.y, 1.0f);
	Vector4_t coords=mult_matrix*dist;
	coords.elements[0..3]/=coords.w;
	*scrx=cast(int)(coords.x*vxrend_framebuf_w/2/coords.z+vxrend_framebuf_w/2); *scry=cast(int)(-coords.y*vxrend_framebuf_h/2/coords.z+vxrend_framebuf_h/2);
	return (Vector3_t(xpos, ypos, zpos)-CameraPos).length;
}

QMatrix_t GluLookAt_Matrix(Vector3_t forward, Vector3_t up_vector=Vector3_t(0.0, -1.0, 0.0)){
	Vector3_t side=(forward.cross(up_vector)).normal();
	Vector3_t up=side.cross(forward).normal();
	QMatrix_t rotation_mat=[
		[side[0], side[1], side[2], 0.0],
		[up[0], up[1], up[2], 0.0],
		[forward[0], forward[1], forward[2], 0.0],
		[0.0, 0.0, 0.0, 1.0]
	];
	return rotation_mat;
}

}

static if(ProjectionType==ProjectionTypes.VoxlapProjection){
float __Project2D(immutable in float xpos, immutable in float ypos, immutable in float zpos, out int scrx, out int scry){
	return Vox_Project2D(xpos, zpos, ypos, &scrx, &scry);
}

};

static if(ProjectionType==ProjectionTypes.RotationAndWeakPerspective){
	float __Project2D(float xpos, float ypos, float zpos, int *scrx, int *scry){
		Vector3_t projectpos=Vector3_t(xpos, ypos, zpos);
		Vector3_t campos=Vector3_t(RenderCameraPos.x, RenderCameraPos.z, RenderCameraPos.y);
		projectpos=campos+Players[LocalPlayerID].dir*5.0;
		Vector3_t dist=projectpos-campos;
		//y ?? ??
		Vector3_t xrdist=dist.rotate_raw(Vector3_t(0.0, RenderCameraRot.x, 0.0))/dist.length;
		Vector3_t yrdist=dist.rotate_raw(Vector3_t(0.0, 0.0, -RenderCameraRot.y))/dist.length;
		writeflnlog("%s", yrdist);
		*scrx=cast(int)(((xrdist.x/(1.0+xrdist.z))*.5+.5)*vxrend_framebuf_w);
		*scry=cast(int)(((yrdist.y/(1.0+yrdist.z))*.5+.5)*vxrend_framebuf_h);
		return 10.0;
	}
}

static if(ProjectionType==ProjectionTypes.ScreenPlane){

dpoint3d last_camera_rot;
Vector3_t plane_start;
Vector3_t topleft_p, topright_p, leftbottom_p;
float __Project2D(float xpos, float ypos, float zpos, int *scrx, int *scry){
	if(RenderCameraRot!=last_camera_rot){
		last_camera_rot=RenderCameraRot;
		Vector3_t campos=Vector3_t(RenderCameraPos.x, RenderCameraPos.z, RenderCameraPos.y);
		plane_start=campos+Vector3_t(RenderCameraRot.y, RenderCameraRot.x, 0.0).RotationAsDirection();
		topleft_p=campos+Vector3_t(RenderCameraRot.y-45.0, RenderCameraRot.x-45.0, 0.0).RotationAsDirection();
	}
	return -1.0;
}

}

int[2] Project2D(immutable in float xpos, immutable in float ypos, immutable in float zpos, float *dist){
	int[2] scrpos;
	float dst=__Project2D(xpos, ypos, zpos, scrpos[0], scrpos[1]);
	if(dist)
		*dist=dst;
	return scrpos;
}

int[2] Project2D(immutable in float xpos, immutable in float ypos, immutable in float zpos){
	int[2] scrpos;
	__Project2D(xpos, ypos, zpos, scrpos[0], scrpos[1]);
	return scrpos;
}

int[2] Project2D(T)(immutable in T coord) if(__traits(hasMember, coord, "x") && __traits(hasMember, coord, "y") && __traits(hasMember, coord, "z")){
	int[2] scrpos;
	__Project2D(coord.x, coord.y, coord.z, scrpos[0], scrpos[1]);
	return scrpos;
}

bool Project2D(immutable in float xpos, immutable in float ypos, immutable in float zpos, out int scrx, out int scry){
	return __Project2D(xpos, ypos, zpos, scrx, scry)>=0.0;
}

bool Project2D(immutable in float xpos, immutable in float ypos, immutable in float zpos, out int scrx, out int scry, float *dist){
	float dst=__Project2D(xpos, ypos, zpos, scrx, scry);
	if(dist!=null)
		*dist=dst;
	return dst>=0.0;
}

void Renderer_Draw3DParticle(float x, float y, float z, RendererParticleSize_t w, RendererParticleSize_t h, RendererParticleSize_t l, uint col){
	float dist;
	int scrx, scry;
	int hw=w>>1, hh=h>>1;
	Project2D(x, y, z, scrx, scry, &dist);
	if(scrx<hw || scry<hh || scrx>=vxrend_framebuf_w-hw || scry>=vxrend_framebuf_h-hh || dist<1.0 || dist==typeof(dist).infinity)
		return;
	Vox_DrawRect2D(scrx, scry, w/to!int(dist)+1, h/to!int(dist)+1, col, dist);
}

void Renderer_Draw3DParticle(Vector3_t *pos, RendererParticleSize_t w, RendererParticleSize_t h, RendererParticleSize_t l, uint col){
	return Renderer_Draw3DParticle(pos.x, pos.y, pos.z, w, h, l, col);
}

alias Renderer_DrawWireframe=Renderer_DrawSprite;

void Renderer_DrawSprite(SpriteRenderData_t *sprrend, Vector3_t pos, Vector3_t rotation){
	Sprite_t spr;
	spr.model=sprrend.model;
	spr.pos=pos; spr.rot=rotation; spr.density=sprrend.size/Vector3_t(spr.model.size);
	spr.color_mod=sprrend.color_mod; spr.replace_black=sprrend.replace_black;
	spr.brightness=sprrend.brightness; spr.check_visibility=sprrend.check_visibility;
	return Renderer_DrawSprite(&spr);
}

void Renderer_DrawSprite(Sprite_t *spr){
	if(!Sprite_Visible(spr) && spr.check_visibility)
		return;
	immutable Sprite_t *cspr=cast(immutable Sprite_t*)spr;
	Vector3_t sprpos=Vector3_t(spr.xpos, spr.ypos, spr.zpos);
	float mindist=float.infinity;
	foreach(ref flash; Flashes){
		float dist=(flash.centre-sprpos).length;
		if(dist<flash.radius && dist<mindist){
			spr.brightness=to!ubyte((1.0-dist/flash.radius)*127.5f+127.5f);
			mindist=dist;
		}
	}
	if(spr.brightness){
		if(spr.color_mod){
			if(spr.replace_black)
				_Render_Sprite!(true, true, true)(*cspr);
			else
				_Render_Sprite!(false, true, true)(*cspr);
		}
		else{
			if(spr.replace_black)
				_Render_Sprite!(true, false, true)(*cspr);
			else
				_Render_Sprite!(false, false, true)(*cspr);
		}
	}
	else{
		if(spr.color_mod){
			if(spr.replace_black)
				_Render_Sprite!(true, true, false)(*cspr);
			else
				_Render_Sprite!(false, true, false)(*cspr);
		}
		else{
			if(spr.replace_black)
				_Render_Sprite!(true, false, false)(*cspr);
			else
				_Render_Sprite!(false, false, false)(*cspr);
		}
	}
}

void _Render_Sprite(alias Enable_Black_Color_Replace, alias Enable_Color_Mod, alias Brighten)(immutable in Sprite_t spr){
	uint blkx, blkz;
	float modeldist;
	{
		float xdiff=spr.xpos-RenderCameraPos.x, ydiff=spr.ypos-RenderCameraPos.z, zdiff=spr.zpos-RenderCameraPos.y;
		//Change this and make it consider ydiff too when not using Voxlap
		modeldist=sqrt(xdiff*xdiff+zdiff*zdiff);
		if(modeldist>VoxlapInterface.maxscandist)
			return;
		if(!spr.xdensity || !spr.ydensity || !spr.zdensity)
			return;
	}
	immutable uint blockadvance=cast(uint)(modeldist*modeldist/(VoxlapInterface.maxscandist*VoxlapInterface.maxscandist)*2.0f)+1;
	immutable float sprdensity=Vector3_t(spr.xdensity, spr.ydensity, spr.zdensity).length;
	immutable int screen_w=vxrend_framebuf_w, screen_h=vxrend_framebuf_h;
	immutable float KVRectW=(cast(float)screen_w)/2.0f*XFOV_Ratio*2.0f*blockadvance*sprdensity;
	immutable float KVRectH=(cast(float)screen_h)/2.0f*YFOV_Ratio*2.0f*blockadvance*sprdensity;
	Vector3_t renderrot=Vector3_t(spr.rhe, -(spr.rti+90.0), -spr.rst);
	immutable Vector3_t rdens=Vector3_t(spr.density).rotate_raw(renderrot)*.5;
	uint color_mod_alpha, color_mod_r, color_mod_g, color_mod_b;
	immutable uint fog_color_mod_r=(VoxlapInterface.fogcol>>16)&255, fog_color_mod_g=(VoxlapInterface.fogcol>>8)&255, fog_color_mod_b=(VoxlapInterface.fogcol)&255;
	immutable float inv_maxscandist=1.0/(VoxlapInterface.maxscandist*VoxlapInterface.maxscandist)*255.0;
	static if(Enable_Color_Mod){
		color_mod_alpha=(spr.color_mod>>24)&255;
		color_mod_r=(spr.color_mod>>16)&255;
		color_mod_g=(spr.color_mod>>8)&255;
		color_mod_b=(spr.color_mod>>0)&255;
	}
	immutable float rot_sx=sin((spr.rhe)*PI/180.0f), rot_cx=cos((spr.rhe)*PI/180.0f);
	immutable float rot_sy=sin(-(spr.rti+90.0f)*PI/180.0f), rot_cy=cos(-(spr.rti+90.0f)*PI/180.0f);
	immutable float rot_sz=sin(-spr.rst*PI/180.0f), rot_cz=cos(-spr.rst*PI/180.0f);
	immutable Vector3_t rotation_vcos=renderrot.cos(), rotation_vsin=renderrot.sin();
	immutable Vector3_t sprite_centre=Vector3_t(spr.pos)+rdens;
	/*struct DrawRectCall_t{
		int x, y, w, h;
		uint col;
		float dist;
	}
	DrawRectCall_t[] rectangles;
	rectangles.length=spr.model.voxels.length;
	uint _rlen=0;*/
	for(blkx=0; blkx<spr.model.xsize; blkx+=blockadvance){
		immutable xoffset=(blkx-spr.model.xpivot+.5)*spr.xdensity;
		for(blkz=0; blkz<spr.model.zsize; blkz+=blockadvance){
			immutable zoffset=(blkz-spr.model.zpivot-.5)*spr.zdensity;
			for(uint blkind=spr.model.offsets[blkx+blkz*spr.model.xsize];
			blkind<spr.model.offsets[blkx+blkz*spr.model.xsize]+cast(uint)spr.model.column_lengths[blkx+blkz*spr.model.xsize]; blkind+=blockadvance){
				if(!spr.model.voxels[blkind].visiblefaces)
					continue;
				uint vxcolor=spr.model.voxels[blkind].color;
				immutable ushort blky=spr.model.voxels[blkind].ypos;
				static if(!Program_Is_Optimized){
					float fnx=xoffset;
					float fny=(blky-spr.model.ypivot+.5)*spr.ydensity;
					float fnz=zoffset;
					float rot_y=fny, rot_z=fnz, rot_x=fnx;
					fny=rot_y*rot_cx - rot_z*rot_sx; fnz=rot_y*rot_sx + rot_z*rot_cx;
					rot_x=fnx; rot_z=fnz;
					fnz=rot_z*rot_cy - rot_x*rot_sy; fnx=rot_z*rot_sy + rot_x*rot_cy;
					rot_x=fnx; rot_y=fny;
					fnx=rot_x*rot_cz - rot_y*rot_sz; fny=rot_x*rot_sz + rot_y*rot_cz;
					fnx+=spr.xpos; fny+=spr.ypos; fnz+=spr.zpos;
				}
				else{
					immutable Vector3_t voxpos=((Vector3_t(blkx, blky, blkz)-spr.model.pivot)*spr.density).rotate_raw(rotation_vsin, rotation_vcos)+sprite_centre;
				}
				int screenx, screeny;
				static if(!Program_Is_Optimized)
					immutable float renddist=__Project2D(fnx, fny, fnz, screenx, screeny);
				else
					immutable float renddist=__Project2D(voxpos.x, voxpos.y, voxpos.z, screenx, screeny);
				if(renddist<0.0f || isNaN(renddist) || renddist>Current_Visibility_Range)
					continue;
				immutable float inv_renddist=1.0/renddist;
				immutable int w=cast(int)(KVRectW*inv_renddist)+1, h=cast(int)(KVRectH*inv_renddist)+1;
				screenx-=w>>1; screeny-=h>>1;
				if(screenx+w<0 || screeny+h<0 || screenx>=screen_w || screeny>=screen_h){
					continue;
				}
				static if(Enable_Black_Color_Replace){
					if((vxcolor&0x00ffffff)==0x00040404)
						vxcolor=spr.replace_black;
				}
				static if(Enable_Color_Mod){
					uint color_alpha=255-color_mod_alpha, color_r, color_g, color_b;
					color_r=(vxcolor>>>16)&255; 
					color_g=(vxcolor>>>8)&255; 
					color_b=(vxcolor>>>0)&255;
					color_r=(color_r*color_alpha+color_mod_r*color_mod_alpha)>>>8;
					color_g=(color_g*color_alpha+color_mod_g*color_mod_alpha)>>>8;
					color_b=(color_b*color_alpha+color_mod_b*color_mod_alpha)>>>8;
					vxcolor=(color_r<<16) | (color_g<<8) | (color_b);
				}
				static if(Brighten){
					ushort[3] carr=[vxcolor&255, (vxcolor>>8)&255, (vxcolor>>16)&255];
					carr[]*=spr.brightness;
					carr[]/=128;
					carr=[min(carr[0], cast(ushort)255), min(carr[1], cast(ushort)255), min(carr[2], cast(ushort)255)];
					vxcolor=carr[0] | (carr[1]<<8) | carr[2]<<16;
				}
				{
					immutable uint fog_color_mod_alpha=cast(uint)(renddist*renddist*inv_maxscandist);
					immutable uint fog_color_alpha=256-fog_color_mod_alpha;
					uint fog_color_r=(vxcolor>>>16)&255; 
					uint fog_color_g=(vxcolor>>>8)&255; 
					uint fog_color_b=(vxcolor>>>0)&255;
					fog_color_r=(fog_color_r*fog_color_alpha+fog_color_mod_r*fog_color_mod_alpha)>>>8;
					fog_color_g=(fog_color_g*fog_color_alpha+fog_color_mod_g*fog_color_mod_alpha)>>>8;
					fog_color_b=(fog_color_b*fog_color_alpha+fog_color_mod_b*fog_color_mod_alpha)>>>8;
					vxcolor=(fog_color_r<<16) | (fog_color_g<<8) | (fog_color_b);
				}
				//rectangles[_rlen++]=DrawRectCall_t(screenx, screeny, w, h, vxcolor|0xff000000, renddist);
				Renderer_DrawRect2D(screenx, screeny, w, h, vxcolor|0xff000000, renddist);
			}
		}
	}
	//foreach(rect; rectangles[0.._rlen])
	//	Renderer_DrawRect2D(rect.x, rect.y, rect.w, rect.h, rect.col, rect.dist);
}

void Renderer_DrawRect2D(int xpos, int ypos, int w, int h, immutable in uint col, immutable in float dist){
	if(xpos<0){
		w+=xpos; xpos=0;
	}
	if(ypos<0){
		h+=ypos; ypos=0;
	}
	if(xpos+w>=vxrend_framebuf_w)
		w=vxrend_framebuf_w-xpos;
	if(ypos+h>=vxrend_framebuf_h)
		h=vxrend_framebuf_h-ypos;
	uint *pixelptr=cast(uint*)((cast(ubyte*)vxrend_framebuf_pixels)+ypos*vxrend_framebuf_pitch+(xpos<<2));
	float *zbufptr=cast(float*)(VoxlapInterface.zbufoff+(cast(ubyte*)pixelptr));
	for(register_t y=0; y<h; y++){
		for(register_t x=0; x<w; x++){
			bool overwrite=zbufptr[x]>dist;
			zbufptr[x]=overwrite ? dist : zbufptr[x];
			pixelptr[x]=overwrite ? col : pixelptr[x];
		}
		zbufptr=cast(float*)(cast(ubyte*)zbufptr+vxrend_framebuf_pitch);
		pixelptr=cast(uint*)(cast(ubyte*)pixelptr+vxrend_framebuf_pitch);
	}
}

//x^2+y^2=w^2+h^2
SDL_Surface *__SmokeCircle_Generate(int w, int h){
	int hw=w/2, hh=h/2;
	SDL_Surface *srfc=SDL_CreateRGBSurface(0, w, h, 32, 0, 0, 0, 0);
	for(int x=-hw; x<hw; x++){
		for(int y=-hh; y<hh; y++){
			if(x*x*2+y*y*2<hw*hw+hh*hh){
				*Pixel_Pointer(srfc, x+hw, y+hh)=0xffffffff;
			}
			else{
				*Pixel_Pointer(srfc, x+hw, y+hh)=0x00000000;
			}
		}
	}
	return srfc;
}

private RendererTexture_t smoke_circle_tex;
private immutable uint smoke_circle_tex_w=1024, smoke_circle_tex_h=1024;
private struct RendererSmokeCircleQueue_t{
	SDL_Rect rect;
	union{
		uint icol;
		ubyte[4] bcol;
	}
	ubyte alpha;
}
private RendererSmokeCircleQueue_t[] RendererSmokeCircleQueue;


void Renderer_DrawSmokeCircle(immutable in float xpos, immutable in float ypos, immutable in float zpos, immutable in int radius, immutable in uint color, immutable in uint alpha, immutable in float dist){
	int sx, sy;
	if(!Project2D(xpos, ypos, zpos, sx, sy))
		return;
	if(dist>VoxlapInterface.maxscandist)
		return;
	immutable int w=radius*2, h=radius*2;
	immutable int fb_w=vxrend_framebuf_w, fb_h=vxrend_framebuf_h;
	immutable int renderxpos=sx, renderypos=sy-h/2;
	if(renderxpos+w<0 || renderypos+h<0 || renderxpos>=fb_w+w || renderypos>=fb_h+h)
		return;
	immutable uint neg_alpha=255-alpha;
	immutable uint cr=((color>>16)&255)*alpha, cg=((color>>8)&255)*alpha, cb=((color>>0)&255)*alpha;
	immutable int pow_r=radius*radius;
	immutable int min_y=renderypos<0 ? -renderypos : 0, max_y=renderypos+h<fb_h ? h : fb_h-renderypos-1;
	immutable int min_w=renderxpos<fb_w ? (fb_w-renderxpos) : -(renderxpos-fb_w);
	if(min_w>fb_w)
		return;
	if(renderxpos>fb_w)
		return;
	if(dist<.1)
		return;
	uint *pty=cast(uint*)((cast(ubyte*)(vxrend_framebuf_pixels))+(renderxpos<<2)+(renderypos*vxrend_framebuf_pitch));
	float *zbufptr=cast(float*)((cast(ubyte*)pty)+VoxlapInterface.zbufoff);
	immutable uint uiradius=radius;
	if(smoke_circle_tex){
		immutable uint[2][] __check_zbuf_pos=[
			[0u, 0u], [uiradius*2, 0u], [0u, uiradius*2u], [uiradius*2u, uiradius*2u], [uiradius, uiradius]
		];
		bool __no_zbuf_needed=true;
		foreach(pos; __check_zbuf_pos){
			if(zbufptr[renderxpos+pos[0]+(((renderypos+pos[1])*fb_w)>>2)]<dist){
				__no_zbuf_needed=false;
				break;
			}
		}
		if(__no_zbuf_needed){
			RendererSmokeCircleQueue_t c;
			c.rect=SDL_Rect(renderxpos-radius, renderypos, radius*2, radius*2); c.icol=color; c.alpha=to!ubyte(alpha);
			RendererSmokeCircleQueue~=c;
			return;
		}
	}
	immutable uint fb_p=vxrend_framebuf_pitch;
	for(int y=min_y; y<max_y;++y){
		if(y<min_y)
			continue;
		immutable int cy=y-uiradius;
		immutable int sqhwidth=pow_r-cy*cy;
		if(sqhwidth<=0)
			continue;
		immutable int hwidth=int_sqrt!int(sqhwidth);
		//immutable int lwidth=min(hwidth, renderxpos), rwidth=min(hwidth, min_w);
		immutable int lwidth=bitwise_min(hwidth, renderxpos), rwidth=bitwise_min(hwidth, min_w);
		for(int x=-lwidth; x<rwidth; ++x){
			if(dist<zbufptr[x]){
				//zbufptr[x]=dist;
				pty[x]=0xff000000 | (((((pty[x]>>16)&255)*neg_alpha+cr)>>8)<<16) |
				(((((pty[x]>>8)&255)*neg_alpha+cg)>>8)<<8) | (((pty[x]&255)*neg_alpha+cb)>>8);
			}
		}
		pty=cast(typeof(pty))((cast(ubyte*)pty)+fb_p);
		zbufptr=cast(typeof(zbufptr))((cast(ubyte*)zbufptr)+fb_p);
	}
	return;
}

SDL_Rect ScopeTextureBlitRect;
SDL_Texture *ScopeTexture;
uint ScopeTextureWidth=0, ScopeTextureHeight=0;
auto Renderer_DrawRoundZoomedIn(Vector3_t* scope_pos, Vector3_t* scope_rot, MenuElement_t *scope_picture, float xzoom, float yzoom){
	float scope_dist;
	int[2] scope_2D_pos=Project2D(scope_pos.x, scope_pos.y, scope_pos.z, &scope_dist);
	scope_2D_pos[]=scope_2D_pos[]*[cast(int)ScreenXSize, cast(int)ScreenYSize]/[vxrend_framebuf_w, vxrend_framebuf_h];
	immutable  uint scope_xsize=cast(uint)(scope_picture.xsize/scope_dist), scope_ysize=cast(uint)(scope_picture.ysize/scope_dist);
	scope_2D_pos[0]-=scope_xsize>>1; scope_2D_pos[1]-=to!int(scope_ysize*.3);
	struct return_type{
		SDL_Rect dstrect;
		SDL_Rect srcrect;
		SDL_Texture *scope_texture;
		uint scope_texture_width, scope_texture_height;
	}
	if(scope_2D_pos[0]+scope_xsize<0 || scope_2D_pos[1]+scope_ysize<0 || scope_2D_pos[0]>=ScreenXSize || scope_2D_pos[1]>=ScreenYSize)
		return return_type();
	//scope_2D_pos[]-=[cast(int)scope_xsize, cast(int)scope_ysize];
	{
		bool new_tex=false;
		if(!ScopeTexture)
			new_tex=true;
		else
			new_tex=(ScopeTextureWidth<scope_xsize) || (ScopeTextureHeight<scope_ysize);
		if(new_tex){
			if(ScopeTexture){
				SDL_DestroyTexture(ScopeTexture);
			}
			ScopeTextureWidth=scope_xsize+50; ScopeTextureHeight=scope_ysize+50;
			ScopeTexture=SDL_CreateTexture(scrn_renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, ScopeTextureWidth, ScopeTextureHeight);
			SDL_SetTextureBlendMode(ScopeTexture, SDL_BLENDMODE_BLEND);
		}
	}
	ubyte brightness=to!ubyte(RendererBrightness*255.0f);
	SDL_SetTextureColorMod(ScopeTexture, brightness, brightness, brightness);
	SDL_Rect lock_rect; lock_rect.x=0; lock_rect.y=0; lock_rect.w=scope_xsize; lock_rect.h=scope_ysize;
	void *scope_texture_pixels;
	int scope_texture_pitch;
	if(SDL_LockTexture(ScopeTexture, &lock_rect, &scope_texture_pixels, &scope_texture_pitch)){
		writeflnlog("ERROR LOCKING TEXTURE: %s\n", fromStringz(SDL_GetError()));
		return return_type();
	}
	vxrend_framebuf_pixels=cast(int*)scope_texture_pixels; vxrend_framebuf_pitch=scope_texture_pitch;
	vxrend_framebuf_w=scope_xsize; vxrend_framebuf_h=scope_ysize;
	Renderer_StartRendering(true);
	Renderer_SetCamera(scope_rot.y, scope_rot.x, scope_rot.z, X_FOV/xzoom/(tofloat(ScreenXSize)/scope_xsize), Y_FOV/yzoom/(tofloat(ScreenYSize)/scope_ysize), scope_pos.x, scope_pos.y, scope_pos.z);
	Do_Sprite_Visibility_Checks=true;
	Render_World!false(false);
	vxrend_framebuf_pixels=cast(int*)vxrend_framebuf.pixels; vxrend_framebuf_pitch=vxrend_framebuf.pitch;
	vxrend_framebuf_w=vxrend_framebuf.w; vxrend_framebuf_h=vxrend_framebuf.h;
	Renderer_StartRendering(true);
	immutable float pow_w=scope_xsize*scope_xsize/2.0f/2.0f;
	immutable float x_y_ratio=to!float(scope_xsize)/to!float(scope_ysize);
	uint *scope_surface_ptr=cast(uint*)scope_texture_pixels;
	for(int circle_y=-cast(int)((scope_ysize>>1)); circle_y<=cast(int)(scope_ysize>>1); circle_y++){
		float circle_x=circle_y*x_y_ratio;
		float powdist=pow_w-circle_x*circle_x;
		if(powdist<0.0f) //Hack to fix some float bug that makes pow_w<circle_x*circle_x despite the numbers being equal
			powdist=0.0f;
		immutable uint width=(cast(uint)sqrt(powdist))<<1;
		immutable uint sx=(scope_xsize-width)>>1;
		if(sx){
			scope_surface_ptr[0..sx]=0;
			scope_surface_ptr[sx+width..scope_xsize]=0;
		}
		scope_surface_ptr=cast(uint*)((cast(ubyte*)scope_surface_ptr)+scope_texture_pitch);
	}
	if(SDL_UnlockTexture(ScopeTexture)){
		writeflnlog("ERROR UNLOCKING TEXTURE: %s\n", fromStringz(SDL_GetError()));
		return return_type();
	}
	return_type ret;
	ret.dstrect.w=scope_xsize; ret.dstrect.h=scope_ysize;
	ret.dstrect.x=scope_2D_pos[0]; ret.dstrect.y=scope_2D_pos[1]-ret.dstrect.h/2;
	ret.srcrect=lock_rect;
	ret.scope_texture=ScopeTexture;
	ret.scope_texture_width=ScopeTextureWidth; ret.scope_texture_height=ScopeTextureHeight;
	return ret;
}

struct FlashVoxelLink_t{
	float brightness;
	Flash_t *flash;
}

struct FlashVoxel_t{
	uint x, y, z;
	ubyte original_shade;
	ubyte *shade;
	FlashVoxelLink_t[] flashes;
}

struct Flash_t{
	Vector3_t centre;
	uint[] voxels;
	float radius;
	float timer;
	float decay;
}

Flash_t*[] Flashes;
FlashVoxel_t[uint] FlashVoxels;

void Renderer_AddFlash(Vector3_t pos, float radius, float brightness){
	Flash_t *flash=new Flash_t;
	Flashes~=flash;
	Vector3_t spos=vmax(pos-radius, Vector3_t(0)), epos=vmin(pos+radius, Vector3_t(MapXSize, MapYSize, MapZSize));
	for(uint x=to!uint(spos.x); x<epos.x; x++){
		for(uint z=to!uint(spos.z); z<epos.z; z++){
			for(uint y=0; y<63; y++){
				y=getfloorz(x, z, y);
				Vector3_t vecdist=Vector3_t(x, y, z)-pos;
				float voxdist=vecdist.length;
				if(voxdist>radius)
					continue;
				uint *addr=cast(uint*)getcube(x, z, y);
				if(addr>cast(uint*)1){
					uint hash=Hash_Coordinates(x, y, z);
					FlashVoxel_t *vox=hash in FlashVoxels;
					if(vox==null){
						FlashVoxels[hash]=FlashVoxel_t();
						vox=hash in FlashVoxels;
						vox.x=x; vox.y=y; vox.z=z;
						vox.shade=&(cast(ubyte*)addr)[3];
						vox.original_shade=*vox.shade;
					}
					vox.flashes~=FlashVoxelLink_t(brightness*(1.0f-voxdist/radius)*255.0f, flash);
					flash.voxels~=hash;
				}
			}
		}
	}
	flash.centre=pos;
	flash.radius=radius;
	flash.timer=1.0;
	flash.decay=1.0/(radius*radius);
}

void Renderer_UpdateFlashes(alias UpdateGfx=true)(float update_speed){
	if(UpdateGfx){
		update_speed*=50.0f;
		foreach(flash; Flashes){
			if(!flash.timer)
				continue;
			flash.timer-=update_speed*flash.decay;
			if(flash.timer<0.0f){
				flash.timer=0.0f;
				foreach(voxhash; flash.voxels){
					FlashVoxel_t *vox=voxhash in FlashVoxels;
					if(vox==null)
						continue;
					if(vox.flashes.length==1){
						vox.flashes.length=0;
						*vox.shade=vox.original_shade;
						FlashVoxels.remove(voxhash);
						continue;
					}
					foreach(ind, voxflash; vox.flashes){
						if(voxflash.flash==flash){
							remove(vox.flashes, ind);
							break;
						}
					}
				}
				FlashVoxels.rehash();
			}
		}
	}
	while(Flashes.length){
		if(!Flashes[$-1].timer)
			Flashes.length--;
		else
			break;
	}
	foreach(voxel; FlashVoxels.byValue()){
		uint newshade=voxel.original_shade;
		foreach(flash; voxel.flashes)
			newshade+=cast(ubyte)(flash.brightness*flash.flash.timer);
		*voxel.shade=cast(ubyte)min(newshade, 255);
	}
}
