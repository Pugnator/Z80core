GRAMMARDIR:=grammar
SRCDIR:=src
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

SRC:=\
	$(SRCDIR)/$(BISOUT).c \
	$(SRCDIR)/$(LEXOUT).c \
	$(SRCDIR)/main.c \
	$(SRCDIR)/z80tab.c \
	$(SRCDIR)/assembler.c \
	$(SRCDIR)/dassembler.c \
	$(SRCDIR)/compat.c

CC:=mingw32-gcc
OBJ=$(SRC:%.c=$(OBJDIR)/%.o)

CLITOOL:=$(OUTDIR)/asm

CFLAGS+=-D__DEBUG -D_ZVERSION="0.1" -O0 -std=gnu99 -gdwarf-2 -g3 -Iinclude -Isrc

.PHONY: all
all: dirs lexers $(CLITOOL)

.PHONY: dirs
dirs: $(OBJDIR) $(OUTDIR)

$(OBJDIR):
	$(MKDIR_P) $(OBJDIR)

$(OUTDIR):
	$(MKDIR_P) $(OUTDIR)

$(CLITOOL): $(OBJ)
	$(CC) -o $@ $^ $(CFLAGS)

.PHONY: lexers
lexers:
	@$(BISON) -t -v -d $(BISSRC) -o $(SRCDIR)/$(BISOUT).c
	@$(LEX) -f -i -o $(SRCDIR)/$(LEXOUT).c $(LEXSRC)	

$(OBJDIR)/%.o: %.c
	$(MKDIR_P) `dirname $@`
	$(CC) -c -o $@ $< $(CFLAGS)

.PHONY: clean
clean:
	rm -rf $(OBJ) \
	$(OBJDIR) \
	$(OUTDIR) \
	$(EXEC).bin \
	$(CLITOOL) \
	$(SRCDIR)/$(BISOUT).c \
	$(SRCDIR)/$(LEXOUT).c \
	*.backup \
	$(SRCDIR)/*.output \
	$(SRCDIR)/$(BISOUT).h