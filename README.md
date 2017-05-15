This software is based on:
https://github.com/Ericson2314/Voxlap

Original Voxlap Engine non-commercial license:
------------------------------------------------------------------------------

1. Any derivative works based on Voxlap may be distributed as long as it is
   free of charge and through noncommercial means.

2. You must give me proper credit. This line of text is sufficient:

 > VOXLAP engine by Ken Silverman (http://advsys.net/ken)
 
 > Make sure it is clearly visible somewhere in your archive.

3. If you wish to release modified source code to your game, please add the
   following line to each source file changed:

   `// This file has been modified from Ken Silverman's original release`

4. I am open to commercial applications based on Voxlap, however you must
   consult with me first to acquire a commercial license. Using Voxlap as a
   test platform or as an advertisement to another commercial game is
   commercial exploitation and prohibited without a commercial license.

Usage:
DMD/GDC/LDC with object files (Standard):
	Put Makefile into vxw-client source directory. Type "make -f make_voxlap_renderer". Go to vxw-client source directory, type "make". (For GDC or LDC, type "make gdc"/"make ldc").
LDC with LLVM LTO (For FPS increase by 1/5):
	Put Makefile into vxw-client source directory. Type "make -f make_voxlap_renderer voxlap5.ll". Go to vxw-client source directory, type "make ll".