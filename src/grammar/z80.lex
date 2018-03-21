%{
#include <common.h>
ASMSTYPE asmlval;
void asm_load_buffer (const char* input);
extern int asmnerrs;
int asmcolumn = 0;

extern int asmlex(void);
extern YY_BUFFER_STATE asm_scan_buffer(char *, size_t);
#define YY_USER_ACTION current_line=asmlineno;asmcolumn+=asmleng;
#define SAVE_CTX strncpy(asmlval.str, asmtext, MAX_TOKEN_SIZE )
%}

%option backup
%option prefix="asm"
/* %option debug */
%option noyywrap
%option yylineno
%option ecs
%option align
%array

digit             [0-9]
alpha             [A-Za-z]+

dec               {digit}+
hex               0[xX]{1}[0-9A-Fa-f]+|[0-9A-Fa-f]+[hH]{1}|0[0-9A-Fa-f]+[hH]{1}|[$]{1}[0-9A-Fa-f]+
oct               o{1}[0-7]+
bcd               [01]*bcd
bin               0b[01]+|%[01]+
num               {dec}|{hex}|{oct}|{bcd}|{bin}|{char}

%x SKIP
%%

<INITIAL><<EOF>>               { return END_OF_FILE;}
"/*"                           {BEGIN(SKIP);}
<SKIP>.
<SKIP>\n
<SKIP>"*/"                {BEGIN(INITIAL);}
{dec}                 {
                        asmlval.var = strtoimax(asmtext, 0, 10);
                        if(asmlval.var <= 0x7)
                            return BIT8;
                        else if(asmlval.var <= 0xFF)
                            return WORD;
                        else if (asmlval.var <= 0xFFFF)
                            return DWORD;
                        else if (asmlval.var <= 0xFFFFFFFF)
                            return QWORD;
                        return DQWORD;
                      }

{hex}                 {
                        if('$' == asmtext[0])
                        {
                            asmtext[0] = '0';
                        }
                        asmlval.var = strtoimax(asmtext, 0, 16);
                        if(asmlval.var <= 0x7)
                            return BIT8;
                        else if(asmlval.var <= 0xFF)
                            return WORD;
                        else if (asmlval.var <= 0xFFFF)
                            return DWORD;
                        else if (asmlval.var <= 0xFFFFFFFF)
                            return QWORD;
                        return DQWORD;
                      }

{bin}                 {
                        if ('%' == asmtext[0])
                        {
                            asmlval.var = strtoimax(asmtext + 1, 0, 2);
                        }
                        else
                        {
                            asmlval.var = strtoimax(asmtext + 2, 0, 2);
                        }

                        if(asmlval.var <= 0x7)
                            return BIT8;
                        else if(asmlval.var <= 0xFF)
                            return WORD;
                        else if (asmlval.var <= 0xFFFF)
                            return DWORD;
                        else if (asmlval.var <= 0xFFFFFFFF)
                            return QWORD;
                        return DQWORD;
                      }

{oct}                 {
                        asmlval.var = strtoimax(asmtext, 0, 8);
                        if(asmlval.var <= 0x7)
                            return BIT8;
                        else if(asmlval.var <= 0xFF)
                            return WORD;
                        else if (asmlval.var <= 0xFFFF)
                            return DWORD;
                        else if (asmlval.var <= 0xFFFFFFFF)
                            return QWORD;
                        return DQWORD;
                      }

['][\x00-\x7F][']     {
                        asmlval.var = asmtext[1];
                        return WORD;
                      }


^[_a-zA-Z][_a-zA-Z0-9]+":"      { add_label(asmtext, PC);}

"#"                   |
";"                   |
"//"                  |
";".*                 {}

%{/* Instructions */%}

"adc"               { SAVE_CTX; return ADC; }
"add"               { SAVE_CTX; return ADD; }
"and"               { SAVE_CTX; return AND; }
"bit"               { SAVE_CTX; return BIT; }
"call"              { SAVE_CTX; return CALL; }
"ccf"               { SAVE_CTX; return CCF; }
"cp"                { SAVE_CTX; return CP; }
"cpd"               { SAVE_CTX; return CPD; }
"cpdr"              { SAVE_CTX; return CPDR; }
"cpi"               { SAVE_CTX; return CPI; }
"cpir"              { SAVE_CTX; return CPIR; }
"cpl"               { SAVE_CTX; return CPL; }
"daa"               { SAVE_CTX; return DAA; }
"dec"               { SAVE_CTX; return DEC; }
"di"                { SAVE_CTX; return DI; }
"djnz"              { SAVE_CTX; return DJNZ; }
"ei"                { SAVE_CTX; return EI; }
"ex"                { SAVE_CTX; return EX; }
"exx"               { SAVE_CTX; return EXX; }
"halt"              { SAVE_CTX; return HALT; }
"im"                { SAVE_CTX; return IM; }
"in"                { SAVE_CTX; return IN; }
"inc"               { SAVE_CTX; return INC; }
"ind"               { SAVE_CTX; return IND; }
"indr"              { SAVE_CTX; return INDR; }
"ini"               { SAVE_CTX; return INI; }
"inir"              { SAVE_CTX; return INIR; }
"jp"                { SAVE_CTX; return JP; }
"jr"                { SAVE_CTX; return JR; }
"ld"                { SAVE_CTX; return LD; }
"ldd"               { SAVE_CTX; return LDD; }
"lddr"              { SAVE_CTX; return LDDR; }
"ldi"               { SAVE_CTX; return LDI; }
"ldir"              { SAVE_CTX; return LDIR; }
"neg"               { SAVE_CTX; return NEG; }
"nop"               { SAVE_CTX; return NOP; }
"or"                { SAVE_CTX; return OR; }
"otdr"              { SAVE_CTX; return OTDR; }
"otir"              { SAVE_CTX; return OTIR; }
"out"               { SAVE_CTX; return OUT; }
"outd"              { SAVE_CTX; return OUTD; }
"outi"              { SAVE_CTX; return OUTI; }
"pop"               { SAVE_CTX; return POP; }
"push"              { SAVE_CTX; return PUSH; }
"res"               { SAVE_CTX; return RES; }
"ret"               { SAVE_CTX; return RET; }
"reti"              { SAVE_CTX; return RETN; }
"retn"              { SAVE_CTX; return RETN; }
"rl"                { SAVE_CTX; return RL; }
"rla"               { SAVE_CTX; return RLA; }
"rlc"               { SAVE_CTX; return RLC; }
"rld"               { SAVE_CTX; return RLD; }
"rlca"              { SAVE_CTX; return RLCA; }
"rr"                { SAVE_CTX; return RR; }
"rra"               { SAVE_CTX; return RRA; }
"rrc"               { SAVE_CTX; return RRC; }
"rrd"               { SAVE_CTX; return RRD; }
"rrca"              { SAVE_CTX; return RRCA; }
"rst"               { SAVE_CTX; return RST; }
"sbc"               { SAVE_CTX; return SBC; }
"scf"               { SAVE_CTX; return SCF; }
"set"               { SAVE_CTX; return SET;}
"sla"               { SAVE_CTX; return SLA; }
"sll"               { SAVE_CTX; return SLL; }
"sra"               { SAVE_CTX; return SRA; }
"srl"               { SAVE_CTX; return SRL; }
"sub"               { SAVE_CTX; return SUB; }
"xor"               { SAVE_CTX; return XOR; }


%{/* 8bit registers */%}

"A"                 |
"B"                 |
"C"                 |
"D"                 |
"E"                 |
"F"                 |
"H"                 |
"I"                 |
"L"                 |
"R"                 |
"XL"                |
"XH"                |
"YL"                |
"YH"                { SAVE_CTX; return REG;}

%{/* 16bit registers */%}

"AF"                |
"AF'"               |
"BC"                |
"DE"                |
"HL"                |
"PC"                |
"SP"                { SAVE_CTX; return DREG;}

"IX"                { SAVE_CTX;return IX;}
"IY"                { SAVE_CTX;return IY;}

%{/* CPU flags */%}

%{/* C flag is substituted by C reg in order to avoid clash */%}

"Z"|"S"|"M"|"N"|"NZ"|"NC"|"P"|"PO"|"PE"                      { SAVE_CTX; return FLAG;}
"+"|"-"|"/"|"*"|"&"|"\""|"["|"("|"]"|")"|","|"%"                 {return asmtext[0];}

"$"                 {return ASMPC;}
"$$"                {return ASMORG;}
"\n"                {return NL;}

".byte"|"defb"|"db"              {return DEFB;}
".word"|"defw"|"dw"              {return DEFW;}

".org"|"org"        {return ORG;}
".equ"|"equ"        {return EQU;}
".end"              {return END;}
"defm"              {return TEXT;}

[a-zA-Z0-9]+      {  SAVE_CTX; return STRING;}
["]([\x00-\x7F]{-}["\\\n]|\\(.|\n))*["]        { SAVE_CTX; return STRING;}
.
%%

void asmerror(const char *error_txt)
{

    #ifndef __WIN32
    printf("\x1B[31m%d error%s detected, line %d: %s near \"%s\"\x1B[0m\n", asmnerrs, 1 == asmnerrs ? "":"s" , asmlineno, error_txt, asmtext);
    #else
    printf("%d error%s detected, line %d: %s near \"%s\"\n", asmnerrs, 1 == asmnerrs ? "":"s" , asmlineno, error_txt, asmtext);
    #endif
}

void asm_load_buffer (const char *input)
{
    asmnerrs =0;
    asmlineno=0;
    asm_delete_buffer( YY_CURRENT_BUFFER );
    asm_scan_string(input);
}
