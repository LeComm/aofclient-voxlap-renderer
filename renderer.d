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
import core.simd;
import voxlap;
import protocol;
import gfx;
import world;
import misc;
import vector;
import ui;
import renderer_templates;

public uint Renderer_WindowFlags=0;
public alias RendererTexture_t=SDL_Texture *;
public immutable float Renderer_SmokeRenderSpeed=.4+.35*Program_Is_Optimized;

private Vector_t!(3, real) RenderCameraPos, RenderCameraRot;
private Vector_t!(3, real) RenderCamera_VForward, RenderCamera_VSide, RenderCamera_VUp;
private register_t hframebuf_w, hframebuf_h;
private real fhframebuf_w, fhframebuf_h;
private dpoint3d Cam_ipo, Cam_ist, Cam_ihe, Cam_ifo;
private vx5_interface *VoxlapInterface;

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

ubyte[] Fog_AlphaValues;
uint Fog_AlphaColorComponent1, Fog_AlphaColorComponent2;

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
	SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, Config_Read!bool("anti_aliasing") ? "1" : "0");
	if(!scrn_renderer){
		if(Config_Read!string("videodriver")!="auto")
			SDL_SetHint(SDL_HINT_RENDER_DRIVER, toStringz(Config_Read!string("videodriver")));
		/*int drivers=SDL_GetNumRenderDrivers();
		if(drivers>0){
			SDL_RendererInfo[] renderer_info;
			renderer_info.length=drivers;
			writeflnlog("Available render drivers:");
			for(uint i=0; i<renderer_info.length; i++){
				SDL_GetRenderDriverInfo(i, &renderer_info[i]);
				writeflnlog("	%s", fromStringz(renderer_info[i].name));
			}
		}
		else{
			writeflnerr("Failed listing avilable SDL render drivers: %s", fromStringz(SDL_GetError()));
		}*/
		scrn_renderer=SDL_CreateRenderer(scrn_window, -1, (Config_Read!bool("hwaccel") ? SDL_RENDERER_ACCELERATED : SDL_RENDERER_SOFTWARE) | SDL_RENDERER_PRESENTVSYNC*Config_Read!bool("vsync"));
	}
	bool new_framebuf=!vxrend_framebuf;
	if(vxrend_framebuf){
		if(vxrend_framebuf.w!=screen_xsize || vxrend_framebuf.h!=screen_ysize){
			SDL_FreeSurface(vxrend_framebuf);
			new_framebuf=true;
		}
	}
	if(new_framebuf){
		vxrend_framebuf=SDL_CreateRGBSurface(0, screen_xsize, screen_ysize, 32, 0, 0, 0, 0);
		vxrend_framebuf_pixels=cast(int*)vxrend_framebuf.pixels;
		vxrend_framebuf_pitch=vxrend_framebuf.pitch; vxrend_framebuf_w=vxrend_framebuf.w; vxrend_framebuf_h=vxrend_framebuf.h;
		if(vxrend_texture)
			SDL_DestroyTexture(vxrend_texture);
	}
	//SDL_SetRenderDrawBlendMode(scrn_renderer, SDL_BLENDMODE_BLEND);
	vxrend_texture=SDL_CreateTexture(scrn_renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, vxrend_framebuf_w, vxrend_framebuf_h);
}

real XFOV_Ratio=1.0, YFOV_Ratio=1.0;
void Renderer_SetCamera(real xrotation, real yrotation, real tilt, real xfov, real yfov, real xpos, real ypos, real zpos){
	RenderCameraPos=typeof(RenderCameraPos)(xpos, ypos, zpos);
	RenderCameraRot=typeof(RenderCameraPos)(xrotation, yrotation, tilt);
	XFOV_Ratio=45.0/xfov; YFOV_Ratio=45.0/yfov;
	auto rot=typeof(RenderCamera_VForward)(xrotation, yrotation, tilt);
	RenderCamera_VForward=typeof(RenderCamera_VForward)(degcos(rot.x)*degcos(rot.y), degsin(rot.y), degsin(rot.x)*degcos(rot.y)).normal();
	RenderCamera_VSide=-RenderCamera_VForward.cross(typeof(RenderCamera_VSide)(0.0, -1.0, 0.0)).normal()*vxrend_framebuf_w*45.0/xfov;
	RenderCamera_VUp=RenderCamera_VSide.cross(RenderCamera_VForward).normal()*vxrend_framebuf_w*45.0/yfov;
	hframebuf_w=vxrend_framebuf_w>>>1; hframebuf_h=vxrend_framebuf_h>>>1;
	fhframebuf_w=hframebuf_w; fhframebuf_h=hframebuf_h;
	__Renderer_SetCam();
}

void __Renderer_SetCam(){
	Vox_ConvertToEucl(RenderCameraRot.x+90.0f, RenderCameraRot.y, RenderCameraRot.z, &Cam_ist, &Cam_ihe, &Cam_ifo);
	Cam_ipo.x=RenderCameraPos.x; Cam_ipo.y=RenderCameraPos.z; Cam_ipo.z=RenderCameraPos.y;
	setcamera(&Cam_ipo, &Cam_ist, &Cam_ihe, &Cam_ifo, vxrend_framebuf_w/2, vxrend_framebuf_h/2, vxrend_framebuf_w*XFOV_Ratio);
}

void Renderer_SetLOD(float lod){
	Renderer_BaseQuality=lod;
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

RendererTexture_t Renderer_NewTexture(uint xsize, uint ysize, bool streaming_texture=false){
	if(!streaming_texture)
		return SDL_CreateTexture(scrn_renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, xsize, ysize);
	SDL_Texture *tex=SDL_CreateTexture(scrn_renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, xsize, ysize);
	if(!tex)
		return SDL_CreateTexture(scrn_renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, xsize, ysize);
	return tex;
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

void _Register_Lighting_BBox(int xpos, int ypos, int zpos){
	updatebbox(xpos-1, zpos-1, ypos-1, xpos+1, zpos+1, ypos+1, 0);
}

version(DigitalMars){
	@nogc pragma(inline, true):
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
	if(lighting_update_flag && (Config_Read!bool("explosion_flashes") || Config_Read!bool("gun_flashes"))){
		VoxlapInterface.lightmode=1;
		updatevxl();
		VoxlapInterface.lightmode=0;
		lighting_update_flag=false;
		foreach(bdmg; BlockDamage){
			bdmg.UpdateVoxel();
		}
	}
	VoxlapInterface.anginc=Renderer_BaseQuality+RendererBlurAnginc;
	__Renderer_SetCam();
	voxsetframebuffer(vxrend_framebuf_pixels, vxrend_framebuf_pitch, vxrend_framebuf_w, vxrend_framebuf_h);
	opticast();
	/*VoxlapInterface.zbufoff+=vxrend_framebuf_w*4/2;
	Vox_ConvertToEucl(RenderCameraRot.x+90.0f, RenderCameraRot.y, 180.0*PI/180.0, &Cam_ist, &Cam_ihe, &Cam_ifo);
	setcamera(&RenderCameraPos, &Cam_ist, &Cam_ihe, &Cam_ifo, vxrend_framebuf_w/2, vxrend_framebuf_h/2, vxrend_framebuf_w*XFOV_Ratio);
	voxsetframebuffer((vxrend_framebuf_pixels+vxrend_framebuf_w/2), vxrend_framebuf_pitch, vxrend_framebuf_w/2, vxrend_framebuf_h);
	opticast();
	VoxlapInterface.zbufoff-=vxrend_framebuf_w*4/2;
	//for(int x=1; x<vxrend_framebuf_w/2; x++){
		for(int y=1; y<vxrend_framebuf_h/2; y++){
			immutable auto ind1=vxrend_framebuf_w/2-1+x+(y)*vxrend_framebuf_w, ind2=vxrend_framebuf_w/2-1+(vxrend_framebuf_w/2-x)+(vxrend_framebuf_h-y-1)*vxrend_framebuf_w;
			swap(vxrend_framebuf_pixels[ind1], vxrend_framebuf_pixels[ind2]);
			swap((cast(float*)((cast(ubyte*)(&vxrend_framebuf_pixels[ind1]))+VoxlapInterface.zbufoff))[0],
			(cast(float*)((cast(ubyte*)(&vxrend_framebuf_pixels[ind2]))+VoxlapInterface.zbufoff))[0]);
		}
	//}
	Vox_ConvertToEucl(RenderCameraRot.x+90.0f, RenderCameraRot.y, 0, &Cam_ist, &Cam_ihe, &Cam_ifo);
	setcamera(&RenderCameraPos, &Cam_ist, &Cam_ihe, &Cam_ifo, vxrend_framebuf_w/2, vxrend_framebuf_h/2, vxrend_framebuf_w*XFOV_Ratio);
	voxsetframebuffer(vxrend_framebuf_pixels, vxrend_framebuf_pitch, vxrend_framebuf_w/2, vxrend_framebuf_h);*/
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

void Renderer_StartRendering(bool Render_3D){
}

void Renderer_SetFog(uint fogcolor, uint fogrange){
	if(fogrange!=Fog_AlphaValues.length){
		Fog_AlphaValues.length=fogrange;
		for(uint i=0; i<fogrange; i++){
			Fog_AlphaValues[i]=cast(typeof(Fog_AlphaValues[0]))((cast(real)(i*i))*255.0/(cast(real)((fogrange-1)*(fogrange-1))));
		}
	}
	Fog_AlphaColorComponent1=fogcolor&0x00ff00ff; Fog_AlphaColorComponent2=fogcolor&0x0000ff00;
	VoxlapInterface.fogcol=fogcolor|0xff000000;
	VoxlapInterface.maxscandist=fogrange;
}

void Renderer_FillRect2D(SDL_Rect *rct, ubyte[4] *color){
	if((*color)[3]){
		SDL_SetRenderDrawBlendMode(scrn_renderer, SDL_BLENDMODE_BLEND);
	}
	SDL_SetRenderDrawColor(scrn_renderer, (*color)[0], (*color)[1], (*color)[2], (*color)[3]);
	SDL_RenderFillRect(scrn_renderer, rct);
	if((*color)[3]){
		SDL_SetRenderDrawBlendMode(scrn_renderer, SDL_BLENDMODE_NONE);
	}
}

void Renderer_DrawLine2D(int x1, int y1, int x2, int y2, ubyte[4] *color){
	if((*color)[3]){
		SDL_SetRenderDrawBlendMode(scrn_renderer, SDL_BLENDMODE_BLEND);
	}
	SDL_SetRenderDrawColor(scrn_renderer, (*color)[0], (*color)[1], (*color)[2], (*color)[3]);
	SDL_RenderDrawLine(scrn_renderer, x1, y1, x2, y2);	if((*color)[3]){
		SDL_SetRenderDrawBlendMode(scrn_renderer, SDL_BLENDMODE_NONE);
	}
}

//NOTE: It's ok if you don't even plan on implementing blur in your renderer if it's some 100% CPU-based scrap that would only get lagged down
void Renderer_SetBlur(real amount){
	real screen_size_ratio=pow(1000.0/sqrt(cast(real)(ScreenXSize*ScreenXSize+ScreenYSize*ScreenYSize)), 7.0);
	ubyte min_alpha=32;
	RendererBlurAlpha=cast(ubyte)(min_alpha+(255-min_alpha)/(1.0+amount*screen_size_ratio));
	RendererBlurAnginc=floor(amount*5.0)/5.0*2.0;
}

//NOTE: Actually these don't belong here, but a renderer can bring its own map memory format
bool Voxel_IsSolid(Tx, Ty, Tz)(Tx x, Ty y, Tz z){
	return cast(bool)isvoxelsolid(cast(uint)x, cast(uint)z, cast(uint)y);
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

version(DigitalMars){
	@nogc pragma(inline, true):
}
nothrow T __Project2D(T)(Vector_t!(3, T) pos, ref signed_register_t scrx, ref signed_register_t scry){
	immutable vdist=pos-RenderCameraPos;
	immutable ddist=vdist.dot(RenderCamera_VForward);
	immutable tdist=vdist/ddist;
	scrx=(cast(signed_register_t)(tdist.dot(RenderCamera_VSide)+.5))+hframebuf_w;
	scry=(cast(signed_register_t)(tdist.dot(RenderCamera_VUp)+.5))+hframebuf_h;
	return ddist;
}

nothrow T Dist3D_To_2DCoord(T)(Vector_t!(3, T) vdist, ref T scrx, ref T scry){
	immutable ddist=vdist.dot(RenderCamera_VForward);
	immutable tdist=vdist/ddist;
	scrx=tdist.dot(RenderCamera_VSide)+fhframebuf_w;
	scry=tdist.dot(RenderCamera_VUp)+fhframebuf_h;
	return ddist;
}

void Renderer_Draw3DParticle(alias hole_side=false)(immutable in float x, immutable in float y, immutable in float z,
immutable in RendererParticleSize_t w, immutable in RendererParticleSize_t h, immutable in RendererParticleSize_t l, uint col){
	float dist;
	signed_register_t scrx, scry, hw=w>>1, hh=h>>1;
	Project2D(x, y, z, scrx, scry, dist);
	if(scrx<hw || scry<hh || scrx>=vxrend_framebuf_w-hw || scry>=vxrend_framebuf_h-hh || dist<1.0 || dist>Fog_AlphaValues.length)
		return;
	immutable fog_color_mod_alpha=Fog_AlphaValues[cast(size_t)dist];
	immutable inv_fog_color_mod_alpha=255-fog_color_mod_alpha;
	immutable fog_ccomp1=((col&0x00ff00ff)*inv_fog_color_mod_alpha+Fog_AlphaColorComponent1*fog_color_mod_alpha)>>>8;
	immutable fog_ccomp2=((col&0x0000ff00)*inv_fog_color_mod_alpha+Fog_AlphaColorComponent2*fog_color_mod_alpha)>>>8;
	col=(fog_ccomp1&0x00ff00ff) | (fog_ccomp2&0x0000ff00);
	Renderer_FillRect3D(scrx, scry, w/to!int(dist)+1, h/to!int(dist)+1, col|0xff000000, dist*dist);
}

alias Renderer_DrawWireframe=Renderer_DrawSprite;

string __mixin_CTArgFuncList(alias funcstart, alias cnt)(){
	string ret="[";
	for(uint i=0; i<(1<<cnt); i++){
		ret~=funcstart;
		ret~="!(";
		for(uint j=0; j<cnt; j++){
			ret~=(i&(1<<j)) ? "true" : "false";
			if(j!=cnt-1)
				ret~=",";
		}
		ret~=")";
		if(i!=(1<<cnt)-1)
			ret~=",";
	}
	ret~="]";
	return ret;
}

void Renderer_DrawSprite(SpriteRenderData_t *sprrend, Vector3_t pos, Vector3_t rotation){
	Sprite_t spr;
	spr.model=sprrend.model;
	spr.pos=pos; spr.rot=rotation; spr.density=sprrend.size/Vector3_t(spr.model.size);
	spr.color_mod=sprrend.color_mod; spr.replace_black=sprrend.replace_black;
	spr.check_visibility=sprrend.check_visibility; spr.motion_blur=to!ubyte(sprrend.motion_blur*255.0);
	return Renderer_DrawSprite(spr);
}

void Renderer_DrawSprite(in Sprite_t spr){
	if(!Sprite_Visible(spr) && spr.check_visibility)
		return;
	Vector3_t sprpos=Vector3_t(spr.xpos, spr.ypos, spr.zpos);
	float mindist=float.infinity;
	ubyte brightness=0;
	if(Config_Read!bool("gun_flashes") || Config_Read!bool("explosion_flashes")){
		foreach(ref flash; Flashes){
			float dist=(flash.centre-sprpos).length;
			if(dist<flash.radius && dist<mindist){
				brightness=to!ubyte((1.0-dist/flash.radius)*127.5f+127.5f);
				mindist=dist;
			}
		}
	}
	bool motion_blur=spr.motion_blur>0.0;
	auto render_sprite_funcs=mixin(__mixin_CTArgFuncList!("&_Render_Sprite", 4)());
	render_sprite_funcs[(spr.replace_black!=0)|((spr.color_mod!=0)<<1)|((brightness!=0)<<2)|(motion_blur<<3)](cast(immutable)spr, brightness);
}

version(DigitalMars){
	@nogc pragma(inline, true):
}
nothrow void _Render_Sprite(alias Enable_Black_Color_Replace, alias Enable_Color_Mod, alias Brighten, alias Motion_Blur)(immutable in Sprite_t spr, immutable in ubyte brightness){
	float modeldist;
	{
		try{
			if(!Sprite_Visible(spr))
				return;
		}catch(Exception){}
		//Change this and make it consider ydiff too when not using Voxlap
		modeldist=(RenderCameraPos-spr.pos).length-(Vector3_t(spr.model.size)*spr.density).length*sqrt(2.0);
		if(modeldist>Fog_AlphaValues.length)
			return;
		if(!spr.density.x || !spr.density.y || !spr.density.z)
			return;
	}
	immutable uint blockadvance=cast(uint)(modeldist*modeldist/(VoxlapInterface.maxscandist*VoxlapInterface.maxscandist)*VoxlapInterface.anginc*2.0f)+1;
	immutable int screen_w=vxrend_framebuf_w, screen_h=vxrend_framebuf_h;
	immutable lod_vx_size=pow(blockadvance, .75);
	immutable MVRectSize=Vector_t!(2, real)(screen_w*XFOV_Ratio, screen_h*YFOV_Ratio)/2.0*1.45*lod_vx_size*spr.density.length;
	immutable renderrot=Vector_t!(3, real)(spr.rot.x, -(spr.rot.y+90.0), -spr.rot.z);
	immutable uint fog_color_component1=Fog_AlphaColorComponent1, fog_color_component2=Fog_AlphaColorComponent2;
	immutable real inv_maxscandist=255.0/(VoxlapInterface.maxscandist*VoxlapInterface.maxscandist);
	static if(Enable_Color_Mod){
		immutable uint color_mod_alpha=(spr.color_mod>>24)&255;
		immutable uint color_mod_inv_alpha=255-color_mod_alpha;
		immutable uint color_mod_component1=(((spr.color_mod>>16)&255)<<16) | ((spr.color_mod>>0)&255)
		, color_mod_component2=((spr.color_mod>>8)&255)<<8;
	}
	immutable fog_alpha=cast(immutable ubyte*)Fog_AlphaValues.ptr;
	immutable max_visdist=Fog_AlphaValues.length;
	const ubyte* framebuf=cast(const ubyte*)vxrend_framebuf_pixels;
	immutable zbufoff=VoxlapInterface.zbufoff, pitch=vxrend_framebuf_pitch;
	immutable auto campos=CameraPos;
	ubyte visxbit=3, visybit=3, viszbit=3;
	if(spr.model.voxels.length>3000){
		if(spr.pos.x<CameraPos.x)
			visxbit=RenderCamera_VForward.x>=0.0 ? 2 : 1;
		if(spr.pos.x>CameraPos.x)
			visxbit=RenderCamera_VForward.x>=0.0 ? 1 : 2;
		if(spr.pos.y<CameraPos.y)
			visybit=RenderCamera_VForward.y>=0.0 ? 2 : 1;
		if(spr.pos.y>CameraPos.y)
			visybit=RenderCamera_VForward.y>=0.0 ? 1 : 2;
		if(spr.pos.z<CameraPos.z)
			viszbit=RenderCamera_VForward.z>=0.0 ? 2 : 1;
		if(spr.pos.z>CameraPos.z)
			viszbit=RenderCamera_VForward.z>=0.0 ? 1 : 2;
	}
	immutable ubyte visbit=cast(ubyte)(visxbit | (visybit<<4) | (viszbit<<2));
	immutable spr_edges=spr.Edge_Vectors();
	immutable minpos=spr_edges[0];
	immutable xdiff=spr_edges[1]*lod_vx_size/cast(real)spr.model.size.x, ydiff=spr_edges[2]*lod_vx_size/cast(real)spr.model.size.y,
	zdiff=spr_edges[3]*lod_vx_size/cast(real)spr.model.size.z;
	Vector_t!(3, real) vxpos=minpos+xdiff*.5+ydiff*.5+zdiff*.5-RenderCameraPos;
	for(uint blkx=0; blkx<spr.model.xsize; blkx+=blockadvance, vxpos+=xdiff){
		Vector_t!(3, real) vzpos=0.0;
		for(uint blkz=0; blkz<spr.model.zsize; blkz+=blockadvance, vzpos+=zdiff){
			immutable vxzpos=vxpos+vzpos;
			immutable rowlen=cast(register_t)spr.model.column_lengths[blkx+blkz*spr.model.xsize];
			if(!rowlen)
				continue;
			immutable mvox=&spr.model.voxels[spr.model.offsets[blkx+blkz*spr.model.xsize]];
			immutable real row_start_mvy=mvox[0].ypos, row_end_mvy=mvox[rowlen-1].ypos;
			real startscreenx, startscreeny;
			immutable startrenddist=Dist3D_To_2DCoord(vxzpos+ydiff*row_start_mvy, startscreenx, startscreeny);
			real endscreenx, endscreeny;
			immutable endrenddist=Dist3D_To_2DCoord(vxzpos+ydiff*row_end_mvy, endscreenx, endscreeny);
			if((startscreenx<0 || startscreenx>=screen_w) && (startscreeny<0 || startscreeny>=screen_h)){
				if((endscreenx<0 || endscreenx>=screen_w) && (endscreeny<0 || endscreeny>=screen_h)){
					continue;
				}
			}
			if(endrenddist<.01f || startrenddist<.01f || (startrenddist!=startrenddist) || (endrenddist!=endrenddist))
				continue;
			immutable screencoord_diff=Vector_t!(3, real)(endscreenx-startscreenx, endscreeny-startscreeny, endrenddist-startrenddist);
			immutable screencoord_base=Vector_t!(3, real)(startscreenx, startscreeny, startrenddist);
			immutable inv_row_end_mvy=1.0/max(row_end_mvy-row_start_mvy, 1);
			immutable startvoxdist=Vector_t!(3, real)(vxzpos+ydiff*mvox[0].ypos).length;
			immutable voxdistdiff=(Vector_t!(3, real)(vxzpos+ydiff*mvox[rowlen-1].ypos).length-startvoxdist)*inv_row_end_mvy;
			for(uint blkind=0; blkind<rowlen; blkind+=blockadvance){	
				if(!(mvox[blkind].visiblefaces&visbit))
					continue;
				uint vxcolor=mvox[blkind].color;
				immutable y_posratio=(mvox[blkind].ypos-row_start_mvy)*inv_row_end_mvy;
				immutable register_t vxdist=cast(register_t)(startvoxdist+voxdistdiff*y_posratio);
				//We don't calculate the data for each voxel anymore, we calculate the data for the first and last of the row, then interpolate inbetween
				/*immutable voxpos=vxzpos+ydiff*mvox[blkind].ypos;
				immutable uint vxdist=cast(immutable uint)(Vector_t!(3, real)(voxpos).sqlength);*/
				if(vxdist>=max_visdist)
					continue;
				/*signed_register_t screenx, screeny;
				immutable auto renddist=Dist3D_To_2DCoord(voxpos, screenx, screeny);*/
				immutable screencoord=screencoord_base+screencoord_diff*y_posratio;
				signed_register_t rect_start_x=cast(signed_register_t)(screencoord.x+.4);
				signed_register_t rect_start_y=cast(signed_register_t)(screencoord.y+.4);
				immutable renddist=screencoord.z;
				if(rect_start_x>=screen_w || rect_start_y>=screen_h)
					continue;
				immutable inv_renddist=1.0/renddist;
				immutable rect_size=MVRectSize*inv_renddist+.5;
				immutable rect_w=(cast(signed_register_t)(rect_size.x))+1;
				immutable rect_h=(cast(signed_register_t)(rect_size.y))+1;
				rect_start_x-=rect_w>>1; rect_start_y-=rect_h>>1;
				immutable rect_end_x=min(rect_start_x+rect_w, screen_w-1);
				immutable rect_end_y=min(rect_start_y+rect_h, screen_h-1);
				rect_start_x=max(rect_start_x, 0); rect_start_y=max(rect_start_y, 0);
				if(rect_end_x<=rect_start_x || rect_end_y<=rect_start_y)
					continue;
				static if(Enable_Black_Color_Replace){
					vxcolor=((vxcolor&0x00ffffff)!=0x00040404) ? vxcolor : spr.replace_black;
				}
				static if(Enable_Color_Mod){{
					immutable cmod_ccomp1=((vxcolor&0x00ff00ff)*color_mod_inv_alpha+color_mod_component1*color_mod_alpha)>>>8;
					immutable cmod_ccomp2=((vxcolor&0x0000ff00)*color_mod_inv_alpha+color_mod_component2*color_mod_alpha)>>>8;
					vxcolor=(cmod_ccomp1&0x00ff00ff) | (cmod_ccomp2&0x0000ff00);
				}}
				static if(Brighten){{
					ushort[4] carr=[vxcolor&255, (vxcolor>>>8)&255, (vxcolor>>>16)&255, 0];
					carr[]*=brightness;
					carr[]/=128;
					vxcolor=(carr[0]&0xff00 ? 255 : carr[0]) | ((carr[1]&0xff00 ? 255 : carr[1])<<8) |
					((carr[2]&0xff00 ? 255 : carr[2])<<16);
				}}
				{
					immutable fog_color_mod_alpha=fog_alpha[vxdist];
					immutable inv_fog_color_mod_alpha=255-fog_color_mod_alpha;
					immutable fog_ccomp1=((vxcolor&0x00ff00ff)*inv_fog_color_mod_alpha+fog_color_component1*fog_color_mod_alpha)>>>8;
					immutable fog_ccomp2=((vxcolor&0x0000ff00)*inv_fog_color_mod_alpha+fog_color_component2*fog_color_mod_alpha)>>>8;
					vxcolor=(fog_ccomp1&0x00ff00ff) | (fog_ccomp2&0x0000ff00);
				}
				static if(!Motion_Blur)
					vxcolor|=0xff000000;
				else
					vxcolor|=(255-spr.motion_blur)<<24;
				Renderer_SpriteFillRect3D(rect_start_x, rect_start_y, rect_end_x, rect_end_y, vxcolor, renddist*renddist, framebuf, pitch, screen_w, screen_h, zbufoff);
			}
		}
	}
}

//After making it pure I learned that I have literally nothing from it lmao
version(DigitalMars){
	@nogc pragma(inline, true):
}
pure nothrow void Renderer_SpriteFillRect3D(
signed_register_t xpos, signed_register_t ypos, signed_register_t xpos2, signed_register_t ypos2, immutable in uint col, immutable in float dist,
const ubyte* pixels=cast(const ubyte*)vxrend_framebuf_pixels, 
immutable in register_t pitch=vxrend_framebuf_pitch, immutable in register_t fb_w=vxrend_framebuf_w, immutable in register_t fb_h=vxrend_framebuf_h,
immutable in signed_register_t zbufoff=VoxlapInterface.zbufoff){
	uint *pixelptr=cast(uint*)(pixels+ypos*pitch+((xpos-1)<<2));
	float *zbufptr=cast(float*)((cast(ubyte*)pixelptr)+zbufoff);
	immutable w=xpos2-xpos, h=ypos2-ypos;
	static if(!AssemblerCode_Enabled){
		for(register_t y=h; y; y--){
			register_t x=w;
			//You can't make this faster, they said. "for(...){if(zbufptr[x]>=dist){...}}" is the fastest possible implementation, they said.
			while(x){
				while(zbufptr[x]>=dist && x){
					zbufptr[x]=dist;
					pixelptr[x]=col;
					--x;
				}
				while(zbufptr[x]<dist && x){--x;}
			}
			zbufptr=cast(float*)(cast(ubyte*)zbufptr+pitch);
			pixelptr=cast(uint*)(cast(ubyte*)pixelptr+pitch);
		}
	}
	else{
		//My horrible ASM code :d
		//(btw fuck emms, apparently no compiler ever needs it (or apparently you don't need it for SSE/SSE2 stuff anymore) :d)
		//(late edit: Nah, apparently the compiler just uses SSE2 XMM registers instead of the float stack for all float stuff so it doesn't matter anyways)
		mixin(AssemblerCode_BlockStart~"
			mov EAX, zbufptr;
			mov EBX, pixelptr;
			sub EBX, EAX;
			mov ECX, h;
			mov EDI, pitch;
			mov ESI, w;
			shl ESI, 2;
			movd XMM1, dist;
			movd XMM2, col;
			Y_LOOP_START:;
				mov EDX, EAX;
				prefetchnta [EAX];
				add EAX, ESI;
				X_LOOP1_START:;
					movd XMM0, [EAX];
					ucomiss XMM0, XMM1;
					jb X_LOOP2_JUMPIN;
					X_LOOP1_JUMPIN:;
					movd [EAX], XMM1;
					movd [EAX+EBX], XMM2;
				add EAX, -4;
				cmp EAX, EDX;
				jnz X_LOOP1_START;
				jmp X_LOOP2_END;
				X_LOOP2_START:;
					movd XMM0, [EAX];
					ucomiss XMM0, XMM1;
					jae X_LOOP1_JUMPIN;
					X_LOOP2_JUMPIN:;
				add EAX, -4;
				cmp EAX, EDX;
				jnz X_LOOP2_START;
				X_LOOP2_END:;
				add EAX, EDI;
			loop Y_LOOP_START;};
		");
	}
}

version(DigitalMars){
	@nogc pragma(inline, true):
}
pure nothrow void Renderer_FillRect3D(alias Check_Coords=true)(signed_register_t xpos, signed_register_t ypos, signed_register_t w, signed_register_t h, immutable in uint col, immutable in float dist,
const ubyte* pixels=cast(const ubyte*)vxrend_framebuf_pixels, 
immutable in uint pitch=vxrend_framebuf_pitch, immutable in uint fb_w=vxrend_framebuf_w, immutable in uint fb_h=vxrend_framebuf_h,
immutable in int zbufoff=VoxlapInterface.zbufoff){
	static if(Check_Coords){
		if(xpos<0){
			w+=xpos; xpos=0;
		}
		if(ypos<0){
			h+=ypos; ypos=0;
		}
		if(xpos+w>=fb_w)
			w=fb_w-xpos;
		if(ypos+h>=fb_h)
			h=fb_h-ypos;
		if(!w || !h)
			return;
	}
	uint *pixelptr=cast(uint*)(pixels+ypos*pitch+((xpos-1)<<2));
	float *zbufptr=cast(float*)((cast(ubyte*)pixelptr)+zbufoff);
	static if(!AssemblerCode_Enabled){
		for(register_t y=h; y; y--){
			register_t x=w;
			//You can't make this faster, they said. "for(...){if(zbufptr[x]>=dist){...}}" is the fastest possible implementation, they said.
			while(x){
				while(zbufptr[x]>=dist && x){
					zbufptr[x]=dist;
					pixelptr[x]=col;
					--x;
				}
				while(zbufptr[x]<dist && x){--x;}
			}
			zbufptr=cast(float*)(cast(ubyte*)zbufptr+pitch);
			pixelptr=cast(uint*)(cast(ubyte*)pixelptr+pitch);
		}
	}
	else{
		//My horrible ASM code :d
		//(btw fuck emms, apparently no compiler ever needs it (or apparently you don't need it for SSE/SSE2 stuff anymore) :d)
		//(late edit: Nah, apparently the compiler just uses SSE2 XMM registers instead of the float stack for all float stuff so it doesn't matter anyways)
		mixin(AssemblerCode_BlockStart~"
			mov EAX, zbufptr;
			mov EBX, pixelptr;
			sub EBX, EAX;
			mov ECX, h;
			mov EDI, pitch;
			mov ESI, w;
			shl ESI, 2;
			movd XMM1, dist;
			movd XMM2, col;
			Y_LOOP_START:;
				mov EDX, EAX;
				prefetchnta [EAX];
				add EAX, ESI;
				X_LOOP1_START:;
					movd XMM0, [EAX];
					ucomiss XMM0, XMM1;
					jb X_LOOP2_JUMPIN;
					X_LOOP1_JUMPIN:;
					movd [EAX], XMM1;
					movd [EAX+EBX], XMM2;
				add EAX, -4;
				cmp EAX, EDX;
				jnz X_LOOP1_START;
				jmp X_LOOP2_END;
				X_LOOP2_START:;
					movd XMM0, [EAX];
					ucomiss XMM0, XMM1;
					jae X_LOOP1_JUMPIN;
					X_LOOP2_JUMPIN:;
				add EAX, -4;
				cmp EAX, EDX;
				jnz X_LOOP2_START;
				X_LOOP2_END:;
				add EAX, EDI;
			loop Y_LOOP_START;};
		");
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
	signed_register_t sx, sy;
	if(!Project2D(xpos, ypos, zpos, sx, sy))
		return;
	if(dist>VoxlapInterface.maxscandist)
		return;
	immutable signed_register_t w=radius*2, h=radius*2;
	immutable signed_register_t fb_w=vxrend_framebuf_w, fb_h=vxrend_framebuf_h;
	immutable signed_register_t renderxpos=sx, renderypos=sy;
	if(renderxpos+w<0 || renderypos+h<0 || renderxpos>=fb_w+w || renderypos>=fb_h+h)
		return;
	immutable uint neg_alpha=255-alpha;
	immutable uint cr=((color>>16)&255)*alpha, cg=((color>>8)&255)*alpha, cb=((color>>0)&255)*alpha;
	immutable signed_register_t pow_r=radius*radius;
	immutable signed_register_t min_y=renderypos<0 ? -renderypos : 0, max_y=renderypos+h<fb_h ? h : fb_h-renderypos-1;
	immutable signed_register_t min_w=renderxpos<fb_w ? (fb_w-renderxpos) : -(renderxpos-fb_w);
	if(min_w>fb_w)
		return;
	if(renderxpos>fb_w)
		return;
	if(dist<.1)
		return;
	uint *pty=cast(uint*)((cast(ubyte*)(vxrend_framebuf_pixels))+(renderxpos<<2)+((renderypos+min_y)*vxrend_framebuf_pitch));
	float *zbufptr=cast(float*)((cast(ubyte*)pty)+VoxlapInterface.zbufoff);
	int zbufdiff=VoxlapInterface.zbufoff;
	void *pixels=vxrend_framebuf_pixels;
	immutable uint uiradius=radius;
	if(smoke_circle_tex){
		immutable uint[2][] __check_zbuf_pos=[
			[0u, 0u], [uiradius*2, 0u], [0u, uiradius*2u], [uiradius*2u, uiradius*2u], [uiradius, uiradius]
		];
		bool __no_zbuf_needed=true;
		foreach(pos; __check_zbuf_pos){
			if(renderxpos+pos[0]>=fb_w || renderxpos+pos[0]<0 || renderypos+pos[1]>=fb_h || renderypos+pos[1]<0)
				continue;
			if(zbufptr[renderxpos+pos[0]+(((renderypos+pos[1])*fb_w)>>2)]<dist){
				__no_zbuf_needed=false;
				break;
			}
		}
		if(__no_zbuf_needed){
			RendererSmokeCircleQueue_t c;
			c.rect=SDL_Rect(cast(int)(renderxpos-radius), cast(int)renderypos, radius*2, radius*2); c.icol=color; c.alpha=to!ubyte(alpha);
			RendererSmokeCircleQueue~=c;
			return;
		}
	}
	immutable uint color_ccomp1=(color&0x00ff00ff)*alpha, color_ccomp2=(color&0x0000ff00)*alpha;
	immutable uint fb_p=vxrend_framebuf_pitch;
	immutable sqdist=dist*dist;
	for(signed_register_t y=min_y; y<max_y;y++){
		if(y<min_y)
			continue;
		immutable signed_register_t cy=y-uiradius;
		immutable signed_register_t sqhwidth=pow_r-cy*cy;
		if(sqhwidth<=0)
			continue;
		immutable signed_register_t hwidth=int_sqrt!signed_register_t(sqhwidth);
		//immutable signed_register_t lwidth=min(hwidth, renderxpos), rwidth=min(hwidth, min_w);
		immutable signed_register_t lwidth=bitwise_min(hwidth, renderxpos), rwidth=bitwise_min(hwidth, min_w);
		//Safe, the second faster version needs approval in testing
		/*signed_register_t x=-lwidth;
		while(x<rwidth){
			while(zbufptr[x]>=dist && x<rwidth){
				zbufptr[x]=dist;
				pty[x]=0xff000000 | ((((pty[x]&0x00ff00ff)*neg_alpha+color_ccomp1)>>>8)&0x00ff00ff)
				| ((((pty[x]&0x0000ff00)*neg_alpha+color_ccomp2)>>>8)&0x0000ff00);
				++x;
			}
			while(zbufptr[x]<dist && x<rwidth){++x;}
		}*/
		signed_register_t x=lwidth+rwidth;
		auto zptr=&zbufptr[-lwidth];
		auto pptr=&pty[-lwidth];
		while(x){
			while(zptr[x]>=sqdist && x){
				zptr[x]=sqdist;
				pptr[x]=0xff000000 | ((((pptr[x]&0x00ff00ff)*neg_alpha+color_ccomp1)>>>8)&0x00ff00ff)
				| ((((pptr[x]&0x0000ff00)*neg_alpha+color_ccomp2)>>>8)&0x0000ff00);
				--x;
			}
			while(zptr[x]<sqdist && x){--x;}
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
	__Renderer_SetCam();
	float scope_dist;
	signed_register_t[2] rscope_2D_pos=Project2D(scope_pos.x, scope_pos.y, scope_pos.z, &scope_dist);
	int[2] scope_2D_pos=[cast(int)rscope_2D_pos[0], cast(int)rscope_2D_pos[1]];
	scope_2D_pos[]=scope_2D_pos[]*[cast(int)ScreenXSize, cast(int)ScreenYSize]/[vxrend_framebuf_w, vxrend_framebuf_h];
	xzoom*=1.0+scope_dist; yzoom*=1.0+scope_dist;
	immutable uint scope_xsize=min(cast(uint)(scope_picture.xsize*3.0/(1.0+scope_dist)), ScreenXSize*9/10),
	scope_ysize=min(cast(uint)(scope_picture.ysize*3.0/(1.0+scope_dist)), ScreenYSize*9/10);
	scope_2D_pos[0]-=scope_xsize>>1; scope_2D_pos[1]-=scope_ysize>>1;
	struct return_type{
		SDL_Rect dstrect;
		SDL_Rect srcrect;
		SDL_Texture *scope_texture;
		uint scope_texture_width, scope_texture_height;
	}
	if(scope_2D_pos[0]+scope_xsize<0 || scope_2D_pos[1]+scope_ysize<0 || scope_2D_pos[0]>=ScreenXSize || scope_2D_pos[1]>=ScreenYSize)
		return return_type();
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
	uint old_fog_dist=VoxlapInterface.maxscandist;
	Renderer_SetFog(VoxlapInterface.fogcol, cast(uint)(VoxlapInterface.maxscandist*1.25));
	Renderer_StartRendering(true);
	Renderer_SetCamera(scope_rot.y, scope_rot.x, scope_rot.z, X_FOV/xzoom/(tofloat(ScreenXSize)/scope_xsize), Y_FOV/yzoom/(tofloat(ScreenYSize)/scope_ysize), scope_pos.x, scope_pos.y, scope_pos.z);
	Do_Sprite_Visibility_Checks=true;
	Render_World!false(false);
	Renderer_SetFog(VoxlapInterface.fogcol, old_fog_dist);
	vxrend_framebuf_pixels=cast(int*)vxrend_framebuf.pixels; vxrend_framebuf_pitch=vxrend_framebuf.pitch;
	vxrend_framebuf_w=vxrend_framebuf.w; vxrend_framebuf_h=vxrend_framebuf.h;
	Renderer_StartRendering(true);
	__Renderer_SetCam();
	immutable float pow_w=scope_xsize*scope_xsize/2.0f/2.0f;
	immutable float x_y_ratio=to!float(scope_xsize)/to!float(scope_ysize);
	uint *scope_surface_ptr=cast(uint*)scope_texture_pixels;
	for(int circle_y=-cast(int)((scope_ysize>>1)); circle_y<=cast(int)(scope_ysize>>1); circle_y++){
		float circle_x=circle_y*x_y_ratio;
		float powdist=pow_w-circle_x*circle_x;
		if(powdist<0.0f) //Hack to fix some float bug that makes pow_w<circle_x*circle_x despite the numbers being equal
			powdist=0.0f;
		immutable uint width=(cast(uint)sqrt(powdist))<<1;
		immutable uint sx=(scope_xsize-width)>>>1;
		{
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
	ret.dstrect.x=scope_2D_pos[0]; ret.dstrect.y=scope_2D_pos[1];
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
