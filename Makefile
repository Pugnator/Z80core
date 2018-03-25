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
Z80DLL:=z80

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
	$(ASMDASMDIR)/tap.c \
	$(ASMDASMDIR)/luabind.c

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

FLAGS:=-ffunction-sections -fdata-sections -Iinclude/emucore -Iinclude/asmdasm -Isrc/asmdasm -Iexternal/lua/src
CFLAGS+=-std=gnu99 $(FLAGS)
CXXFLAGS+=-std=c++11 $(FLAGS)
LDFLAGS:=-Wl,--gc-sections
LDFLAGS+=external/lua/src/liblua.a -lm

.PHONY: all
all: FLAGS+=-O3
all: dirs
all: $(LUATOOLS) asm z80lib strip

.PHONY: debug
debug: FLAGS+=-O0 -g
debug: dirs
debug: $(LUATOOLS) asm z80lib

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

.PHONY: z80lib
z80lib: $(OBJ) $(ASMOBJ) $(DASMOBJ)
	$(CC) -shared -fpic -o $(OUTDIR)/$@.dll $^ $(LDFLAGS) -Wl,--export-all-symbols,--enable-auto-import
  
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

.PHONY: linlua
linlua:
	$(MAKE) -C external/lua/src linux
	$(CP) external/lua/src/luac.exe tools/

.PHONY: win32lua
win32lua:
	$(MAKE) -C external/lua/src win32
	$(CP) external/lua/src/luac.exe tools/

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
	$(RM) $(TOOLDIR)
	$(RM) $(ASMDASMDIR)/$(BISOUT).c
	$(RM) $(ASMDASMDIR)/$(LEXOUT).c
	$(RM) $(TEST_BIN)
	$(RM) *.backup
