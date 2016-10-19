#include "cpu.h"
#include <stdlib.h>
#include <stdio.h>

class z80cpu
{
public:
private:
};

static cpuctx *z80_cpu_context_alloc (void)
{
	cpuctx *cpu = (cpuctx*)calloc (1, sizeof *cpu);
	cpu->regs = (vcpu_regs*)calloc (1, sizeof *cpu->regs);
	cpu->pins = (vcpu_pins*)calloc (1, sizeof *cpu->pins);
	return cpu;
}

static void z80_cpu_context_free (cpuctx *ctx)
{
	free(ctx->regs);
	free(ctx->pins);
	free(ctx);
}

static void z80_cpu_reset (cpuctx *ctx)
{
	ctx->regs->SP = 0xffff;
	ctx->regs->AF = 0xffff;
}

static void z80_cpu_tick (uint8_t *memory, cpuctx *ctx)
{
	ctx->pins->_A._A = ctx->regs->PC;
	ctx->pins->_D._D = memory[ctx->regs->PC];
	ctx->opcode = memory[ctx->regs->PC];
	char *result = 0;
	//z80_opcode_decoder(ctx, &result);
	free(result);
}

void emu_start(void)
{
	puts("Starting Emu");
	cpuctx *z = z80_cpu_context_alloc();
	z80_cpu_reset(z);
	z80_cpu_context_free(z);
}
