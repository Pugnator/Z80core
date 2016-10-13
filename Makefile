LEXSRC+=z80.lex
LEXOUT:=z80lex
LEX:=flex

BISON:=bison
BISPRF:=asm
BISSRC:=z80.y
BISOUT=grammar

SRC:=\
	$(BISOUT).c \
	$(LEXOUT).c \
	main.c \
	z80tab.c \
	assembler.c \
	dassembler.c \
	compat.c

CC:=mingw32-gcc
OBJ=$(SRC:%.c=%.o)

CLITOOL:=asm

CFLAGS+=-D__DEBUG -D_ZVERSION="0.1" -O0 -std=gnu99 -gdwarf-2 -g3 -I.

.PHONY: all
all: lexers $(CLITOOL)

$(CLITOOL): $(OBJ)
	$(CC) -o $@ $^ $(CFLAGS)

.PHONY: lexers
lexers:
	@$(BISON) -t -v -d $(BISSRC) -o $(BISOUT).c
	@$(LEX) -f -i -o $(LEXOUT).c $(LEXSRC)	

%.o: %.c	
	$(CC) -c -o $@ $< $(CFLAGS)

.PHONY: clean
clean:
	rm -f $(OBJ) $(EXEC).bin $(LIBZCORE) $(CLITOOL) $(BISOUT).c $(LEXOUT).c