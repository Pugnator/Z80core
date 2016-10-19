CC:=mingw32-gcc
CPP:=mingw32-g++

GRAMMARDIR:=cpu/z80
ASMDASMDIR:=src/asmdasm
EMUDIR:=src/emucore
OBJDIR:=obj
OUTDIR:=bin

LEXSRC:=$(GRAMMARDIR)/z80.lex
LEXOUT:=z80lex
LEX:=flex.exe

BISON:=bison.exe
BISPRF:=asm
BISSRC:=$(GRAMMARDIR)/z80.y
BISOUT=grammar

MKDIR_P = mkdir -p

SRC:=\
	$(ASMDASMDIR)/compat.c \
	$(ASMDASMDIR)/z80tab.c \

ASMSRC:=\
	src/main.cc \
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

CFLAGS:=-O0 -g3 -std=gnu99 -Iinclude/emucore -Iinclude/asmdasm -Isrc/asmdasm -ffunction-sections -fdata-sections
CXXFLAGS:=-O0 -g3 -std=c++11 -Iinclude/emucore -Iinclude/asmdasm -Isrc/asmdasm -ffunction-sections -fdata-sections
LDFLAGS:=-Wl,--gc-sections 

.PHONY: all
all: dirs lexers $(ASMEXEC) $(EMUEXEC)

.PHONY: emu
emu: dirs $(EMUEXEC)

.PHONY: asm
asm: dirs $(ASMEXEC)

.PHONY: dirs
dirs: $(OBJDIR) $(OUTDIR)

$(OBJDIR):
	$(MKDIR_P) $(OBJDIR)

$(OUTDIR):
	$(MKDIR_P) $(OUTDIR)

$(ASMEXEC): $(DASMOBJ) $(ASMOBJ) $(OBJ)
	$(CPP) -o $@ $^ $(CXXFLAGS) $(LDFLAGS)

$(EMUEXEC): $(EMUOBJ) $(DASMOBJ) $(OBJ)
	$(CPP) -o $@ $^ $(CXXFLAGS) $(LDFLAGS)

.PHONY: lexers
lexers:
	$(BISON) -t -v -d $(BISSRC) -o $(ASMDASMDIR)/$(BISOUT).c
	$(LEX) -f -i -o $(ASMDASMDIR)/$(LEXOUT).c $(LEXSRC)	

$(OBJDIR)/%.o: %.c
	$(MKDIR_P) `dirname $@`
	$(CC) -c -o $@ $< $(CFLAGS) 

$(OBJDIR)/%.o: %.cc
	$(MKDIR_P) `dirname $@`
	$(CPP) -c -o $@ $< $(CXXFLAGS)

.PHONY: clean
clean:
	rm -rf $(OBJ) $(OBJDIR) $(OUTDIR) $(ASMDASMDIR)/$(BISOUT).c $(ASMDASMDIR)/$(LEXOUT).c *.backup $(ASMDASMDIR)/*.output $(ASMDASMDIR)/$(BISOUT).h