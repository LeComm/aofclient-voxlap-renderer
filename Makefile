SRCFILES = ./vxwclient-voxlap-renderer/*.d ./vxwclient-voxlap-renderer/*.di *.d *.di
RENDERER_LIB_OBJ=./vxwclient-voxlap-renderer/voxlap5.o
RENDERER_LIB_LL=./vxwclient-voxlap-renderer/voxlap5.ll
RENDERER_LIB_SRC=./vxwclient-voxlap-renderer/voxlap5.c
DEPENDENCIES=./derelict/enet/*.d ./derelict/openal/*.d ./derelict/vorbis/*.d ./derelict/ogg/*.d ./derelict/util/*.d ./SDLang2/sdl2.di
DFLAGS=-g -gc
LDFLAGS=-lslang -lSDL2 -lSDL2_image
TARGET_ARCH=$(shell getconf LONG_BIT)
CRIPPLED_DEBIAN=Debian GNU/Linux 9 \n \l
ARCH_EXTENSIONS=mmx sse sse2 sse3 #sse4.1 ssse3 cx16 cmov
LDC_ARCH_EXT=$(shell arch_ext=; for i in $(ARCH_EXTENSIONS); do arch_ext="$$arch_ext -mattr=$$i"; done; echo $$arch_ext)
GDC_ARCH_EXT=$(shell arch_ext=; for i in $(ARCH_EXTENSIONS); do arch_ext="$$arch_ext -m$$i"; done; echo $$arch_ext)
D_LDFLAGS=$(shell ld_flags=; for i in $(LD_FLAGS); do ld_flags="$$ld_flags -L$$i"; done; echo $$ld_flags)
C_LDFLAGS=$(LDFLAGS)

default:
ifeq ($(shell cat /etc/issue), $(CRIPPLED_DEBIAN))
	make crippled_dmd_linker
else
	make dmd
endif

dmd: $(RENDERER_LIB_OBJ) $(RENDERER_LIB_SRC)
	echo $(D_LDFLAGS)
	dmd -m$(TARGET_ARCH) $(DFLAGS)  $(RENDERER_LIB_OBJ) -unittest $(SRCFILES) $(DEPENDENCIES) -L-L/usr/local/lib -L-L. -L-lSDL2 -L-lSDL2_image -L-lslang -ofmain

ldc: $(RENDERER_LIB_OBJ) $(RENDERER_LIB_SRC)
	ldc2 $(LDC_ARCH_EXT) -m$(TARGET_ARCH) $(DFLAGS) \
	-singleobj $(SRCFILES) $(DEPENDENCIES) $(RENDERER_LIB_OBJ) -L-L/usr/local/lib -L-lslang -L-lSDL2 -L-lSDL2_image -ofmain

gdc: $(RENDERER_LIB_OBJ) $(RENDERER_LIB_SRC)
	gdc -Ofast -pg -march=native $(GDC_ARCH_EXT) -m$(TARGET_ARCH) $(DFLAGS) $(SRCFILES) $(DEPENDENCIES) $(RENDERER_LIB_OBJ) \
	-Wl,-ldl -Wl,-L/usr/local/lib -Wl,-lslang -omain

ll: $(RENDERER_LIB_LL) $(RENDERER_LIB_SRC)
	ldc2 -m$(TARGET_ARCH) -O5 -singleobj -release -output-ll $(SRCFILES) $(DEPENDENCIES) -ofmain.ll
	#20% done
	llvm-link-4.0 main.ll $(RENDERER_LIB_LL) -S -o client.ll
	#40% done
	opt-4.0 -O3 -strip -inline-threshold=1000 client.ll -S -o client_opt.ll
	#60% done
	llc-4.0 -O3 -inline-threshold=100000 client_opt.ll -filetype=obj -o main.o
	#80% done
	gcc-4.8 --release -m$(TARGET_ARCH) -pg -march=native -Ofast main.o -o main $(LDFLAGS) -lphobos2-ldc  -ldruntime-ldc -ldl -lpthread -lm

derelict_obj.o:
	ldc2 -m$(TARGET_ARCH) -singleobj -release $(DEPENDENCIES) -c -ofderelict_obj.o

#Latest debian breaks DMD-compiled executables (PIE/no-PIE crisscross blowing up), so we just compile with DMD then link manually
#Also, supposedly one should be able to change his linker by setting the CC global variable, but DMD couldn't give less of a shit about that global variable
crippled_dmd_linker: $(RENDERER_LIB_OBJ) $(RENDERER_LIB_SRC)
	dmd -m$(TARGET_ARCH) $(DFLAGS) $(RENDERER_LIB_OBJ) -unittest $(SRCFILES)  $(DEPENDENCIES) -ofmain.o -c
	gcc-4.8 $(RENDERER_LIB_OBJ) main.o -o main -g -m32 -L/usr/local/lib -L. -lSDL2 -lSDL2_image \
	-lslang -L/usr/lib/i386-linux-gnu -Xlinker --export-dynamic -Xlinker -Bstatic -lphobos2 -Xlinker -Bdynamic -lpthread -lm -lrt -ldl

#Instead of "*"-functionality, each source file has to be passed; .obj files instead of .o because optlink can't even recognize file type by looking at its content
#(not even talking of HOW TF to get OMF object files on wine)
crippled_windows:
	dmd -O -inline gfx.d main.d misc.d snd.d network.d packettypes.d protocol.d ui.d modlib.d vector.d world.d renderer_templates.d windows_pthread.d vxwclient-voxlap-renderer/renderer.d \
	script.d slang.di vxwclient-voxlap-renderer/voxlap.di ./derelict/enet/enet.d ./derelict/enet/funcs.d ./derelict/enet/types.d ./derelict/util/exception.d ./derelict/util/loader.d \
	./derelict/util/sharedlib.d ./derelict/util/system.d ./derelict/util/wintypes.d ./derelict/util/xtypes.d ./derelict/ogg/ogg.d ./derelict/vorbis/enc.d ./derelict/vorbis/file.d ./derelict/vorbis/vorbis.d \
	./derelict/openal/al.d ./SDLang2/sdl2.di vxwclient-voxlap-renderer/voxlap5.obj libslang.lib pthread.lib missing_symbols.obj SDL2.lib SDL2_image.lib -ofmain.exe

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