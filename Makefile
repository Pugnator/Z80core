GRAMMARDIR:=cpu/z80
ASMDASMDIR:=src/asmdasm
EMUDIR:=src/emucore
OBJDIR:=obj
OUTDIR:=bin

LEXSRC:=$(GRAMMARDIR)/z80.lex
LEXOUT:=z80lex
LEX:=flex

BISON:=bison
BISPRF:=asm
BISSRC:=$(GRAMMARDIR)/z80.y
BISOUT=grammar

MKDIR_P = mkdir -p

ASMSRC:=\
	$(ASMDASMDIR)/$(BISOUT).c \
	$(ASMDASMDIR)/$(LEXOUT).c \
	$(ASMDASMDIR)/z80tab.c \
	$(ASMDASMDIR)/assembler.c \
	$(ASMDASMDIR)/dassembler.c \
	$(ASMDASMDIR)/compat.c

EMUSRC:=\
	src/main.cc \
	$(EMUDIR)/cpu.cc

CC:=mingw32-gcc
CPP:=mingw32-g++

OBJ:=$(SRC:%.c=$(OBJDIR)/%.o)
OBJ+=$(EMUSRC:%.cc=$(OBJDIR)/%.o)

EXEC:=$(OUTDIR)/asm.exe

CFLAGS:=-O0 -g3 -std=gnu99 -Iinclude/emucore -Iinclude/asmdasm -Isrc/asmdasm -ffunction-sections -fdata-sections
CXXFLAGS:=-O0 -g3 -std=c++11 -Iinclude/emucore -Iinclude/asmdasm -Isrc/asmdasm -ffunction-sections -fdata-sections
LDFLAGS:=-Wl,--gc-sections 

.PHONY: all
all: dirs lexers $(EXEC)

.PHONY: dirs
dirs: $(OBJDIR) $(OUTDIR)

$(OBJDIR):
	$(MKDIR_P) $(OBJDIR)

$(OUTDIR):
	$(MKDIR_P) $(OUTDIR)

$(EXEC): $(OBJ)
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