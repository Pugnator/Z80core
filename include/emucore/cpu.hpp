/**
 *
 * @file   cpu.hpp
 * @date   16.03.2018 
 * @license This project is released under the GPL 2 license.
 * @brief 
 *
 */

#pragma once

#include <stdint.h>
#include <inttypes.h>
#include <stdlib.h>
#include <memory>
#include <vector>

#define MAX_RAM_SIZE 0xFFFF

enum Z80_FLAG
{
	NONE, S_FLAG, Z_FLAG, HC_FLAG, P_FLAG, V_FLAG, N0_FLAG, N1_FLAG, CY_FLAG
};

enum M_STATE
{
	FETCH, MEMR, MEMW, IOR, IOW, INTACK, WAIT
};

typedef struct
{
	union
	{
		uint16_t _A;
		struct __attribute__((packed, aligned(2)))
		{
			uint16_t A15 : 1;
			uint16_t A14 : 1;
			uint16_t A13 : 1;
			uint16_t A12 : 1;
			uint16_t A11 : 1;
			uint16_t A10 : 1;
			uint16_t A9 : 1;
			uint16_t A8 : 1;
			uint16_t A7 : 1;
			uint16_t A6 : 1;
			uint16_t A5 : 1;
			uint16_t A4 : 1;
			uint16_t A3 : 1;
			uint16_t A2 : 1;
			uint16_t A1 : 1;
			uint16_t A0 : 1;
		};
	}_A;

	union
	{
		uint8_t _D;
		struct __attribute__((packed, aligned(1)))
		{
			uint8_t D7 : 1;
			uint8_t D6 : 1;
			uint8_t D5 : 1;
			uint8_t D4 : 1;
			uint8_t D3 : 1;
			uint8_t D2 : 1;
			uint8_t D1 : 1;
			uint8_t D0 : 1;
		};

	}_D;
	union
	{
		uint8_t _iface;
		struct __attribute__((packed, aligned(1)))
		{
			uint8_t WR : 1;
			uint8_t RD : 1;
			uint8_t IORQ : 1;
			uint8_t MREQ : 1;
			uint8_t M1 : 1;
			uint8_t    : 3;
		};

	}pins_mem_iface;
	uint16_t CLK : 1;
	uint16_t RESET : 1;
	uint16_t BUSAC : 1;
	uint16_t BUSREQ : 1;
	uint16_t INT : 1;
	uint16_t NMI : 1;
	uint16_t WAIT : 1;
	uint16_t HALT : 1;
	uint16_t REFSH : 1;
	uint16_t : 2;

}vcpu_pins;

static uint8_t m_states_table[][2]=
{
	{0b00101000, 4}, /* Fetch */
	{0b10101000, 3}, /* MEMR */
	{0b10110000, 3}, /* MEMW */
	{0b11001000, 4}, /* IOR */
	{0b11010000, 4}, /* IOW */
	{0b01000000, 7}  /* INT ACK */
};

typedef struct
{
	union
	{
		uint16_t _AF;
		struct
		{
			union
			{
				uint8_t _F;
				struct __attribute__((packed, aligned(1)))
				{
					uint8_t C_flag  : 1;
					uint8_t N_flag  : 1;
					uint8_t PV_flag : 1;
					uint8_t F3_flag : 1;
					uint8_t HF_flag : 1;
					uint8_t F5_flag : 1;
					uint8_t Z_flag  : 1;
					uint8_t S_flag  : 1;
				};
			}_F;
			uint8_t _A;
		};
	}_AF;

	union
	{
		uint16_t _AF2;
		struct
		{
			union
			{
				uint8_t _F2;
				struct __attribute__((packed, aligned(1)))
				{
					uint8_t C_flag  : 1;
					uint8_t N_flag  : 1;
					uint8_t PV_flag : 1;
					uint8_t F3_flag : 1;
					uint8_t HF_flag : 1;
					uint8_t F5_flag : 1;
					uint8_t Z_flag  : 1;
					uint8_t S_flag  : 1;
				};
			}_F2;
			uint8_t _A2;
		};
	}_AF2;

	union
	{
		uint16_t _BC;
		struct
		{
			uint8_t _C;
			uint8_t _B;
		};
	}_BC;
	union
	{
		uint16_t _BC2;
		struct
		{
			uint8_t _C2;
			uint8_t _B2;

		};
	}_BC2;
	union
	{
		uint16_t _DE;
		struct
		{
			uint8_t _E;
			uint8_t _D;
		};

	}_DE;
	union
	{
		uint16_t _DE2;
		struct
		{
			uint8_t _E2;
			uint8_t _D2;
		};

	}_DE2;
	union
	{
		uint16_t _HL;
		struct
		{
			uint8_t _L;
			uint8_t _H;

		};

	}_HL;
	union
	{
		uint16_t _HL2;
		struct
		{
			uint8_t _L2;
			uint8_t _H2;

		};

	}_HL2;
	union
	{
		uint16_t _IX;
		struct
		{
			uint8_t _IXH;
			uint8_t _IXL;

		};

	}_IX;

	union
	{
		uint16_t _IY;
		struct
		{
			uint8_t _IYH;
			uint8_t _IYL;

		};

	}_IY;

	union
	{
		uint16_t _PC;
		struct
		{
			uint8_t _PCH;
			uint8_t _PCL;
		};

	}_PC;

	uint16_t SP;
	union __attribute__((packed, aligned(1)))
	{
		uint8_t _R;
		struct
		{
			uint8_t __R : 7;
			uint8_t   : 1;

		};
	} _R;
	uint8_t I;
	uint8_t IFF1 : 1;
	uint8_t IFF2 : 1;
	uint8_t IM : 1;
}vcpu_regs;

union __attribute__((packed, aligned(2)))
{
	uint16_t _prefix;
	struct
	{
		uint8_t second;
		uint8_t first;
	};
} op_prefix;

union __attribute__((packed, aligned(1)))
{
	uint8_t _opcode;
	struct
	{
		uint8_t z : 3;
		uint8_t y : 3;
		uint8_t x : 2;
	};
	struct
	{
		uint8_t   : 3;
		uint8_t p : 2;
		uint8_t q : 1;
	};
} op_opcode;

/* Macroses for CPU registers */

#define AF _AF._AF
#define A _AF._A
#define F _AF._F._F
#define BC _BC._BC
#define B _BC._B
#define C _BC._C
#define DE _DE._DE
#define D _DE._D
#define E _DE._E
#define HL _HL._HL
#define H _HL._H
#define L _HL._L
#define R _R._R
#define RR _R.__R
#define IX _IX._IX
#define IXH _IX._IXH
#define IXL _IX._IXL
#define IY _IY._IY
#define IYH _IY._IYH
#define IYL _IY._IYL
#define PC _PC._PC
#define PCH _PC._PCH
#define PCL _PC._PCL

#define hFLAGS host_flags

/* CPU FLAGS */

#define Sbit _AF._F.S_flag
#define Zbit _AF._F.Z_flag
#define B5bit _AF._F.F5_flag
#define HCbit _AF._F.HF_flag
#define PVbit _AF._F.PV_flag
#define B3bit _AF._F.F3_flag
#define Nbit _AF._F.N_flag
#define CYbit _AF._F.C_flag

/* CPU PINS */

#define A0 _A.A0
#define A1 _A.A1
#define A2 _A.A2
#define A3 _A.A3
#define A4 _A.A4
#define A5 _A.A5
#define A6 _A.A6
#define A7 _A.A7
#define A8 _A.A8
#define A9 _A.A9
#define A10 _A.A10
#define A11 _A.A11
#define A12 _A.A12
#define A13 _A.A13
#define A14 _A.A14
#define A15 _A.A15

#define D0 _D.D0
#define D1 _D.D1
#define D2 _D.D2
#define D3 _D.D3
#define D4 _D.D4
#define D5 _D.D5
#define D6 _D.D6
#define D7 _D.D7

#define opcode _opcode._opcode
#define opcode_prefix _prefix._prefix
#define opcX _opcode.x
#define opcP _opcode.p
#define opcY _opcode y
#define opcQ _opcode.q
#define opcZ _opcode.z

void emu_start(void);

class vcpu
{
public:
	vcpu()
{
	reset();
	cached_ram = std::make_shared<std::vector<uint8_t>>(MAX_RAM_SIZE);
}
	~vcpu();

public:
	void clock();
private:
	void reset();
	void fetch();

private:
	std::shared_ptr<std::vector<uint8_t>> cached_ram;
	M_STATE state;
	Z80_FLAG flag;
	vcpu_pins pins;
	vcpu_regs regs;
	uint32_t clocks_passed;
};
