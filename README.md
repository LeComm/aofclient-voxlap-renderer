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


Compiling:
	GCC:
		./compile_gcc_opt or ./compile_gcc_noopt (opt = optimize)
	clang:
		./compile_clang_opt or ./compile_clang_noopt (opt = optimize)
Using:
Put renderer.d, voxlap.di, voxlap5.a and compilevoxlap into aof-client directory
./compile_with_voxlap or ./compile_with_voxlap_ldc for that sick framerate
./main (to start the game, don't forget to run the server first)

Licensing:
This software is released under the same license as its preceding Voxlap port (https://github.com/Ericson2314/Voxlap) and as the original Voxlap Engine non-commercial license.