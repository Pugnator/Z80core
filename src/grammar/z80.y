%{
#define _GNU_SOURCE
#include <common.h>
int current_line = 1;
%}

%error-verbose
%expect 0
%define parse.lac full
%define api.prefix {asm}
%token-table
%locations

%union
{
    intmax_t var;
    char *str;
}

%start input

%left '-' '+' '*' '/' '&' '%'
%right UMINUS

%type  <var> expr IXidx IYidx opbrk clbrk
%type  <str> instr defw seq org text equ PDREG PREG
%type  <str> directive

%token <str> NL REG DREG FLAG IX IY END LABEL
%token <str> ADC ADD AND BIT CALL CCF CP CPD CPDR CPI CPIR CPL DAA DEC DI DJNZ EI EX EXX HALT IM IN
%token <str> INC IND INDR INI INIR JP JR LD LDD LDDR LDI LDIR NOP OR OTDR OTIR OUT OUTD OUTI POP PUSH
%token <str> RES RET RETN RL RLA RLC RLCA RR RRA RRC RRCA RST SBC SCF SET SLA SLL SRA SRL SUB XOR NEG
%token <str> RETI RLD RRD END_OF_FILE
%token <str> DEFINE DEFW STRING

%token <var> BIT8 WORD DWORD QWORD DQWORD ASMPC ASMORG TEXT EQU ORG DIRECTIVE
%%

input:    lines
;

lines:    line
        | lines line
;

line:     stmt NL
        | NL
        | END {YYACCEPT;}
        | END_OF_FILE {YYACCEPT;}
;

directive:  defw
        | text
        | DEFINE
;

stmt:     instr
        | expr
        | directive
        | org
        | equ
;

defw:     DEFW seq
;

equ:      EQU {$$ = "";}
;

org:      ORG expr {$$ = ""; CURRENT_ORG = PC = $2;}
;

text:     TEXT STRING {$$ = "kilroy was here";deft($2);free($2);}
;

seq:      expr {$$ = ""; defw ($1);}
        | seq ',' expr {$$ = "";defw ($3);}

expr:     WORD                                 { $$ = $1;}
        | BIT8                                 { $$ = $1;}
        | DWORD                                { $$ = $1;}
        | QWORD                                { $$ = $1;}
        | DQWORD                               { $$ = $1;}
        | ASMPC                                { $$ = PC;}
        | ASMORG                               { $$ = CURRENT_ORG;}
        | STRING                               { tgt_label = true; $$ = get_label_address($1); free($1);}
        | expr '-' expr                        { $$ = $1-$3;}
        | expr '+' expr                        { $$ = $1+$3;}
        | expr '*' expr                        { $$ = $1*$3;}
        | expr '/' expr                        { $$ = $1/$3;}
        | expr '%' expr                        { $$ = $1/$3;}
        | expr '&' expr                        { $$ = $1 & $3;}
        | '-' expr %prec UMINUS                { $$ = -$2;}
;

opbrk:    '('                                  { $$=0;}
        | '['                                  { $$=0;}
;

clbrk:    ')'                                  { $$=0;}
        | ']'                                  { $$=0;}
;


IXidx:    opbrk IX '+' expr clbrk                          { SIGN8($4);$$ = $4;free($2);}
        | opbrk IX '-' expr clbrk                          { SIGN8($4);$$ = $4;free($2);}

IYidx:    opbrk IY '+' expr clbrk                        { SIGN8($4);$$ = $4;free($2);}
        | opbrk IY '-' expr clbrk                        { SIGN8($4);$$ = $4;free($2);}
;

PDREG:     opbrk DREG clbrk                        { TEMPLATE(&$$, "[%s]", $2);free($2);}
;

PREG:     opbrk REG clbrk                          { TEMPLATE(&$$, "[%s]", $2);free($2);}
;

instr:    NOP                                  { TEMPLATE(&$$, "%s", $1); NO_ARGS($$, $1);}
        | CCF                                  { TEMPLATE(&$$, "%s", $1); NO_ARGS($$, $1);}
        | CPD                                  { TEMPLATE(&$$, "%s", $1); NO_ARGS($$, $1);}
        | CPDR                                 { TEMPLATE(&$$, "%s", $1); NO_ARGS($$, $1);}
        | CPI                                  { TEMPLATE(&$$, "%s", $1); NO_ARGS($$, $1);}
        | CPIR                                 { TEMPLATE(&$$, "%s", $1); NO_ARGS($$, $1);}
        | CPL                                  { TEMPLATE(&$$, "%s", $1); NO_ARGS($$, $1);}
        | DAA                                  { TEMPLATE(&$$, "%s", $1); NO_ARGS($$, $1);}
        | DI                                   { TEMPLATE(&$$, "%s", $1); NO_ARGS($$, $1);}
        | EI                                   { TEMPLATE(&$$, "%s", $1); NO_ARGS($$, $1);}
        | EX                                   { TEMPLATE(&$$, "%s", $1); NO_ARGS($$, $1);}
        | EXX                                  { TEMPLATE(&$$, "%s", $1); NO_ARGS($$, $1);}
        | HALT                                 { TEMPLATE(&$$, "%s", $1); NO_ARGS($$, $1);}
        | IND                                  { TEMPLATE(&$$, "%s", $1); NO_ARGS($$, $1);}
        | INDR                                 { TEMPLATE(&$$, "%s", $1); NO_ARGS($$, $1);}
        | INI                                  { TEMPLATE(&$$, "%s", $1); NO_ARGS($$, $1);}
        | INIR                                 { TEMPLATE(&$$, "%s", $1); NO_ARGS($$, $1);}
        | LDD                                  { TEMPLATE(&$$, "%s", $1); NO_ARGS($$, $1);}
        | LDDR                                 { TEMPLATE(&$$, "%s", $1); NO_ARGS($$, $1);}
        | LDI                                  { TEMPLATE(&$$, "%s", $1); NO_ARGS($$, $1);}
        | LDIR                                 { TEMPLATE(&$$, "%s", $1); NO_ARGS($$, $1);}
        | NEG                                  { TEMPLATE(&$$, "%s", $1); NO_ARGS($$, $1);}
        | OTDR                                 { TEMPLATE(&$$, "%s", $1); NO_ARGS($$, $1);}
        | OTIR                                 { TEMPLATE(&$$, "%s", $1); NO_ARGS($$, $1);}
        | OUTD                                 { TEMPLATE(&$$, "%s", $1); NO_ARGS($$, $1);}
        | OUTI                                 { TEMPLATE(&$$, "%s", $1); NO_ARGS($$, $1);}
        | RETI                                 { TEMPLATE(&$$, "%s", $1); NO_ARGS($$, $1);}
        | RETN                                 { TEMPLATE(&$$, "%s", $1); NO_ARGS($$, $1);}
        | RLA                                  { TEMPLATE(&$$, "%s", $1); NO_ARGS($$, $1);}
        | RLCA                                 { TEMPLATE(&$$, "%s", $1); NO_ARGS($$, $1);}
        | RLD                                  { TEMPLATE(&$$, "%s", $1); NO_ARGS($$, $1);}
        | RRA                                  { TEMPLATE(&$$, "%s", $1); NO_ARGS($$, $1);}
        | RRCA                                 { TEMPLATE(&$$, "%s", $1); NO_ARGS($$, $1);}
        | RRD                                  { TEMPLATE(&$$, "%s", $1); NO_ARGS($$, $1);}
        | SCF                                  { TEMPLATE(&$$, "%s", $1); NO_ARGS($$, $1);}
        /*###################################################################################################################*/

        /* ADC */
        /*###################################################################################################################*/
        | ADC REG  ',' REG                     { TEMPLATE(&$$, "%s %s, %s", $1, $2, $4); HANDLE($$,0,0); FREE($1, $2, $4);}
        | ADC REG  ',' PDREG                   { TEMPLATE(&$$, "%s %s, %s", $1, $2, $4); HANDLE($$,0,0); FREE($1, $2, $4);}
        | ADC DREG ',' DREG                    { TEMPLATE(&$$, "%s %s, %s", $1, $2, $4); HANDLE($$,0,0); FREE($1, $2, $4);}
        | ADC REG  ',' expr                    { TEMPLATE(&$$, "%s %s, %%#.2x", $1, $2); HANDLE($$,$4,1); FREE($1, $2);}
        | ADC REG  ',' IXidx                   { UNSIGN8($4); TEMPLATE(&$$, "%s %s, [IX + %%#.2x]", $1, $2); HANDLE($$,$4,1); FREE($1, $2);}
        | ADC REG  ',' IYidx                   { UNSIGN8($4); TEMPLATE(&$$, "%s %s, [IY + %%#.2x]", $1, $2); HANDLE($$,$4,1); FREE($1, $2);}
        /*###################################################################################################################*/

        /* ADD */
        /*###################################################################################################################*/
        | ADD REG  ',' REG                     { TEMPLATE(&$$, "%s %s, %s", $1, $2, $4); HANDLE($$,0,0); FREE($1, $2, $4);}
        | ADD REG  ',' PDREG                   { TEMPLATE(&$$, "%s %s, %s", $1, $2, $4); HANDLE($$,0,0); FREE($1, $2, $4);}
        | ADD DREG ',' DREG                    { TEMPLATE(&$$, "%s %s, %s", $1, $2, $4); HANDLE($$,0,0); FREE($1, $2, $4);}
        | ADD IX ',' DREG                      { TEMPLATE(&$$, "%s %s, %s", $1, $2, $4); HANDLE($$,0,0); FREE($1, $2, $4);}
        | ADD IY ',' DREG                      { TEMPLATE(&$$, "%s %s, %s", $1, $2, $4); HANDLE($$,0,0); FREE($1, $2, $4);}
        | ADD IX ',' IX                        { TEMPLATE(&$$, "%s %s, %s", $1, $2, $4); HANDLE($$,0,0); FREE($1, $2, $4);}
        | ADD IY ',' IY                        { TEMPLATE(&$$, "%s %s, %s", $1, $2, $4); HANDLE($$,0,0); FREE($1, $2, $4);}
        | ADD REG  ',' expr                    { TEMPLATE(&$$, "%s %s, %%#.2x", $1, $2); HANDLE($$,$4,1); FREE($1, $2);}
        | ADD REG  ',' IXidx                   { TEMPLATE(&$$, "%s %s, [IX + %%#.2x]", $1, $2); HANDLE($$,$4,1); FREE($1, $2);}
        | ADD REG  ',' IYidx                   { TEMPLATE(&$$, "%s %s, [IY + %%#.2x]", $1, $2); HANDLE($$,$4,1); FREE($1, $2);}
        /*###################################################################################################################*/

        /* AND */
        /*###################################################################################################################*/
        | AND REG                              { TEMPLATE(&$$, "%s %s", $1, $2); HANDLE($$,0,0); FREE($1, $2);}
        | AND PDREG                            { TEMPLATE(&$$, "%s %s", $1, $2); HANDLE($$,0,0); FREE($1, $2);}
        | AND expr                             { UNSIGN8($2);TEMPLATE(&$$, "%s %%#.2x", $1); HANDLE($$,$2,1); FREE($1);}
        | AND IXidx                            { TEMPLATE(&$$, "%s [IX + %%#.2x]", $1); HANDLE($$,$2,1); FREE($1);}
        | AND IYidx                            { TEMPLATE(&$$, "%s [IY + %%#.2x]", $1); HANDLE($$,$2,1); FREE($1);}
        /*###################################################################################################################*/

        /* BIT */
        /*###################################################################################################################*/
        | BIT expr ',' REG                     { TEMPLATE(&$$, "%s %jd, %s", $1, $2, $4); HANDLE($$,0,0); FREE($1, $4);}
        | BIT expr ',' PDREG                   { TEMPLATE(&$$, "%s %jd, %s", $1, $2, $4); HANDLE($$,0,0); FREE($1, $4);}
        | BIT expr  ',' IXidx                  { TEMPLATE(&$$, "%s %jd, [IX + %%#.2x]", $1, $2); HANDLE($$,$4,1); FREE($1);}
        | BIT expr  ',' IYidx                  { TEMPLATE(&$$, "%s %jd, [IY + %%#.2x]", $1, $2); HANDLE($$,$4,1); FREE($1);}
        /*###################################################################################################################*/

        /* CALL */
        /*###################################################################################################################*/
        | CALL expr                           {UNSIGN16($2); TEMPLATE(&$$, "%s %%#.4x", $1); HANDLE($$,$2,2); FREE($1);}
        | CALL FLAG ',' expr                  {UNSIGN16($4); TEMPLATE(&$$, "%s %%#.4x", $1); HANDLE($$,$4,2); FREE($1, $2);}
        | CALL REG ',' expr                   {UNSIGN16($4); TEMPLATE(&$$, "%s %%#.4x", $1); HANDLE($$,$4,2); FREE($1, $2);}
        /*###################################################################################################################*/

        /* CP */
        /*###################################################################################################################*/
        | CP expr                              { UNSIGN8($2);TEMPLATE(&$$, "%s %%#.2x", $1); HANDLE($$,$2,1); FREE($1);}
        | CP REG                               { TEMPLATE(&$$, "%s %s", $1, $2); HANDLE($$,0,0); FREE($1, $2);}
        | CP PDREG                             { TEMPLATE(&$$, "%s %s", $1, $2); HANDLE($$,0,0); FREE($1, $2);}
        | CP IXidx                             { TEMPLATE(&$$, "%s [IX + %%#.2x]", $1); HANDLE($$,$2,1); FREE($1);}
        | CP IYidx                             { TEMPLATE(&$$, "%s [IY + %%#.2x]", $1); HANDLE($$,$2,1); FREE($1);}
        /*###################################################################################################################*/

        /* DEC */
        /*###################################################################################################################*/
        | DEC PDREG                            { TEMPLATE(&$$, "%s %s", $1, $2); HANDLE($$,0,0); FREE($1, $2);}
        | DEC REG                              { TEMPLATE(&$$, "%s %s", $1, $2); HANDLE($$,0,0); FREE($1, $2);}
        | DEC DREG                             { TEMPLATE(&$$, "%s %s", $1, $2); HANDLE($$,0,0); FREE($1, $2);}
        | DEC IX                               { TEMPLATE(&$$, "%s %s", $1, $2); HANDLE($$,0,0); FREE($1, $2);}
        | DEC IY                               { TEMPLATE(&$$, "%s %s", $1, $2); HANDLE($$,0,0); FREE($1, $2);}
        | DEC IXidx                            { TEMPLATE(&$$, "%s [IX + %%#.2x]", $1); HANDLE($$,$2,1); FREE($1);}
        | DEC IYidx                            { TEMPLATE(&$$, "%s [IY + %%#.2x]", $1); HANDLE($$,$2,1); FREE($1);}
        /*###################################################################################################################*/

        /* DJNZ */
        /*###################################################################################################################*/
        | DJNZ expr                            { TEMPLATE(&$$, "%s %%#.2x", $1); HANDLE($$,$2-PC-2,1); FREE($1);}
        /*###################################################################################################################*/

        /* EX series */
        /*###################################################################################################################*/
        | EX PDREG                            { TEMPLATE(&$$, "%s %s", $1, $2); HANDLE($$,0,0); FREE($1, $2);}
        | EX PDREG ',' IX                     { TEMPLATE(&$$, "%s %s, IX", $1, $2); HANDLE($$,0,0); FREE($1, $2);}
        | EX PDREG ',' IY                     { TEMPLATE(&$$, "%s %s, %s", $1, $2, $4); HANDLE($$,0,0); FREE($1, $2, $4);}
        | EX PDREG ',' DREG                   { TEMPLATE(&$$, "%s %s, %s", $1, $2, $4); HANDLE($$,0,0); FREE($1, $2, $4);}
        | EX DREG ',' DREG                    { TEMPLATE(&$$, "%s %s, %s", $1, $2, $4); HANDLE($$,0,0); FREE($1, $2, $4);}
        /*###################################################################################################################*/

        /* IM */
        /*###################################################################################################################*/
        | IM expr                             { TEMPLATE(&$$, "%s %jd", $1, $2); HANDLE($$,0,0); FREE($1);}
        /*###################################################################################################################*/

        /* IN */
        /*###################################################################################################################*/
        | IN PREG ',' PREG                   { TEMPLATE(&$$, "%s %s, %s", $1, $2, $4); HANDLE($$,0,0); FREE($1, $2, $4);}
        | IN REG ',' PREG                    { TEMPLATE(&$$, "%s %s, %s", $1, $2, $4); HANDLE($$,0,0); FREE($1, $2, $4);}
        | IN REG ',' opbrk expr clbrk            { TEMPLATE(&$$, "%s %s, [%%#.2x]", $1, $2); HANDLE($$,$5,1); FREE($1, $2);}
        /*###################################################################################################################*/

        /* INC */
        /*###################################################################################################################*/
        | INC REG                             { TEMPLATE(&$$, "%s %s", $1, $2); HANDLE($$,0,0); FREE($1, $2);}
        | INC DREG                             { TEMPLATE(&$$, "%s %s", $1, $2); HANDLE($$,0,0); FREE($1, $2);}
        | INC PDREG                             { TEMPLATE(&$$, "%s %s", $1, $2); HANDLE($$,0,0); FREE($1, $2);}
        | INC IX                             { TEMPLATE(&$$, "%s %s", $1, $2); HANDLE($$,0,0); FREE($1, $2);}
        | INC IY                             { TEMPLATE(&$$, "%s %s", $1, $2); HANDLE($$,0,0); FREE($1, $2);}
        | INC IYidx                { TEMPLATE(&$$, "%s [IY + %%#.2x]", $1); HANDLE($$,$2,1); FREE($1);}
        | INC IXidx                { TEMPLATE(&$$, "%s [IX + %%#.2x]", $1); HANDLE($$,$2,1); FREE($1);}
        /*###################################################################################################################*/

        /* JP */
        /*###################################################################################################################*/
        | JP expr                                  { UNSIGN16($2); TEMPLATE(&$$, "%s %%#.4x", $1); HANDLE($$,$2,2); FREE($1);}
        | JP opbrk IX clbrk                { TEMPLATE(&$$, "%s [%s]", $1, $3); HANDLE($$,0,0); FREE($1, $3);}
        | JP opbrk IY clbrk                { TEMPLATE(&$$, "%s [%s]", $1, $3); HANDLE($$,0,0); FREE($1, $3);}
        | JP PDREG                                  { TEMPLATE(&$$, "%s %s", $1, $2); HANDLE($$,0,0); FREE($1, $2);}
        | JP FLAG ',' expr                         { UNSIGN16($4); TEMPLATE(&$$, "%s %s, %%#.4x", $1, $2); HANDLE($$,$4,2); FREE($1, $2);}
        | JP REG ',' expr                         { UNSIGN16($4); TEMPLATE(&$$, "%s %s, %%#.4x", $1, $2); HANDLE($$,$4,2); FREE($1, $2);}
        /*###################################################################################################################*/

        /* JR */
        /*###################################################################################################################*/
        | JR expr                                  { TEMPLATE(&$$, "%s %%#.2x", $1); HANDLE($$, $2-PC-2,1); FREE($1);}
        | JR FLAG ',' expr                         { TEMPLATE(&$$, "%s %s, %%#.2x", $1, $2); HANDLE($$,$4-PC-2,1); FREE($1, $2);}
        | JR REG ',' expr                         { TEMPLATE(&$$, "%s %s, %%#.2x", $1, $2); HANDLE($$,$4-PC-2,1); FREE($1, $2);}
        /*###################################################################################################################*/

        /* LD */
        /*###################################################################################################################*/
        | LD opbrk expr clbrk ',' REG                  { UNSIGN16($3); TEMPLATE(&$$, "%s [%%#.4x], %s", $1,$6); HANDLE($$,$3,2); FREE($1,$6);}
        | LD opbrk expr clbrk ',' DREG                 { UNSIGN16($3); TEMPLATE(&$$, "%s [%%#.4x], %s", $1,$6); HANDLE($$,$3,2); FREE($1,$6);}
        | LD opbrk expr clbrk ',' IX                   { UNSIGN16($3); TEMPLATE(&$$, "%s [%%#.4x], %s", $1,$6); HANDLE($$,$3,2); FREE($1,$6);}
        | LD opbrk expr clbrk ',' IY                   { UNSIGN16($3); TEMPLATE(&$$, "%s [%%#.4x], %s", $1,$6); HANDLE($$,$3,2); FREE($1,$6);}
        | LD PDREG ',' REG                          { TEMPLATE(&$$, "%s %s, %s", $1,$2,$4); HANDLE($$,0,0); FREE($1,$2,$4);}
        | LD REG ',' PDREG                          { TEMPLATE(&$$, "%s %s, %s", $1,$2,$4); HANDLE($$,0,0); FREE($1,$2,$4);}
        | LD DREG ',' DREG                         { TEMPLATE(&$$, "%s %s, %s", $1,$2,$4); HANDLE($$,0,0); FREE($1,$2,$4);}
        | LD DREG ',' IX                           { TEMPLATE(&$$, "%s %s, %s", $1,$2,$4); HANDLE($$,0,0); FREE($1,$2,$4);}
        | LD DREG ',' IY                           { TEMPLATE(&$$, "%s %s, %s", $1,$2,$4); HANDLE($$,0,0); FREE($1,$2,$4);}
        | LD REG ',' IXidx                 { TEMPLATE(&$$, "%s %s, [IX + %%#.2x]", $1,$2); HANDLE($$,$4,1); FREE($1,$2);}
        | LD REG ',' IYidx                 { TEMPLATE(&$$, "%s %s, [IY + %%#.2x]", $1,$2); HANDLE($$,$4,1); FREE($1,$2);}
        | LD REG ',' REG                           { TEMPLATE(&$$, "%s %s, %s", $1,$2,$4); HANDLE($$,0,0); FREE($1,$2,$4);}
        | LD REG ',' expr                          { TEMPLATE(&$$, "%s %s, %%#.2x", $1,$2); HANDLE($$,$4,1); FREE($1,$2);}
        | LD REG ',' opbrk expr clbrk                  { TEMPLATE(&$$, "%s %s, [%%#.4x]", $1,$2); HANDLE($$,$5,2); FREE($1,$2);}
        | LD DREG ',' opbrk expr clbrk                 { TEMPLATE(&$$, "%s %s, [%%#.4x]", $1,$2); HANDLE($$,$5,2); FREE($1,$2);}
        | LD DREG ',' expr                         { TEMPLATE(&$$, "%s %s, %%#.4x", $1,$2); HANDLE($$,$4,2); FREE($1,$2);}
        | LD IX ',' expr                           { TEMPLATE(&$$, "%s %s, %%#.4x", $1,$2); HANDLE($$,$4,2); FREE($1,$2);}
        | LD IY ',' expr                           { TEMPLATE(&$$, "%s %s, %%#.4x", $1,$2); HANDLE($$,$4,2); FREE($1,$2);}
        | LD IX ',' opbrk expr clbrk                   { TEMPLATE(&$$, "%s %s, [%%#.4x]", $1,$2); HANDLE($$,$5,2); FREE($1,$2);}
        | LD IY ',' opbrk expr clbrk                   { TEMPLATE(&$$, "%s %s, [%%#.4x]", $1,$2); HANDLE($$,$5,2); FREE($1,$2);}
        | LD PDREG ',' expr                         { TEMPLATE(&$$, "%s %s, %%#.2x", $1,$2); HANDLE($$,$4,1); FREE($1,$2);}
        | LD IXidx ',' REG               { TEMPLATE(&$$, "%s [IX + %%#.2x], %s", $1,$4); HANDLE($$,$2,1); FREE($1,$4);}
        | LD IYidx ',' REG               { TEMPLATE(&$$, "%s [IY + %%#.2x], %s", $1,$4); HANDLE($$,$2,1); FREE($1,$4);}
        | LD IXidx ',' expr              { TEMPLATE(&$$, "%s [IX + %%#.2x], %%#.2x", $1); HANDLE($$,(uint16_t)$4<<8|$2,1); FREE($1);}
        | LD IYidx ',' expr              { TEMPLATE(&$$, "%s [IY + %%#.2x], %%#.2x", $1); HANDLE($$,(uint16_t)$4<<8|$2,1); FREE($1);}
        | LD REG ',' RES expr ',' IXidx  { TEMPLATE(&$$, "%s %s, %s %jd, [IX + %%#.2x]", $1,$2,$4,$5); HANDLE($$,$7,1); FREE($1,$2,$4);}
        | LD REG ',' RES expr ',' IYidx  { TEMPLATE(&$$, "%s %s, %s %jd, [IY + %%#.2x]", $1,$2,$4,$5); HANDLE($$,$7,1); FREE($1,$2,$4);}
        | LD REG ',' SET expr ',' IXidx  { TEMPLATE(&$$, "%s %s, %s %jd, [IX + %%#.2x]", $1,$2,$4,$5); HANDLE($$,$7,1); FREE($1,$2,$4);}
        | LD REG ',' SET expr ',' IYidx  { TEMPLATE(&$$, "%s %s, %s %jd, [IY + %%#.2x]", $1,$2,$4,$5); HANDLE($$,$7,1); FREE($1,$2,$4);}
        | LD REG ',' RL IXidx            { TEMPLATE(&$$, "%s %s, %s [IX + %%#.2x]", $1,$2,$4); HANDLE($$,$5,1); FREE($1,$2,$4);}
        | LD REG ',' RL IYidx            { TEMPLATE(&$$, "%s %s, %s [IY + %%#.2x]", $1,$2,$4); HANDLE($$,$5,1); FREE($1,$2,$4);}
        | LD REG ',' RLC IXidx           { TEMPLATE(&$$, "%s %s, %s [IX + %%#.2x]", $1,$2,$4); HANDLE($$,$5,1); FREE($1,$2,$4);}
        | LD REG ',' RLC IYidx           { TEMPLATE(&$$, "%s %s, %s [IY + %%#.2x]", $1,$2,$4); HANDLE($$,$5,1); FREE($1,$2,$4);}
        | LD REG ',' RR IXidx            { TEMPLATE(&$$, "%s %s, %s [IX + %%#.2x]", $1,$2,$4); HANDLE($$,$5,1); FREE($1,$2,$4);}
        | LD REG ',' RR IYidx            { TEMPLATE(&$$, "%s %s, %s [IY + %%#.2x]", $1,$2,$4); HANDLE($$,$5,1); FREE($1,$2,$4);}
        | LD REG ',' RRC IXidx           { TEMPLATE(&$$, "%s %s, %s [IX + %%#.2x]", $1,$2,$4); HANDLE($$,$5,1); FREE($1,$2,$4);}
        | LD REG ',' RRC IYidx           { TEMPLATE(&$$, "%s %s, %s [IY + %%#.2x]", $1,$2,$4); HANDLE($$,$5,1); FREE($1,$2,$4);}
        | LD REG ',' SLA IXidx           { TEMPLATE(&$$, "%s %s, %s [IX + %%#.2x]", $1,$2,$4); HANDLE($$,$5,1); FREE($1,$2,$4);}
        | LD REG ',' SLA IYidx           { TEMPLATE(&$$, "%s %s, %s [IY + %%#.2x]", $1,$2,$4); HANDLE($$,$5,1); FREE($1,$2,$4);}
        | LD REG ',' SLL IXidx           { TEMPLATE(&$$, "%s %s, %s [IX + %%#.2x]", $1,$2,$4); HANDLE($$,$5,1); FREE($1,$2,$4);}
        | LD REG ',' SLL IYidx           { TEMPLATE(&$$, "%s %s, %s [IY + %%#.2x]", $1,$2,$4); HANDLE($$,$5,1); FREE($1,$2,$4);}
        | LD REG ',' SRA IXidx           { TEMPLATE(&$$, "%s %s, %s [IX + %%#.2x]", $1,$2,$4); HANDLE($$,$5,1); FREE($1,$2,$4);}
        | LD REG ',' SRA IYidx           { TEMPLATE(&$$, "%s %s, %s [IY + %%#.2x]", $1,$2,$4); HANDLE($$,$5,1); FREE($1,$2,$4);}
        | LD REG ',' SRL IXidx           { TEMPLATE(&$$, "%s %s, %s [IX + %%#.2x]", $1,$2,$4); HANDLE($$,$5,1); FREE($1,$2,$4);}
        | LD REG ',' SRL IYidx           { TEMPLATE(&$$, "%s %s, %s [IY + %%#.2x]", $1,$2,$4); HANDLE($$,$5,1); FREE($1,$2,$4);}
        /*###################################################################################################################*/

        /* RES */
        /*###################################################################################################################*/
        | RES BIT8 ',' IXidx             { TEMPLATE(&$$, "%s %jd, [IX + %%#.2x]", $1, $2); HANDLE($$,$4,1); FREE($1);}
        | RES BIT8 ',' IYidx             { TEMPLATE(&$$, "%s %jd, [IY + %%#.2x]", $1, $2); HANDLE($$,$4,1); FREE($1);}
        | RES BIT8 ',' REG                         { TEMPLATE(&$$, "%s %jd, %s", $1, $2, $4); HANDLE($$,0,0); FREE($1, $4);}
        | RES BIT8 ',' PDREG                        { TEMPLATE(&$$, "%s %jd, %s", $1, $2, $4); HANDLE($$,0,0); FREE($1, $4);}
        /*###################################################################################################################*/


        /* OR */
        /*###################################################################################################################*/
        | OR REG                          { TEMPLATE(&$$, "%s %s", $1, $2); HANDLE($$,0,0); FREE($1, $2);}
        | OR PDREG                         { TEMPLATE(&$$, "%s %s", $1, $2); HANDLE($$,0,0); FREE($1, $2);}
        | OR expr                         { UNSIGN8($2); TEMPLATE(&$$, "%s %%#.2x", $1); HANDLE($$,$2,1); FREE($1);}
        | OR IXidx              { TEMPLATE(&$$, "%s [IY + %%#.2x]", $1); HANDLE($$,$2,1); FREE($1);}
        | OR IYidx              { TEMPLATE(&$$, "%s [IY + %%#.2x]", $1); HANDLE($$,$2,1); FREE($1);}
        /*###################################################################################################################*/

        /* POP */
        /*###################################################################################################################*/
        | OUT opbrk expr clbrk ',' REG        { TEMPLATE(&$$, "%s [%%#.2x], %s", $1, $6); HANDLE($$,$3,1); FREE($1, $6);}
        | OUT PREG ',' expr               { TEMPLATE(&$$, "%s %s, %jd", $1, $2, $4); HANDLE($$,0,0); FREE($1, $2);}
        | OUT PREG ',' REG                { TEMPLATE(&$$, "%s %s, %s", $1, $2, $4); HANDLE($$,0,0); FREE($1, $2, $4);}
        /*###################################################################################################################*/

        /* POP */
        /*###################################################################################################################*/
        | POP DREG                          { TEMPLATE(&$$, "%s %s", $1, $2); HANDLE($$,0,0); FREE($1, $2);}
        | POP IX                          { TEMPLATE(&$$, "%s %s", $1, $2); HANDLE($$,0,0); FREE($1, $2);}
        | POP IY                          { TEMPLATE(&$$, "%s %s", $1, $2); HANDLE($$,0,0); FREE($1, $2);}
        /*###################################################################################################################*/

        /* PUSH */
        /*###################################################################################################################*/
        | PUSH DREG                          { TEMPLATE(&$$, "%s %s", $1, $2); HANDLE($$,0,0); FREE($1, $2);}
        | PUSH IX                          { TEMPLATE(&$$, "%s %s", $1, $2); HANDLE($$,0,0); FREE($1, $2);}
        | PUSH IY                          { TEMPLATE(&$$, "%s %s", $1, $2); HANDLE($$,0,0); FREE($1, $2);}
        /*###################################################################################################################*/

        /* RET */
        /*###################################################################################################################*/
        | RET                             { TEMPLATE(&$$, "%s", $1); HANDLE($$,0,0); FREE($1);}
        | RET REG                         { TEMPLATE(&$$, "%s %s", $1, $2); HANDLE($$,0,0); FREE($1, $2);}
        | RET FLAG                        { TEMPLATE(&$$, "%s %s", $1, $2); HANDLE($$,0,0); FREE($1, $2);}
        /*###################################################################################################################*/

        /* RL */
        /*###################################################################################################################*/
        | RL REG                          { TEMPLATE(&$$, "%s %s", $1, $2); HANDLE($$,0,0); FREE($1, $2);}
        | RL PDREG                          { TEMPLATE(&$$, "%s %s", $1, $2); HANDLE($$,0,0); FREE($1, $2);}
        | RL IXidx              { TEMPLATE(&$$, "%s [IY + %%#.2x]", $1); HANDLE($$,$2,1); FREE($1);}
        | RL IYidx              { TEMPLATE(&$$, "%s [IY + %%#.2x]", $1); HANDLE($$,$2,1); FREE($1);}
        /*###################################################################################################################*/

        /* RLC */
        /*###################################################################################################################*/
        | RLC REG                          { TEMPLATE(&$$, "%s %s", $1, $2); HANDLE($$,0,0); FREE($1, $2);}
        | RLC PDREG                          { TEMPLATE(&$$, "%s %s", $1, $2); HANDLE($$,0,0); FREE($1, $2);}
        | RLC IXidx              { TEMPLATE(&$$, "%s [IY + %%#.2x]", $1); HANDLE($$,$2,1); FREE($1);}
        | RLC IYidx              { TEMPLATE(&$$, "%s [IY + %%#.2x]", $1); HANDLE($$,$2,1); FREE($1);}
        /*###################################################################################################################*/

        /* RR */
        /*###################################################################################################################*/
        | RR REG                          { TEMPLATE(&$$, "%s %s", $1, $2); HANDLE($$,0,0); FREE($1, $2);}
        | RR PDREG                          { TEMPLATE(&$$, "%s %s", $1, $2); HANDLE($$,0,0); FREE($1, $2);}
        | RR IXidx              { TEMPLATE(&$$, "%s [IY + %%#.2x]", $1); HANDLE($$,$2,1); FREE($1);}
        | RR IYidx              { TEMPLATE(&$$, "%s [IY + %%#.2x]", $1); HANDLE($$,$2,1); FREE($1);}
        /*###################################################################################################################*/

        /* RRC */
        /*###################################################################################################################*/
        | RRC REG                          { TEMPLATE(&$$, "%s %s", $1, $2); HANDLE($$,0,0); FREE($1, $2);}
        | RRC PDREG                          { TEMPLATE(&$$, "%s %s", $1, $2); HANDLE($$,0,0); FREE($1, $2);}
        | RRC IXidx              { TEMPLATE(&$$, "%s [IY + %%#.2x]", $1); HANDLE($$,$2,1); FREE($1);}
        | RRC IYidx              { TEMPLATE(&$$, "%s [IY + %%#.2x]", $1); HANDLE($$,$2,1); FREE($1);}
        /*###################################################################################################################*/

        /* RST */
        /*###################################################################################################################*/
        | RST expr                         { UNSIGN8($2); TEMPLATE(&$$, "%s %d", $1, (uint8_t)$2); HANDLE($$,0,0); FREE($1);}
        /*###################################################################################################################*/

        /* SBC */
        /*###################################################################################################################*/
        | SBC REG ',' REG                          { TEMPLATE(&$$, "%s %s, %s", $1, $2,$4); HANDLE($$,0,0); FREE($1, $2, $4);}
        | SBC DREG ',' DREG                          { TEMPLATE(&$$, "%s %s, %s", $1, $2,$4); HANDLE($$,0,0); FREE($1, $2, $4);}
        | SBC REG ',' PDREG                         { TEMPLATE(&$$, "%s %s, %s", $1, $2,$4); HANDLE($$,0,0); FREE($1, $2, $4);}
        | SBC REG ',' expr                         { TEMPLATE(&$$, "%s %s, %%#.2x", $1, $2); HANDLE($$,$4,1); FREE($1, $2);}
        | SBC REG ',' IXidx              { TEMPLATE(&$$, "%s %s, [IY + %%#.2x]", $1,$2); HANDLE($$,$4,1); FREE($1, $2);}
        | SBC REG ',' IYidx              { TEMPLATE(&$$, "%s %s, [IY + %%#.2x]", $1,$2); HANDLE($$,$4,1); FREE($1, $2);}
        /*###################################################################################################################*/

        /* SET */
        /*###################################################################################################################*/
        | SET BIT8 ',' IXidx                    { TEMPLATE(&$$, "%s %jd, [IX + %%#.2x]", $1, $2); HANDLE($$,$4,1); FREE($1);}
        | SET BIT8 ',' IYidx                    { TEMPLATE(&$$, "%s %jd, [IY + %%#.2x]", $1, $2); HANDLE($$,$4,1); FREE($1);}
        | SET BIT8 ',' REG                    { TEMPLATE(&$$, "%s %jd, %s", $1, $2, $4); HANDLE($$,0,0); FREE($1, $4);}
        | SET BIT8 ',' PDREG                    { TEMPLATE(&$$, "%s %jd, %s", $1, $2, $4); HANDLE($$,0,0); FREE($1, $4);}
        /*###################################################################################################################*/

        /* SLA */
        /*###################################################################################################################*/
        | SLA REG                          { TEMPLATE(&$$, "%s %s", $1, $2); HANDLE($$,0,0); FREE($1, $2);}
        | SLA PDREG                          { TEMPLATE(&$$, "%s %s", $1, $2); HANDLE($$,0,0); FREE($1, $2);}
        | SLA IXidx              { TEMPLATE(&$$, "%s [IY + %%#.2x]", $1); HANDLE($$,$2,1); FREE($1);}
        | SLA IYidx              { TEMPLATE(&$$, "%s [IY + %%#.2x]", $1); HANDLE($$,$2,1); FREE($1);}
        /*###################################################################################################################*/

        /* SLL */
        /*###################################################################################################################*/
        | SLL REG                          { TEMPLATE(&$$, "%s %s", $1, $2); HANDLE($$,0,0); FREE($1, $2);}
        | SLL PDREG                          { TEMPLATE(&$$, "%s %s", $1, $2); HANDLE($$,0,0); FREE($1, $2);}
        | SLL IXidx              { TEMPLATE(&$$, "%s [IY + %%#.2x]", $1); HANDLE($$,$2,1); FREE($1);}
        | SLL IYidx              { TEMPLATE(&$$, "%s [IY + %%#.2x]", $1); HANDLE($$,$2,1); FREE($1);}
        /*###################################################################################################################*/

        /* SRA */
        /*###################################################################################################################*/
        | SRA REG                          { TEMPLATE(&$$, "%s %s", $1, $2); HANDLE($$,0,0); FREE($1, $2);}
        | SRA PDREG                          { TEMPLATE(&$$, "%s %s", $1, $2); HANDLE($$,0,0); FREE($1, $2);}
        | SRA IXidx              { TEMPLATE(&$$, "%s [IY + %%#.2x]", $1); HANDLE($$,$2,1); FREE($1);}
        | SRA IYidx              { TEMPLATE(&$$, "%s [IY + %%#.2x]", $1); HANDLE($$,$2,1); FREE($1);}
        /*###################################################################################################################*/

        /* SRL */
        /*###################################################################################################################*/
        | SRL REG                          { TEMPLATE(&$$, "%s %s", $1, $2); HANDLE($$,0,0); FREE($1, $2);}
        | SRL PDREG                          { TEMPLATE(&$$, "%s %s", $1, $2); HANDLE($$,0,0); FREE($1, $2);}
        | SRL IXidx              { TEMPLATE(&$$, "%s [IY + %%#.2x]", $1); HANDLE($$,$2,1); FREE($1);}
        | SRL IYidx              { TEMPLATE(&$$, "%s [IY + %%#.2x]", $1); HANDLE($$,$2,1); FREE($1);}
        /*###################################################################################################################*/

        /* SUB */
        /*###################################################################################################################*/
        | SUB expr                          { TEMPLATE(&$$, "%s %%#.2x", $1); HANDLE($$,$2,1); FREE($1);}
        | SUB PDREG                          { TEMPLATE(&$$, "%s %s", $1, $2); HANDLE($$,0,0); FREE($1, $2);}
        | SUB REG                          { TEMPLATE(&$$, "%s %s", $1, $2); HANDLE($$,0,0); FREE($1, $2);}
        | SUB IXidx              { TEMPLATE(&$$, "%s [IY + %%#.2x]", $1); HANDLE($$,$2,1); FREE($1);}
        | SUB IYidx              { TEMPLATE(&$$, "%s [IY + %%#.2x]", $1); HANDLE($$,$2,1); FREE($1);}
        /*###################################################################################################################*/

        /* XOR */
        /*###################################################################################################################*/
        | XOR expr                          { TEMPLATE(&$$, "%s %%#.2x", $1); HANDLE($$,$2,1); FREE($1);}
        | XOR PDREG                          { TEMPLATE(&$$, "%s %s", $1, $2); HANDLE($$,0,0); FREE($1, $2);}
        | XOR REG                          { TEMPLATE(&$$, "%s %s", $1, $2); HANDLE($$,0,0); FREE($1, $2);}
        | XOR IXidx              { TEMPLATE(&$$, "%s [IY + %%#.2x]", $1); HANDLE($$,$2,1); FREE($1);}
        | XOR IYidx              { TEMPLATE(&$$, "%s [IY + %%#.2x]", $1); HANDLE($$,$2,1); FREE($1);}
        /*###################################################################################################################*/
;
%%
