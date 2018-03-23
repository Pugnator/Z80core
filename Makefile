WINRES:=windres
RM:= rm -rf
STRIP:=strip
MAKE:=make
LUATOOLS=""

ifeq ($(shell uname), Linux)
  CC:=gcc
  CPP:=g++
  BISON:=bison
  LEX:=flex
  LUATOOLS=linlua
  CP = cp
else
  CC:=mingw32-gcc
  CPP:=mingw32-g++
  WINRES:=windres
  BISON:=external/win_bison.exe
  LEX:=external/win_flex.exe
  LUATOOLS=win32lua
  CP = cp
endif
MKDIR_P = mkdir -p

SRCDIR:=src
GRAMMARDIR:=$(SRCDIR)/grammar
ASMDASMDIR:=$(SRCDIR)/asmdasm
EMUDIR:=$(SRCDIR)/emucore
OBJDIR:=obj
OUTDIR:=bin
TOOLDIR:=tools
MONITOR:=tests/monitor48k/monitor48k.asm
TEST_BIN:=test.bin

LEXSRC:=$(GRAMMARDIR)/z80.lex
LEXOUT:=z80lex

BISPRF:=asm
BISSRC:=$(GRAMMARDIR)/z80.y
BISOUT=grammar

SRC:=\
	$(ASMDASMDIR)/compat.c \
	$(ASMDASMDIR)/z80tab.c \

ASMSRC:=\
	$(ASMDASMDIR)/main.c \
	$(ASMDASMDIR)/$(BISOUT).c \
	$(ASMDASMDIR)/$(LEXOUT).c \
	$(ASMDASMDIR)/assembler.c \
  $(ASMDASMDIR)/tap.c

DASMSRC:=\
	$(ASMDASMDIR)/dassembler.c

EMUSRC:=\
	$(EMUDIR)/cpu.cc \
	$(EMUDIR)/exec.cc

OBJ:=$(SRC:%.c=$(OBJDIR)/%.o)
ASMOBJ:=$(ASMSRC:%.c=$(OBJDIR)/%.o)
DASMOBJ:=$(DASMSRC:%.c=$(OBJDIR)/%.o)
EMUOBJ:=$(EMUSRC:%.cc=$(OBJDIR)/%.o)
EMUOBJ+=$(DASMOBJ)

ASMEXEC:=$(OUTDIR)/zasm.exe

EMUEXEC:=$(OUTDIR)/zemu.exe

FLAGS:=-ffunction-sections -fdata-sections -Iinclude/emucore -Iinclude/asmdasm -Isrc/asmdasm
CFLAGS+=-std=gnu99 $(FLAGS)
CXXFLAGS+=-std=c++11 $(FLAGS)
LDFLAGS:=-Wl,--gc-sections

.PHONY: all
all: dirs
all: FLAGS+=-O3
all: $(LUATOOLS) asm strip

.PHONY: debug
debug: FLAGS+=-O0 -g
debug: dirs
debug: asm

.PHONY: emu
emu: lexers $(EMUEXEC)

.PHONY: asm
asm:  lexers $(ASMEXEC)

.PHONY: dirs
dirs: 
	mkdir -p $(OBJDIR) 
	mkdir -p $(OUTDIR) 
	mkdir -p $(TOOLDIR)

.PHONY: lexers
lexers:
	$(BISON) -t -v -d $(BISSRC) -o $(ASMDASMDIR)/$(BISOUT).c
	$(LEX) -f -i -o $(ASMDASMDIR)/$(LEXOUT).c $(LEXSRC)

$(OBJDIR)/app.res: src/app.rc
	$(MKDIR_P) `dirname $@`
ifeq ($(OS), Windows_NT)
	$(WINRES) $< -O coff -o $@
endif

$(OBJDIR)/%.o: %.c
	$(MKDIR_P) `dirname $@`
	$(CC) -c -o $@ $< $(CFLAGS)

$(OBJDIR)/%.o: %.cc
	$(MKDIR_P) `dirname $@`
	$(CPP) -c -o $@ $< $(CXXFLAGS)

$(OBJDIR):
	$(MKDIR_P) $(OBJDIR)

$(OUTDIR):
	$(MKDIR_P) $(OUTDIR)

ifeq ($(OS), Windows_NT)
  $(ASMEXEC): $(OBJ) $(ASMOBJ) $(DASMOBJ) $(OBJDIR)/app.res
		$(CC) -o $@ $^ $(CFLAGS) $(LDFLAGS)
  
  $(EMUEXEC): $(EMUOBJ) $(OBJ) $(OBJDIR)/app.res
		$(CPP) -o $@ $^ $(CXXFLAGS) $(LDFLAGS)
else
  $(ASMEXEC): $(OBJ) $(ASMOBJ) $(DASMOBJ)
		$(CC) -o $@ $^ $(CFLAGS) $(LDFLAGS)
  
  $(EMUEXEC): $(EMUOBJ) $(OBJ)
		$(CPP) -o $@ $^ $(CXXFLAGS) $(LDFLAGS)
endif

linlua:
	$(MAKE) -C external/lua/src linux
	$(CP) externals/lua/src/luac.exe tools/
	$(MAKE) -C external/lua/src clean
	$(MAKE) -C external/lua/src mingw

.PHONY: selftest
win32lua:
	$(MAKE) -C external/lua/src win32
	$(CP) external/lua/src/luac.exe tools/	

.PHONY: selftest
.PHONY: strip
strip:
	-$(STRIP) -s $(ASMEXEC)
	
.PHONY: selftest
selftest:
	$(ASMEXEC) $(MONITOR) -o $(TEST_BIN)

.PHONY: clean
clean:
	$(MAKE) -C external/lua/src clean
	$(RM) $(OBJ)
	$(RM) $(OBJDIR)
	$(RM) $(OUTDIR)
	$(RM) $(ASMDASMDIR)/$(BISOUT).c
	$(RM) $(ASMDASMDIR)/$(LEXOUT).c
	$(RM) Debug
	$(RM) $(TEST_BIN)
	$(RM) *.backup
