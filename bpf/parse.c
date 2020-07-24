#include <sys/mman.h>
#include <string.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include "parse.h"
#include "lexer.h"
#include "bpf.h"
#include "../vector.h"
#include "../hashmap.h"
#include "../hash.h"

#define MAXLINE 1000
#define BPF_MAXINSN 4096

struct symbol {
    char *name;
    uint32_t value;
};

static struct bpf_parser parser;
static vector_t *bytecode;
static hashmap_t *symbol_table;

static uint16_t opcodes[] = {
    [LD]   = BPF_LD | BPF_W,
    [LDH]  = BPF_LD | BPF_H,
    [LDB]  = BPF_LD | BPF_B,
    [LDX]  = BPF_LDX | BPF_W,
    [ST]   = BPF_ST,
    [STX]  = BPF_STX,
    [ADD]  = BPF_ALU | BPF_ADD,
    [SUB]  = BPF_ALU | BPF_SUB,
    [MUL]  = BPF_ALU | BPF_MUL,
    [DIV]  = BPF_ALU | BPF_DIV,
    [AND]  = BPF_ALU | BPF_AND,
    [OR]   = BPF_ALU | BPF_OR,
    [XOR]  = BPF_ALU | BPF_XOR,
    [LSH]  = BPF_ALU | BPF_LSH,
    [RSH]  = BPF_ALU | BPF_RSH,
    [JMP]  = BPF_JMP | BPF_JA,
    [JEQ]  = BPF_JMP | BPF_JEQ,
    [JGT]  = BPF_JMP | BPF_JGT,
    [JGE]  = BPF_JMP | BPF_JGE,
    [JSET] = BPF_JMP | BPF_JSET,
    [TAX]  = BPF_MISC | BPF_TAX,
    [TXA]  = BPF_MISC | BPF_TXA,
    [RET]  = BPF_RET
};

#define get_token() bpf_lex(&parser)
#define bpf_jmp_stm(i, m, jt, jf, k) make_stm(opcodes[i] | (m), jt, jf, k)
#define bpf_stm(i, m, k) make_stm(opcodes[i] | (m), 0, 0, k)

bool bpf_parse_init(char *file)
{
    int fd;
    struct stat st;
    bool ret = false;

    if ((fd = open(file, O_RDONLY)) == -1)
        return false;
    if (fstat(fd, &st) == -1)
        goto end;
    memset(&parser, 0, sizeof(parser));
    parser.size = st.st_size;
    parser.line = 1;
    parser.infile = file;
    if ((parser.input.buf = mmap(NULL, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0)) == MAP_FAILED)
        goto end;
    bytecode = vector_init(10);
    symbol_table = hashmap_init(10, hash_string, compare_string);
    hashmap_set_free_key(symbol_table, free);
    hashmap_set_free_data(symbol_table, free);
    ret = true;

end:
    close(fd);
    return ret;
}

void bpf_parse_free()
{
    munmap(parser.input.buf, parser.size);
    vector_free(bytecode, free);
    hashmap_free(symbol_table);
}

/* TEMP: Errors need to be handled differently  */
static void error(const char *fmt, ...)
{
    char buf[MAXLINE];
    va_list ap;
    int n;

    n = snprintf(buf, MAXLINE, "%s:%d: error: ", parser.infile, parser.line);
    va_start(ap, fmt);
    vsnprintf(buf + n, MAXLINE - n - 1, fmt, ap);
    va_end(ap);
    strcat(buf, "\n");
    fputs(buf, stderr);
}

static bool make_stm(uint16_t opcode, uint8_t jt, uint8_t jf, uint32_t k)
{
    if (vector_size(bytecode) >= BPF_MAXINSN) {
        error("Program exceeds max number of instructions: %u", BPF_MAXINSN);
        return false;
    }
    struct bpf_insn *insn = malloc(sizeof(struct bpf_insn));

    insn->code = opcode;
    insn->jt = jt;
    insn->jf = jf;
    insn->k = k;
    vector_push_back(bytecode, insn);
    return true;
}

static inline bool valid_mem_offset(int i)
{
    return i >= 0 && i < BPF_MEMWORDS;
}

static inline bool match(int token)
{
    return (parser.token = get_token()) == token;
}

static bool parse_abs(int *k)
{
    if (!match('[')) {
        error("Expected \'[\' after operand");
        return false;
    }
    if (!match(INT)) {
        error("Expected immediate");
        return false;
    }
    *k = parser.val.intval;
    if (!match(']')) {
        error("Expected \']\'");
        return false;
    }
    return true;
}

static bool parse_offset(int insn)
{
    if (match('x')) {
        if (!match('+'))
            goto error;
        if (!match(INT))
            goto error;
        return bpf_stm(insn, BPF_IND, parser.val.intval);
    } else if (parser.token == INT) {
        return bpf_stm(insn, BPF_ABS, parser.val.intval);
    }

error:
    error("Syntax error: %c", parser.token);
    return false;
}

static bool parse_int(int insn, int mode)
{
    int k;
    bool negative = false;

    if (match('-')) {
        negative = true;
        if (!match(INT)) {
            goto error;
        }
    } else if (parser.token != INT) {
        goto error;
    }
    k = negative ? parser.val.intval * -1 : parser.val.intval;
    return bpf_stm(insn, mode, k);

error:
    error("Expected immediate");
    return false;
}

static bool parse_mem(int insn, int mode)
{
    int k;

    if (!parse_abs(&k))
        return false;
    if (!valid_mem_offset(k))
        return false;
    return bpf_stm(insn, mode, k);
}

static bool parse_msh(int insn)
{
    if (parser.val.intval != 4)
        goto error;
    if (!match('*'))
        goto error;
    if (match('(')) {
        int k;

        if (!parse_abs(&k))
            return false;
        if (!match('&'))
            goto error;
        if (!match(INT) && parser.val.intval == 0xf)
            goto error;
        if (!match(')'))
            goto error;

        /*
         * The instruction: ldx  4 * ([k] & 0xf)
         * should use the BPF_B size modifier even though it has no 'b' suffix
         */
        return bpf_stm(insn, BPF_B | BPF_MSH, k);
    }

error:
    error("Unexpexted token %c\n", parser.token);
    return false;
}

static bool parse_ld()
{
    if (match('#')) {
        return parse_int(LD, BPF_IMM);
    } else if (parser.token == 'M') {
        return parse_mem(LD, BPF_MEM);
    } else if (parser.token == '[') {
        if (!parse_offset(LD)) {
            error("Unexpected token: %c", parser.token);
            return false;
        }
        if (!match(']')) {
            error("Expected \']\'");
            return false;
        }
    }
    return true;
}

static bool parse_ldbh()
{
    int insn = parser.token;

    if (!match('[')) {
        error("Expected \'[\' after operand");
        return false;
    }
    if (!parse_offset(insn)) {
        error("Unexpected token: %c", parser.token);
        return false;
    }
    if (!match(']')) {
        error("Expected \']\'");
        return false;
    }
    return true;
}

static bool parse_ldx()
{
    if (match('#'))
        return parse_int(LDX, BPF_IMM);
    else if (parser.token == 'M')
        return parse_mem(LDX, BPF_MEM);
    else if (parser.token == INT)
        return parse_msh(LDX);
    return true;
}

static bool parse_ret()
{
    if (match('#'))
        return parse_int(RET, BPF_K);
    if (parser.token == 'a' || parser.token == 'A')
        return bpf_stm(RET, BPF_A, 0);
    error("Unexpected token: %c", parser.token);
    return false;
}

static bool parse_st()
{
    int insn = parser.token;

    if (match('M'))
        return parse_mem(insn, 0);
    error("Unexpected token: %c", parser.token);
    return false;
}

static bool parse_alu()
{
    int insn = parser.token;

    if (match('#'))
        return parse_int(insn, BPF_K);
    if (parser.token == 'x')
        return bpf_stm(insn, BPF_X, 0);
    error("Unexpected token: %c", parser.token);
    return false;
}

static bool parse_label()
{
    struct symbol *sym;
    char *str = parser.val.str;

    if (!match(':')) {
        free(str);
        if (parser.token != LABEL)
            return true;
        if (!match(':')) {
            free(str);
            free(parser.val.str);
            return true;
        }
    }
    if (hashmap_contains(symbol_table, parser.val.str)) {
        error("Multiple defined label");
        free(parser.val.str);
        return false;
    }
    sym = malloc(sizeof(*sym));
    sym->name = parser.val.str;
    sym->value = parser.line - 1;
    hashmap_insert(symbol_table, sym->name, sym);
    return true;
}

static bool parse_jmp()
{
    struct symbol *sym;

    if (!match(LABEL)) {
        error("Unexpected token: %c", parser.token);
        return false;
    }
    if ((sym = hashmap_get(symbol_table, parser.val.str)) == NULL) {
        error("Undefined label");
        free(parser.val.str);
        return false;
    }
    free(parser.val.str);
    return bpf_stm(JMP, 0, sym->value - parser.line);
}

static bool parse_cond_jmp()
{
    int insn = parser.token;
    int k;
    struct symbol *jt;
    struct symbol *jf;

    if (!match('#'))
        goto error;
    if (!match(INT))
        goto error;
    k = parser.val.intval;
    if (!match(','))
        goto error;
    if (!match(LABEL))
        goto error;
    if ((jt = hashmap_get(symbol_table, parser.val.str)) == NULL)
        goto undefined;
    if (!match(','))
        goto error;
    free(parser.val.str);
    if (!match(LABEL))
        goto error;
    if ((jf = hashmap_get(symbol_table, parser.val.str)) == NULL)
        goto undefined;
    if (jt->value < parser.line || jf->value < parser.line) {
        free(parser.val.str);
        error("Backward jumps are not supported");
        return false;
    }
    free(parser.val.str);
    return bpf_jmp_stm(insn, BPF_K, jt->value - parser.line, jf->value - parser.line, k);

error:
    free(parser.val.str);
    error("Unexpected token: %c", parser.token);
    return false;

undefined:
    free(parser.val.str);
    error("Undefined label: %s", parser.val.str);
    return false;
}

struct bpf_prog bpf_parse()
{
    bool ret = false;
    struct bpf_prog prog = {
        .bytecode = NULL,
        .size = 0
    };

    parser.input.tok = parser.input.buf;
    parser.input.cur = parser.input.buf;
    parser.input.lim = parser.input.buf + strlen((char *) parser.input.buf) + 1;
    while ((parser.token = get_token()) != 0) {
        if (parser.token == LABEL) {
            if (!parse_label())
                return prog;
        }
    }
    parser.input.tok = parser.input.buf;
    parser.input.cur = parser.input.buf;
    parser.line = 1;
    while ((parser.token = get_token()) != 0) {
        switch (parser.token) {
        case LABEL:
            free(parser.val.str);
            if (!match(':')) {
                error("Unexpected token: %c", parser.token);
                return prog;
            }
            break;
        case INT:
            error("Unexpected integer");
            return prog;
        case LD:
            ret = parse_ld();
            break;
        case LDB:
        case LDH:
            ret = parse_ldbh();
            break;
        case LDX:
            ret = parse_ldx();
            break;
        case ST:
        case STX:
            ret = parse_st();
            break;
        case ADD:
        case SUB:
        case MUL:
        case DIV:
        case AND:
        case OR:
        case XOR:
        case LSH:
        case RSH:
            ret = parse_alu();
            break;
        case JMP:
            ret = parse_jmp();
            break;
        case JEQ:
        case JGT:
        case JGE:
        case JSET:
            ret = parse_cond_jmp();
            break;
        case RET:
            ret = parse_ret();
            break;
        case TAX:
            ret = bpf_stm(TAX, 0, 0);
            break;
        case TXA:
            ret = bpf_stm(TXA, 0, 0);
            break;
        default:
            error("Unexpected token: %c", parser.token);
            return prog;
        }
        if (!ret)
            return prog;
    }
    int sz = vector_size(bytecode);
    struct bpf_insn *bc = malloc(sz * sizeof(struct bpf_insn));

    for (int i = 0; i < sz; i++)
        bc[i] = * (struct bpf_insn *) vector_get_data(bytecode, i);
    prog.bytecode = bc;
    prog.size = (uint16_t) sz;
    return prog;
}