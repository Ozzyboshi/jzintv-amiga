# jzintv-amiga
Intellivision emulator for old amiga classic machines.
RTG is required.  
Tested on a vampirized Amiga, works quite well except for audio.  

### Compile instructions
Download or clone the repository on a directory of your choice then run :
  docker run --rm -v #yourdir#:/data -w /data/src  --rm ozzyboshi/bebbo-amiga-gcc make -f Makefile.m68k
  
Ignore the implicit declaration warnings

### jzintv-amiga in action
[![Vid 1](https://img.youtube.com/vi/7mEO_5BfSBU/0.jpg)](https://www.youtube.com/watch?v=7mEO_5BfSBU)
[![Vid 2](https://img.youtube.com/vi/pujyfHmzLa0/0.jpg)](https://www.youtube.com/watch?v=pujyfHmzLa0)

### How to run
First copy your roms under the rom folder, you'll probably need exec.bin and grom.bin, they are not included on this repository because I think they are copyrighted.  
Also copy your game roms under the rom folder, if you don't have any there are plenty of free rom here
http://www.intellivision.us/roms/roms.php  

Now open your amiga shell and go to the root folder where you unpacked the emulator and run

bin/jzintv --help

This will give you a list of supported switches to use.  
On my vampirized A600 i need to hack the input controls to remap the intellevision enter button to return, at default this key is linked to the enter of the numeric pad, but as you know, A600 don't have it.
Also there are some performance issues on a vampire with audio, so you must disable it with -a0 flag.
If you want fullscreen use the -f1.
So the typical launch configuration for me is 

bin/jzintv --kbdhackfile=hackfiles/hack1.cfg -ao -f1 #ROMNAME#

Hope in the future I will be able to optimize audio processing (maybe with AMMX? I dont know).



