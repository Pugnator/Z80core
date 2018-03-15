WINRES:=windres
RM:= rm -rf
STRIP:=strip
MAKE:=make
ifeq ($(shell uname), Linux)
  CC:=gcc
  CPP:=g++
  BISON:=bison
  LEX:=flex  
else
  CC:=mingw32-gcc
  CPP:=mingw32-g++
  WINRES:=windres
  BISON:=external/win_bison.exe
  LEX:=external/win_flex.exe
endif
MKDIR_P = mkdir -p

SRCDIR:=src
GRAMMARDIR:=$(SRCDIR)/grammar
ASMDASMDIR:=$(SRCDIR)/asmdasm
EMUDIR:=$(SRCDIR)/emucore
OBJDIR:=obj
OUTDIR:=bin

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
	$(ASMDASMDIR)/assembler.c

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

FLAGS:=-O2 -g3 -ffunction-sections -fdata-sections -Iinclude/emucore -Iinclude/asmdasm -Isrc/asmdasm
CFLAGS+=-std=gnu99 $(FLAGS)
CXXFLAGS+=-std=c++11 $(FLAGS)
LDFLAGS:=-Wl,--gc-sections


.PHONY: all
all: asm

.PHONY: emu
emu: dirs lexers $(EMUEXEC)

.PHONY: asm
asm: dirs lexers $(ASMEXEC)

.PHONY: dirs
dirs: $(OBJDIR) $(OUTDIR)

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

	$(STRIP) -s $(ASMEXEC)
	$(STRIP) -s $(EMUEXEC)
	
.PHONY: test
test:
	$(MAKE) -C tests

.PHONY: clean
clean:
	$(RM) $(OBJ)
	$(RM) $(OBJDIR)
	$(RM) $(OUTDIR)
	$(RM) $(ASMDASMDIR)/$(BISOUT).c
	$(RM) $(ASMDASMDIR)/$(LEXOUT).c
	$(RM) Debug
	$(RM) *.backup
