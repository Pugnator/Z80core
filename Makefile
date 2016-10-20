CC:=mingw32-gcc
CPP:=mingw32-g++
WINRES:=windres
RM:= rm -rf
BISON:=bison.exe
LEX:=flex.exe
MKDIR_P = mkdir -p


GRAMMARDIR:=cpu/z80
SRCDIR:=src
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

ASMEXEC:=$(OUTDIR)/asm.exe

EMUEXEC:=$(OUTDIR)/emu.exe

FLAGS:=-O0 -g3  -falign-functions=16 -falign-loops=16 -ffunction-sections -fdata-sections -Iinclude/emucore -Iinclude/asmdasm -Isrc/asmdasm
CFLAGS+=-std=gnu99 $(FLAGS)
CXXFLAGS+=-std=c++11 $(FLAGS)
LDFLAGS:=-Wl,--gc-sections

.PHONY: all
all: clean asm emu

.PHONY: emu
emu: dirs lexers $(EMUEXEC)

.PHONY: asm
asm: dirs lexers $(ASMEXEC)

.PHONY: dirs
dirs: $(OBJDIR) $(OUTDIR)

$(OBJDIR):
	$(MKDIR_P) $(OBJDIR)

$(OUTDIR):
	$(MKDIR_P) $(OUTDIR)

$(ASMEXEC): $(OBJ) $(ASMOBJ) $(DASMOBJ) $(OBJDIR)/app.res
	$(CC) -o $@ $^ $(CFLAGS) $(LDFLAGS)

$(EMUEXEC): $(EMUOBJ) $(OBJ) $(OBJDIR)/app.res
	$(CPP) -o $@ $^ $(CXXFLAGS) $(LDFLAGS)

.PHONY: lexers
lexers:
	$(BISON) -t -v -d $(BISSRC) -o $(ASMDASMDIR)/$(BISOUT).c
	$(LEX) -f -i -o $(ASMDASMDIR)/$(LEXOUT).c $(LEXSRC)

$(OBJDIR)/app.res: app.rc
	$(MKDIR_P) `dirname $@`
	$(WINRES) $< -O coff -o $@

$(OBJDIR)/%.o: %.c
	$(MKDIR_P) `dirname $@`
	$(CC) -c -o $@ $< $(CFLAGS)

$(OBJDIR)/%.o: %.cc
	$(MKDIR_P) `dirname $@`
	$(CPP) -c -o $@ $< $(CXXFLAGS)

.PHONY: clean
clean:
	$(RM) $(OBJ) 
	$(RM) $(OBJDIR) 
	$(RM) $(OUTDIR) 
	$(RM) $(ASMDASMDIR)/$(BISOUT).c 
	$(RM) $(ASMDASMDIR)/$(LEXOUT).c 
	$(RM) *.backup 
