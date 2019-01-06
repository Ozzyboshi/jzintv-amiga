# jzintv-amiga
Intellivision emulator for old amiga classic machines.
RTG is required.  
Tested on a vampirized Amiga, works quite well except for audio.  

### Compile instructions
Download or clone the repository on a directory of your choice then run :
  docker run --rm -v #yourdir#:/data -w /data/src  --rm ozzyboshi/bebbo-amiga-gcc make -f Makefile.m68k
  
Ignore the implicit declaration instructions

