SRCFILES = ./vxwclient-voxlap-renderer/*.d ./vxwclient-voxlap-renderer/*.di *.d *.di
RENDERER_LIB_OBJ=./vxwclient-voxlap-renderer/voxlap5.o
RENDERER_LIB_LL=./vxwclient-voxlap-renderer/voxlap5.ll
RENDERER_LIB_SRC=./vxwclient-voxlap-renderer/voxlap5.c
DEPENDENCIES=./derelict/enet/*.d ./derelict/openal/*.d ./derelict/vorbis/*.d ./derelict/ogg/*.d ./derelict/util/*.d ./SDLang2/sdl2.di
DFLAGS=-g
LDFLAGS=-lslang
TARGET_ARCH=$(shell getconf LONG_BIT)
CRIPPLED_DEBIAN=Debian GNU/Linux 9 \n \l
ARCH_EXTENSIONS=mmx sse sse2 sse3 #sse4.1 ssse3 cx16 cmov
LDC_ARCH_EXT=$(shell arch_ext=; for i in $(ARCH_EXTENSIONS); do arch_ext="$$arch_ext -mattr=$$i"; done; echo $$arch_ext)
GDC_ARCH_EXT=$(shell arch_ext=; for i in $(ARCH_EXTENSIONS); do arch_ext="$$arch_ext -m$$i"; done; echo $$arch_ext)

default:
ifeq ($(shell cat /etc/issue), $(CRIPPLED_DEBIAN))
	make crippled_dmd_linker
else
	make dmd
endif

dmd: $(RENDERER_LIB_OBJ) $(RENDERER_LIB_SRC)
	dmd -m$(TARGET_ARCH) $(DFLAGS)  $(RENDERER_LIB_OBJ) -unittest $(SRCFILES)  $(DEPENDENCIES) -L-L/usr/local/lib -L-L. -L-lSDL2 -L-lSDL2_image -L-lslang -ofmain.o -c

ldc: $(RENDERER_LIB_OBJ) $(RENDERER_LIB_SRC)
	ldc2 -O5 -g $(LDC_ARCH_EXT) -m$(TARGET_ARCH) $(DFLAGS) \
	-singleobj -release $(SRCFILES) $(DEPENDENCIES) $(RENDERER_LIB_OBJ) -gc -L-L/usr/local/lib -L-lslang -L-lSDL2 -L-lSDL2_image -ofmain

gdc: $(RENDERER_LIB_OBJ) $(RENDERER_LIB_SRC)
	gdc -Ofast -pg -march=native $(GDC_ARCH_EXT) -m$(TARGET_ARCH) $(DFLAGS) $(SRCFILES) $(DEPENDENCIES) $(RENDERER_LIB_OBJ) \
	-Wl,-ldl -Wl,-L/usr/local/lib -Wl,-lslang -omain

ll: $(RENDERER_LIB_LL) $(RENDERER_LIB_SRC)
	ldc2 -m$(TARGET_ARCH) -O5 -partially-inline-libcalls -singleobj -release -output-ll $(SRCFILES) -ofmain.ll
	#20% done
	llvm-link main.ll $(RENDERER_LIB_LL) -S -o client.ll
	#40% done
	opt -O3 -inline-threshold=1000 client.ll -S -o client_opt.ll
	#60% done
	llc -O3 -inline-threshold=100000 client_opt.ll -filetype=obj -o main.o
	#80% done
	gcc -m$(TARGET_ARCH) -pg -march=native -Ofast main.o -o main $(LDFLAGS) -lphobos2-ldc -Wl,-Ofast -ldruntime-ldc -ldl -lpthread -lm

derelict_obj.o:
	ldc2 -m$(TARGET_ARCH) -singleobj -release $(DEPENDENCIES) -c -ofderelict_obj.o

#Latest debian breaks DMD-compiled executables (PIE/no-PIE crisscross blowing up), so we just compile with DMD then link manually
#Also, supposedly one should be able to change his linker by setting the CC global variable, but DMD couldn't give less of a shit about that global variable
crippled_dmd_linker:
	dmd -m$(TARGET_ARCH) $(DFLAGS)  $(RENDERER_LIB_OBJ) -unittest $(SRCFILES)  $(DEPENDENCIES) -ofmain.o -c
	gcc-4.8 ./vxwclient-voxlap-renderer/voxlap5.o main.o -o main -g -m32 -L/usr/local/lib -L. -lSDL2 -lSDL2_image \
	-lslang -L/usr/lib/i386-linux-gnu -Xlinker --export-dynamic -Xlinker -Bstatic -lphobos2 -Xlinker -Bdynamic -lpthread -lm -lrt -ldl

#Instead of "*"-functionality, each source file has to be passed; .obj files instead of .o because optlink can't even recognize file type by looking at its content
#(not even talking of HOW to get OMF object files on wine)
crippled_windows:
	SRCFILES=gfx.d main.d misc.d snd.d network.d packettypes.d protocol.d ui.d modlib.d vector.d world.d renderer_templates.d windows_pthread.d vxwclient-voxlap-renderer/renderer.d \
	script.d slang.di vxwclient-voxlap-renderer/voxlap.di ./derelict/sdl2/functions.d ./derelict/sdl2/image.d ./derelict/sdl2/mixer.d ./derelict/sdl2/net.d ./derelict/sdl2/sdl.d ./derelict/sdl2/ttf.d \
	./derelict/sdl2/types.d ./derelict/enet/enet.d ./derelict/enet/funcs.d ./derelict/enet/types.d ./derelict/util/exception.d ./derelict/util/loader.d ./derelict/util/sharedlib.d ./derelict/util/system.d \
	./derelict/util/wintypes.d ./derelict/util/xtypes.d ./derelict/ogg/ogg.d ./derelict/vorbis/enc.d ./derelict/vorbis/file.d ./derelict/vorbis/vorbis.d ./derelict/openal/al.d
	TARGET_ARCH=32
	RENDERER_LIB_OBJ=vxwclient-voxlap-renderer/voxlap5.obj
	OTHER_OBJ=libslang.lib pthread.lib missing_symbols.obj
	dmd -inline $(SRCFILES) -m$(TARGET_ARCH) $(DFLAGS) $(RENDERER_LIB_OBJ) $(OTHER_OBJ) -ofmain.exe

$(RENDERER_LIB_OBJ):
	cd vxwclient-voxlap-renderer; make -f make_voxlap_renderer voxlap5.o

$(RENDERER_LIB_LL):
	cd vxwclient-voxlap-renderer; make -f make_voxlap_renderer voxlap5.ll

.PHONY: clean
clean:
	rm *.o *.ll

.PHONY: distclean
distclean:
	rm *.o main *.ll