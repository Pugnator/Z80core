/* A Bison parser, made by GNU Bison 3.0.4.  */

/* Bison interface for Yacc-like parsers in C

   Copyright (C) 1984, 1989-1990, 2000-2015 Free Software Foundation, Inc.

   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>.  */

/* As a special exception, you may create a larger work that contains
   part or all of the Bison parser skeleton and distribute that work
   under terms of your choice, so long as that work isn't itself a
   parser generator using the skeleton or a modified version thereof
   as a parser skeleton.  Alternatively, if you modify or redistribute
   the parser skeleton itself, you may (at your option) remove this
   special exception, which will cause the skeleton and the resulting
   Bison output files to be licensed under the GNU General Public
   License without this special exception.

   This special exception was added by the Free Software Foundation in
   version 2.2 of Bison.  */

#ifndef YY_ASM_GRAMMAR_H_INCLUDED
# define YY_ASM_GRAMMAR_H_INCLUDED
/* Debug traces.  */
#ifndef ASMDEBUG
# if defined YYDEBUG
#if YYDEBUG
#   define ASMDEBUG 1
#  else
#   define ASMDEBUG 0
#  endif
# else /* ! defined YYDEBUG */
#  define ASMDEBUG 1
# endif /* ! defined YYDEBUG */
#endif  /* ! defined ASMDEBUG */
#if ASMDEBUG
extern int asmdebug;
#endif

/* Token type.  */
#ifndef ASMTOKENTYPE
# define ASMTOKENTYPE
  enum asmtokentype
  {
    UMINUS = 258,
    NL = 259,
    REG = 260,
    DREG = 261,
    FLAG = 262,
    IX = 263,
    IY = 264,
    END = 265,
    LABEL = 266,
    ADC = 267,
    ADD = 268,
    AND = 269,
    BIT = 270,
    CALL = 271,
    CCF = 272,
    CP = 273,
    CPD = 274,
    CPDR = 275,
    CPI = 276,
    CPIR = 277,
    CPL = 278,
    DAA = 279,
    DEC = 280,
    DI = 281,
    DJNZ = 282,
    EI = 283,
    EX = 284,
    EXX = 285,
    HALT = 286,
    IM = 287,
    IN = 288,
    INC = 289,
    IND = 290,
    INDR = 291,
    INI = 292,
    INIR = 293,
    JP = 294,
    JR = 295,
    LD = 296,
    LDD = 297,
    LDDR = 298,
    LDI = 299,
    LDIR = 300,
    NOP = 301,
    OR = 302,
    OTDR = 303,
    OTIR = 304,
    OUT = 305,
    OUTD = 306,
    OUTI = 307,
    POP = 308,
    PUSH = 309,
    RES = 310,
    RET = 311,
    RETN = 312,
    RL = 313,
    RLA = 314,
    RLC = 315,
    RLCA = 316,
    RR = 317,
    RRA = 318,
    RRC = 319,
    RRCA = 320,
    RST = 321,
    SBC = 322,
    SCF = 323,
    SET = 324,
    SLA = 325,
    SLL = 326,
    SRA = 327,
    SRL = 328,
    SUB = 329,
    XOR = 330,
    NEG = 331,
    RETI = 332,
    RLD = 333,
    RRD = 334,
    END_OF_FILE = 335,
    DEFINE = 336,
    DEFW = 337,
    STRING = 338,
    BIT8 = 339,
    WORD = 340,
    DWORD = 341,
    QWORD = 342,
    DQWORD = 343,
    NAN = 344,
    ASMPC = 345,
    ASMORG = 346,
    TEXT = 347,
    EQU = 348,
    ORG = 349,
    DIRECTIVE = 350
  };
#endif

/* Value type.  */
#if ! defined ASMSTYPE && ! defined ASMSTYPE_IS_DECLARED

union ASMSTYPE
{
#line 15 "z80.y" /* yacc.c:1909  */

    intmax_t var;
    char *str;

#line 163 "grammar.h" /* yacc.c:1909  */
};

typedef union ASMSTYPE ASMSTYPE;
# define ASMSTYPE_IS_TRIVIAL 1
# define ASMSTYPE_IS_DECLARED 1
#endif

/* Location type.  */
#if ! defined ASMLTYPE && ! defined ASMLTYPE_IS_DECLARED
typedef struct ASMLTYPE ASMLTYPE;
struct ASMLTYPE
{
  int first_line;
  int first_column;
  int last_line;
  int last_column;
};
# define ASMLTYPE_IS_DECLARED 1
# define ASMLTYPE_IS_TRIVIAL 1
#endif


extern ASMSTYPE asmlval;
extern ASMLTYPE asmlloc;
int asmparse (void);

#endif /* !YY_ASM_GRAMMAR_H_INCLUDED  */
