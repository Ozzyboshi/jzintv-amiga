##############################################################################
## Source-level Makefile for jzintv
##
## The whole make process is driven from the top-level Makefile.  In contrast
## to the (poor) advice given in the Make documentation, I do _not_ call make
## recursively to build the source of this project.
##############################################################################

##############################################################################
## Project directory structure
##############################################################################
B=../bin
L=../lib
R=../rom

##############################################################################
## Project-wide build flags
##############################################################################
P           = export PATH;

SDL_CFLAGS := $(shell sdl-config --cflags)
SDL_LFLAGS := $(shell sdl-config --libs)

SVN_REV := $(shell (svn info -R || :) | grep "Last Changed Rev:" | cut -d' ' -f4 | sort -n | tail -1)
SVN_DTY := $(shell echo `(svn status || :) | grep -v '^?' | wc -l`)

# Set "X" to be the executable extension, and "O" to be the object extension.
X =        
O = o

# Set "RM" to be the remove/delete command
RM = rm -f 

 WARN    = -Wall -W -Wextra -Wshadow -Wpointer-arith
 WARN   += -Wbad-function-cast -Wcast-qual -Wc++-compat
 WARN   += -Wmissing-declarations -Wmissing-prototypes
 WARN   += -Wstrict-prototypes -Wmaybe-uninitialized
 WARN   += -Werror -Wstrict-aliasing -Wlogical-op

 WARNXX  = -Wall -W -Wextra -Wshadow -Wpointer-arith 
 WARNXX += -Wcast-qual -Wsequence-point
 WARNXX += -Wc++11-compat -Wc++14-compat -Wc++1z-compat
 WARNXX += -Werror -Wstrict-aliasing -Wlogical-op
 WARNXX += -Wold-style-cast


#WARN    = -W -Wall -ansi -Wbad-function-cast -Wcast-align \
		   -Wcast-qual -Wchar-subscripts -Winline \
		   -Wmissing-prototypes -Wnested-externs -Wpointer-arith \
		   -Wshadow -Wstrict-prototypes -Wwrite-strings \
	   -Dlinux

#WARNXX  = -W -Wall -ansi -Wcast-align \
 		   -Wcast-qual -Wchar-subscripts -Winline \
 		   -Wpointer-arith -Wredundant-decls -Wshadow -Wwrite-strings \
 	   -Dlinux


 CC  = $(P) gcc-8.1.0 -std=c99
 CXX = $(P) g++-8.1.0 -std=c++14
#CC  = $(P) gcc-3.4
#CC  = $(P) /usr/local/bin/gcc -V4.1.1
#CXX = $(P) /usr/local/bin/g++ 
#CC  = $(P) icc
#CC  = $(P) /usr/bin/gcc
#CXX = $(P) /usr/bin/g++

#DEF_FLAGS += -DDIRECT_INTV2PC
#DEF_FLAGS += -DNEED_INOUT
 DEF_FLAGS += -Dlinux

 LTO = -flto=4 -ffat-lto-objects

#OPT_FLAGS = -ggdb3
#OPT_FLAGS = -O
#OPT_FLAGS = -O2 -ggdb3 -pg
#OPT_FLAGS =  -ggdb3 -O6 -fomit-frame-pointer -fprefetch-loop-arrays -msse #-DBENCHMARK_STIC
 OPT_FLAGS =  $(LTO) -O6 -fomit-frame-pointer -fprefetch-loop-arrays -msse2
 OPT_FLAGS += -fsplit-loops -funswitch-loops -fvisibility=hidden
 OPT_FLAGS += -fstrict-aliasing
#OPT_FLAGS = -O6 -msse2 -pg -ggdb -DLOCAL=
#OPT_FLAGS = -tpp6 -axMiKW -ip -vec_report3 -opt_report -ansi_alias -restrict -DHAVE_RESTRICT -align -O3 -Ob1 # -ipo # intel icc flags

#OPT_FLAGS += -DBENCHMARK_STIC

CFLAGS   = $(OPT_FLAGS) $(WARN)   -I. -I.. $(DEF_FLAGS) $(EXTRA)
CXXFLAGS = $(OPT_FLAGS) $(WARNXX) -I. -I.. $(DEF_FLAGS) $(EXTRA)
#LFLAGS   = /usr/local/lib/libgcc_s.so -L../lib 
 LFLAGS   = -static-libstdc++ -static-libgcc -L../lib -lrt

CFLAGS   += -ffunction-sections -fdata-sections

OBJS      = jzintv.$(O)
PROG_SDL  = $(B)/jzintv
PROG_NULL = $(B)/jzintv_batch
TOCLEAN  += $(PROG_SDL)
TOCLEAN  += $(PROG_NULL)
TOCLEAN  += core

CFLAGS += $(SDL_CFLAGS) 
#LFLAGS += $(SDL_LFLAGS)

# If enabling the sanitizer, disable -static by setting SLFLAGS = LFLAGS
#CFLAGS += -fsanitize=undefined -fno-sanitize=alignment
#CFLAGS += -fsanitize=address 
#SLFLAGS = $(LFLAGS)

##############################################################################
## Generic build-rules
##############################################################################

all: $(OBJS) $(OBJS_SDL) $(OBJS_NULL) $(PROG_SDL) $(PROG_NULL)

$(PROG_SDL): $(OBJS) $(OBJS_SDL) svn_revision.$(O)
	$(CXX) -o $(PROG_SDL) $(OBJS) $(OBJS_SDL) svn_revision.$(O) $(CFLAGS) $(SDL_LFLAGS) $(LFLAGS)

$(PROG_NULL): $(OBJS) $(OBJS_NULL) svn_revision.$(O)
	$(CXX) -o $(PROG_NULL) $(OBJS) $(OBJS_NULL) svn_revision.$(O) $(CFLAGS) $(SLFLAGS)

clean:
	$(RM) $(OBJS) 
	$(RM) $(OBJS_SDL) 
	$(RM) $(OBJS_NULL) 
	$(RM) $(TOCLEAN)

%.$(O): %.c
	$(CC) -o $@ $(CFLAGS) -c $<

%.s: %.c
	$(CC) -fverbose-asm -S -o $@ $(CFLAGS) -c $<

##############################################################################
## Linux-specific stuff
##############################################################################

pads/pads_intv2pc.$(O):
	$(CC) -O3 -o pads/pads_intv2pc.$(O) $(CFLAGS) -c pads/pads_intv2pc.c

OBJS_SDL += pads/pads_cgc_linux.$(O)
OBJS += pads/pads_intv2pc.$(O)

##############################################################################
## Makefile.common includes all the subMakefiles and such
##############################################################################
include Makefile.common
build: jzIntv SDK-1600
