/**
 *
 * @file   assembler.c
 * @date   16.03.2018
 * @license This project is released under the GPL 2 license.
 * @brief Assembler
 *
 */

#include <common.h>
#include <assembler.h>
#include <tap.h>
#include <ihex.h>

RUNPASS run_pass = PASS1;
bool abort_on_error = true;
uint16_t PC = 0;
uint16_t DATA_PC = 0;
uint16_t CURRENT_ORG = 0;
uint16_t PROG_START = 0xFFFF;
uint8_t prog[PROG_SIZE] = {0};
size_t assembled_bytes = 0;
user_label *labels = NULL;
bool label_unresolved = false;

/* semantic errors reported via error_print(); nonzero fails the assembly */
static int semantic_errors = 0;

/* highest address written + 1, i.e. the size of the emitted image */
static uint32_t prog_end = 0;
/* set once output has reached the top of the address space */
static bool address_overflow = false;
static bool overflow_reported = false;

enum
{
    ERROR_RELJM_RANGE = 0,
    ERROR_MNEM_NOT_FOUND
};

const char *error_texts[] = {"Relative jump is out of range: %jd [must be between -128:127]\n",
                             "Mnemonic was not found: \"%s\"\n", NULL};

/**
@brief Write one byte at *cursor and advance it.

Refuses to wrap past 0xFFFF: the old code let PC roll over to 0, which both
wrote out of bounds (prog[] was one byte short of the address space) and
silently truncated the output image.

@return true if the byte was stored
*/
static bool emit_byte(uint16_t *cursor, uint8_t byte)
{
    if (address_overflow)
    {
        if (!overflow_reported)
        {
            overflow_reported = true;
            error_print("Program does not fit into the 64K address space\n");
        }
        return false;
    }

    prog[*cursor] = byte;
    if (*cursor + 1u > prog_end)
    {
        prog_end = *cursor + 1u;
    }

    if (PROG_SIZE - 1 == *cursor)
    {
        /* a byte at 0xFFFF is legal; only the one after it has nowhere to go */
        address_overflow = true;
    }
    else
    {
        ++(*cursor);
    }
    return true;
}

/**
@brief Handle the ORG directive: set the assembly address.
*/
void set_origin(intmax_t address)
{
    if (address < 0 || address > 0xFFFF)
    {
        error_print("Origin %jd is outside the 64K address space\n", address);
        return;
    }

    CURRENT_ORG = PC = (uint16_t)address;
    if (CURRENT_ORG < PROG_START)
    {
        PROG_START = CURRENT_ORG;
    }
    /* output may continue below the top again */
    address_overflow = false;
}

/**
@brief Report an operand that does not fit the field the instruction encodes it in.

Accepts both the signed and the unsigned reading of the field, so -1 and 0xFF
are equally valid for a byte operand.

This only diagnoses: the caller emits the same number of bytes either way. An
expression containing a label that pass 1 has not seen yet evaluates to a
placeholder that may well be out of range, and dropping those bytes would move
every later label (the assembly fails on the pass-2 report instead).
*/
static void diagnose_operand_range(intmax_t data, size_t size, const char *mnemo)
{
    intmax_t low = (1 == size) ? -128 : -32768;
    intmax_t high = (1 == size) ? 0xFF : 0xFFFF;

    if (data < low || data > high)
    {
        error_print("Value %jd does not fit in %zu byte(s): \"%s\"\n", data, size, mnemo);
    }
}

/* use binary search algorithm, be sure that opcode table sorted by mnemo field */
const opcode_table *find_opcode(char *instruction)
{
    int i, i_low, i_high, cmp;

    /* get latest valid index */
    i_high = opcode_tab_count - 1;
    i_low = cmp = i = 0;

    /* find sorted array entry */
    while (i_low <= i_high)
    {
        i = (i_low + i_high) / 2;
        cmp = strcasecmp(instruction, opcode_tab[i].mnemo);
        if (!cmp)
        {
            cmp = 0 - opcode_tab[i].duplicate;
        }
        if (!cmp)
        {
            return &opcode_tab[i];
        }
        if (cmp < 0)
        {
            i_high = i - 1;
        }
        else
        {
            i_low = i + 1;
        }
    }
    return (void *)0;
}

void debug_print(const char *format, ...)
{
    if (!verbose)
    {
        return;
    }
    char *string;
    va_list args;
    va_start(args, format);
    if (0 > vasprintf(&string, format, args))
    {
        string = NULL;
    }

    va_end(args);
    if (string)
    {
        printf("Line %d: %s", current_line, string);
        free(string);
    }
}

void error_print(const char *format, ...)
{
    /* Pass 1 works with an incomplete label table, so anything it considers
       wrong is re-checked (and reported) on pass 2. Reporting only there also
       keeps every diagnostic from being printed twice. */
    if (PASS2 != run_pass)
    {
        return;
    }
    ++semantic_errors;
    char *string;
    va_list args;
    va_start(args, format);
    if (0 > vasprintf(&string, format, args))
    {
        string = NULL;
    }
    va_end(args);
    if (string)
    {
        printf("Line %d: %s", current_line, string);
        free(string);
    }
}

bool check_relative_jump(intmax_t destination)
{
    return destination <= 127 && destination >= -128;
}

bool check_double_argumented(uint16_t opcode)
{
    switch (opcode)
    {
    case 0xDD36:
    case 0xFD36:
        return true;
    default:
        return false;
    }
}

bool is_single(uint32_t opcode)
{
    return opcode <= 0xFF;
}

bool is_prefixed(uint32_t opcode)
{
    return opcode <= 0xFFFF;
}

bool is_double_prefixed(uint32_t opcode)
{
    return opcode > 0xFFFF;
}

/**
@brief Main function that handles instruction extracted by Bison.
@param result Resulted template of instruction created by Bison's grammar rule
@param data Immediate data for instruction if needed.
@param size Size of the data parameter in bytes. If equals 0 - no data present.
@return 1 on success, 0 on error
*/
int handle_instruction(char *instruction, intmax_t data, size_t size)
{
    if ('*' == *instruction)
    {
        return ASM_ERROR;
    }
    if (PASS1 == run_pass)
    {
        data = 0;
    }

    const opcode_table *new_opc = find_opcode(instruction);
    if (NULL == new_opc)
    {
        if (PASS2 == run_pass)
        {
            error_print(error_texts[ERROR_MNEM_NOT_FOUND], instruction);
            return ASM_ERROR;
        }
        else
        {
            return ASM_OK;
        }
    }
    /* Handle cases where relative jump is pointed to label */
    if (new_opc->reljmp && PASS2 == run_pass && !check_relative_jump(data))
    {
        error_print(error_texts[ERROR_RELJM_RANGE], data);
        return ASM_ERROR;
    }
    /* DD36/FD36 pack a displacement and an immediate into one 16-bit value */
    bool wide_operand = (2 == size) || check_double_argumented(new_opc->opcode);
    if (size && !new_opc->reljmp)
    {
        diagnose_operand_range(data, wide_operand ? 2 : 1, new_opc->mnemo);
    }

    uint16_t start = PC;

    /* Basic one-byte opcodes */
    if (is_single(new_opc->opcode))
    {
        emit_byte(&PC, new_opc->opcode & 0xFF);
    }
    /* Single prefixed */
    else if (is_prefixed(new_opc->opcode))
    {
        emit_byte(&PC, new_opc->opcode >> 8);
        emit_byte(&PC, new_opc->opcode & 0xFF);
    }
    /* Double prefixed: prefix, prefix, displacement, opcode */
    else
    {
        if (PASS2 == run_pass && verbose)
        {
            printf("%#.4x: ", PC);
            printf(new_opc->mnemo, data);
            puts("");
        }
        emit_byte(&PC, new_opc->opcode >> 16);
        emit_byte(&PC, new_opc->opcode >> 8);
        emit_byte(&PC, data & 0xFF);
        emit_byte(&PC, new_opc->opcode & 0xFF);
        return ASM_OK;
    }

    if (!size)
    {
        if (PASS2 == run_pass && verbose)
        {
            printf("%#.4x: ", start);
            printf("%s\n", new_opc->mnemo);
        }
        return ASM_OK;
    }
    if (PASS2 == run_pass && verbose)
    {
        printf("%#.4x: ", start);
        printf(new_opc->mnemo, (uint16_t)data);
        puts("");
    }
    emit_byte(&PC, data & 0xFF);
    if (wide_operand)
    {
        emit_byte(&PC, (data >> 8) & 0xFF);
    }
    return ASM_OK;
}

/**
@brief Define word(s): emit a 16-bit little-endian constant
*/
void defw(intmax_t data)
{
    diagnose_operand_range(data, 2, "defw");
    emit_byte(&DATA_PC, data & 0xFF);
    emit_byte(&DATA_PC, (data >> 8) & 0xFF);
}

/**
@brief Define byte(s): emit an 8-bit constant
*/
void defb(intmax_t data)
{
    diagnose_operand_range(data, 1, "defb");
    emit_byte(&DATA_PC, data & 0xFF);
}

static uint8_t hex2val(char a)
{
    return (a > '9') ? (uint8_t)((a & 0xDFu) - 0x37u) : (uint8_t)(a - '0');
}

static char process_backslash(char *s, int *index)
{
    switch (*s)
    {
    case 'a':
        return '\a';
    case 'b':
        return '\b';
    case 'f':
        return '\f';
    case 'n':
        return '\n';
    case 'r':
        return '\r';
    case 't':
        return '\t';
    case 'v':
        return '\v';
    case '\'':
    case '"':
    case '\\':
    case '\?':
        break;
    /* hex value */
    case 'x':
    {
        int val = 0;
        for (int i = 1; isxdigit(s[i]); ++i)
        {
            ++index[0];
            val <<= 4;
            val |= hex2val(s[i]);
        }
        return val & 0xFF;
    }
    /* oct value */
    case '0':
    case '1':
    case '2':
    case '3':
    case '4':
    case '5':
    case '6':
    case '7':
    {
        int val = 0;
        /* put back */
        --index[0];
        for (int i = 0; i < 3; ++i)
        {
            if (s[0] < '0' || s[0] > '7')
            {
                break;
            }
            val <<= 3;
            val |= *s - '0';
            ++s;
            ++index[0];
        }
        return val;
    }
    }
    return s[0];
}

/**
 * [deft  description]
 * @param text [description]
 */
void deft(char *text)
{
    int len = strlen(text);
    int escape = 0;
    for (int i = 0; i < len; ++i)
    {
        int ch = text[i];
        if (!escape)
        {
            if (ch == '\"')
            {
                continue;
            }
            if (ch == '\\')
            {
                escape = 1;
                continue;
            }
        }
        else
        {
            escape = 0;
            ch = process_backslash(&text[i], &i);
        }
        emit_byte(&DATA_PC, (uint8_t)ch);
    }
}

/* labels are case-insensitive: the hash key is the lowercased name */
static user_label *find_label(const char *name)
{
    char key[MAX_TOKEN_SIZE];
    size_t len = 0;
    while (name[len] && len < sizeof(key) - 1)
    {
        key[len] = (char)tolower((unsigned char)name[len]);
        ++len;
    }
    key[len] = '\0';

    user_label *found = NULL;
    HASH_FIND(hh, labels, key, len, found);
    return found;
}

/**
@brief Define a label or EQU constant.
@param label Name, optionally with a trailing ':' (stripped in place)
@param address Value; INTMAX_MIN if it cannot be computed yet (forward reference)
*/
void add_label(char *label, intmax_t address)
{
    char *s = strchr(label, ':');
    if (s)
    {
        *s = '\0';
    }

    user_label *found = find_label(label);

    if (found)
    {
        /* Every definition is seen once per pass, so a name encountered twice
           within the same pass is a genuine redefinition. */
        if (found->pass_defined == (unsigned)run_pass)
        {
            error_print("Label \"%s\" is already defined\n", label);
            return;
        }
        found->pass_defined = (unsigned)run_pass;
        /* pass 2 knows every label, so an EQU that could not be evaluated on
           pass 1 can be resolved now */
        if (INTMAX_MIN != address)
        {
            found->address = address;
        }
        return;
    }

    user_label *new = malloc(sizeof(*new));
    if (!new)
    {
        error_print("Memory allocation failed\n");
        return;
    }

    new->address = address;
    new->pass_defined = (unsigned)run_pass;
    new->label = strdup(label);
    if (!new->label)
    {
        free(new);
        error_print("Memory allocation failed\n");
        return;
    }
    for (s = new->label; *s; ++s)
    {
        *s = (char)tolower((unsigned char)*s);
    }

    HASH_ADD_KEYPTR(hh, labels, new->label, strlen(new->label), new);
}

/**
@brief Look up a label or EQU constant by name (case-insensitive).
@return The value, or 0 for a name that is not defined (yet). Such a name is a
        legitimate forward reference on pass 1, but an error on pass 2. Either
        way label_unresolved is set, so callers can tell the placeholder apart
        from a genuine zero.
*/
intmax_t get_label_address(char *label)
{
    user_label *found = find_label(label);
    if (found && INTMAX_MIN != found->address)
    {
        return found->address;
    }
    label_unresolved = true;
    if (PASS2 == run_pass)
    {
        error_print("Undefined label \"%s\"\n", label);
    }
    return 0;
}

/**
@brief Print buffer in hex representation
@param pv Buffer to print
@param len Size of the buffer
@return Nothing
*/
void hex_print(const void *pv, size_t len)
{
    puts("======================START====================");
    const uint8_t *p = (const uint8_t *)pv;
    if (NULL == pv)
    {
        puts("NULL");
    }
    else
    {
        size_t i = 0;
        size_t width = 0;
        for (; i < len; ++i)
        {
            if (width++ % 16 == 0)
            {
                puts("");
            }

            printf("%.2X ", *p++);
        }
    }
    puts("\n=======================END=====================");
}

/**
@brief Load asm file into memory
@param filename Filename of source asm file
@param buffer Pointer to a buffer in memory (Will be allocated by the function)
@return 1 on success, 0 on error
*/
int load_file(char *filename, char **buffer)
{
    FILE *in = fopen(filename, "r");
    if (!in)
    {
        puts(TEXT_FAILED_OPEN_FOR_READ);
        return 0;
    }
    fseek(in, 0, SEEK_END);
    size_t fsize = ftell(in);
    rewind(in);

    if (fsize > MAX_SOURCE_SIZE)
    {
        puts(TEXT_SOURCE_FILE_TOO_LARGE);
        fclose(in);
        return 0;
    }

    *buffer = calloc(1, fsize + 2);
    if (0 == *buffer)
    {
        puts(TEXT_FAILED_ALLOCATE_MEMORY);
        fclose(in);
        return 0;
    }
    size_t rd = fread(*buffer, fsize, 1, in);
    (void)rd;
    fclose(in);
    strcat(*buffer, "\n");
    return 1;
}

/**
 * [print_labels description]
 * @param print [description]
 */
void print_labels(user_label *print)
{
    user_label *cur, *tmp;
    HASH_ITER(hh, print, cur, tmp)
    {
        printf("[%#.4X] \"%s\"\n", (uint16_t)cur->address, cur->label);
    }
}

/**
@brief Assemble a source buffer and write the result.
@param source Null-terminated source text
@param fmt Output format: bin, tap or ihex
@param out Output file, already open for writing
@return ASM_OK on success, ASM_ERROR if any pass reported an error
*/
int process_source(char *source, char *fmt, FILE *out)
{
    assembled_bytes = 0;
    semantic_errors = 0;

    /* Pass 1 collects label definitions; forward references are expected and
       are not diagnosed until pass 2 knows every label. */
    run_pass = PASS1;
    PC = 0;
    prog_end = 0;
    address_overflow = false;
    overflow_reported = false;
    asm_load_buffer(source);
    if (0 != asmparse())
    {
        return ASM_ERROR;
    }

    run_pass = PASS2;
    PC = 0;
    prog_end = 0;
    address_overflow = false;
    overflow_reported = false;
    asm_load_buffer(source);
    if (0 != asmparse() || semantic_errors)
    {
        return ASM_ERROR;
    }

    if (PROG_START == 0xFFFF)
    {
        PROG_START = 0x0000;
    }
    /* prog_end is the highest address written + 1, so it survives a program
       that ends exactly at 0xFFFF (where PC can no longer advance) */
    size_t payload = (prog_end > PROG_START) ? (size_t)(prog_end - PROG_START) : 0;
    if (!strcmp(fmt, "bin"))
    {
        fwrite(prog, 1, prog_end, out);
        assembled_bytes = prog_end;
    }
    else if (!strcmp(fmt, "tap"))
    {
        struct t_tap_info tap = {0};

        tap.prog_start = PROG_START;
        tap.entry_point = PROG_START;
        tap.rom_size = payload;
        tap.rom = &prog[tap.prog_start];
        (void)tap_create(&tap, out);
        assembled_bytes = payload;
    }
    else if (!strcmp(fmt, "ihex"))
    {
        if (0 != save_array_to_ihex(out, PROG_START, &prog[PROG_START], payload))
        {
            puts("Failed to write Intel HEX output");
            return ASM_ERROR;
        }
        assembled_bytes = payload;
    }
    else
    {
        printf("Unknown output format \"%s\"\n", fmt);
        return ASM_ERROR;
    }
    return ASM_OK;
}
