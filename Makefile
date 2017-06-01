SRCFILES = ./vxwclient-voxlap-renderer/*.d ./vxwclient-voxlap-renderer/*.di ./derelict/sdl2/*.d ./derelict/enet/*.d ./derelict/openal/*.d ./derelict/vorbis/*.d ./derelict/ogg/*.d ./derelict/util/*.d *.d *.di
RENDERER_LIB_OBJ=./vxwclient-voxlap-renderer/voxlap5.o
RENDERER_LIB_LL=./vxwclient-voxlap-renderer/voxlap5.ll
RENDERER_LIB_SRC=./vxwclient-voxlap-renderer/voxlap5.c
DFLAGS=-g
LDFLAGS=-lslang
TARGET_ARCH=$(shell getconf LONG_BIT)

dmd: $(RENDERER_LIB_OBJ) $(RENDERER_LIB_SRC)
	dmd -m$(TARGET_ARCH) $(DFLAGS)  $(RENDERER_LIB_OBJ) -unittest $(SRCFILES) -L-L/usr/local/lib -L-L. -L-lslang -ofmain -L-no-pie -v

ldc: $(RENDERER_LIB_OBJ) $(RENDERER_LIB_SRC)
	ldc2 -O5 -mattr=mmx -mattr=sse -mattr=sse2 -mattr=sse3 -m$(TARGET_ARCH) $(DFLAGS) -singleobj -release -inline $(SRCFILES) $(RENDERER_LIB_OBJ) -gc -L-L/usr/local/lib -L-lslang -ofmain

gdc: $(RENDERER_LIB_OBJ) $(RENDERER_LIB_SRC)
	gdc  -pg -Ofast -march=native -mmmx -msse -msse2 -msse3 -m$(TARGET_ARCH) $(DFLAGS) $(SRCFILES) $(RENDERER_LIB_OBJ) -Wl,-ldl -Wl,-L/usr/local/lib -Wl,-lslang -omain

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