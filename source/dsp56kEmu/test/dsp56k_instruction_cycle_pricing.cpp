// Tier T0: the instruction words are literals in this file; no firmware is touched.
//
// WHAT IT MEASURES. The clock cycles DSP::calcOpcodeCycles() charges for an
// instruction, against DSP56300 Family Manual Rev. 5, Appendix A, Table A-1
// "Instruction Timing, Word Count, and Encoding" (T, + pru, + lab, + lim).
//
// Every expected value is a literal read off that table, never recomputed from
// g_cycles, so a wrong table entry and a wrong formula both fail here.
//
// The table's terms, p. A-1:
//   T      "Addressing mode is the Post-Update mode (post-increment,
//          post-decrement and post offset by N) or the No-Update mode"
//   + pru  "pre-update addressing modes (pre-decrement and offset by N
//          addressing modes)"
//   + lab  "Long Absolute Address mode"
//   + lim  "long immediate data addressing mode"
//
// So (Rn)+Nn and (Rn)-Nn are post-update and pay no pru; (Rn+Nn) and -(Rn) pay
// it. Absolute address (MMMRRR 110000) pays lab only and immediate data
// (MMMRRR 110100) pays lim only: they share MMM and differ in RRR.
//
// SHORT ABSOLUTE. Table A-1 prints "MOVE [x or y]:aa,D 1" followed by
// "MOVE [x or y]aa 2", which names no direction. The 1 is taken for both read
// and write because every other source that separates the two agrees they cost
// the same: MOVEC S1,[x or y]:aa is 1 like MOVEC [x or y]:aa,D1 (p. A-7), MOVE
// S,[x or y]:ea is 1 like MOVE [x or y]:ea,D (p. A-6), and the DSP56600 Family
// Manual, Table B-1, prints the one row "MOVE S:<aa>,DDDDD 1" for the opcode
// that carries both directions in its W bit.

#include "dsp56kEmu/dsp.h"
#include "dsp56kEmu/memory.h"
#include "dsp56kEmu/opcodes.h"
#include "dsp56kEmu/peripherals56311.h"
#include "dsp56kEmu/unittests.h"

#include <iostream>
#include <sstream>
#include <vector>

namespace
{
	using namespace dsp56k;

	DefaultMemoryValidator g_memoryValidator;

	struct Case
	{
		const char* source;
		TWord op;
		TWord ext;
		Instruction inst;
		uint32_t expectedCycles;
	};

	constexpr TWord g_noExt = 0;

	// Words came from this repository's assembler where it accepts the syntax, and
	// were hand-encoded from opcodeinfo.h where it does not (jmp, brclr, movec).
	// The decode of each is asserted below, so a wrong word fails as a wrong
	// instruction rather than as a wrong price.
	const Case g_cases[] =
	{
		// Long absolute and long immediate: T 1, + lab 1, + lim 1, charged one at a time.
		{"move x:>$1234,a",        0x56f000, 0x001234, Movex_ea, 2},
		{"move a,x:>$1234",        0x567000, 0x001234, Movex_ea, 2},
		{"move #>$123456,a",       0x56f400, 0x123456, Movex_ea, 2},
		{"move y:>$1234,b",        0x5ff000, 0x001234, Movey_ea, 2},
		{"move b,y:>$1234",        0x5f7000, 0x001234, Movey_ea, 2},
		{"movec x:>$1234,m0",      0x05f020, 0x001234, Movec_ea, 2},
		{"movec #>$123456,m0",     0x05f420, 0x123456, Movec_ea, 2},

		// Post offset by N: T 1 with no pru.
		{"move x:(r0)+n0,a",       0x56c800, g_noExt,  Movex_ea, 1},
		{"move x:(r0)-n0,a",       0x56c000, g_noExt,  Movex_ea, 1},
		{"move a,x:(r0)+n0",       0x564800, g_noExt,  Movex_ea, 1},
		{"move y:(r1)-n1,b",       0x5fc100, g_noExt,  Movey_ea, 1},

		// Short absolute, both directions: 1.
		{"move x:$10,a",           0x569000, g_noExt,  Movex_aa, 1},
		{"move a,x:$10",           0x561000, g_noExt,  Movex_aa, 1},
		{"move y:$10,b",           0x5f9000, g_noExt,  Movey_aa, 1},
		{"move b,y:$10",           0x5f1000, g_noExt,  Movey_aa, 1},

		// Neighbours the corrections must leave alone.
		{"nop",                    0x000000, g_noExt,  Nop,          1},
		{"mpy x0,x0,a",            0x200080, g_noExt,  Mpy_S1S2D,    1},
		{"move x:(r0),a",          0x56e000, g_noExt,  Movex_ea,     1},
		{"move x:(r0)+,a",         0x56d800, g_noExt,  Movex_ea,     1},
		{"move x:(r0+n0),a",       0x56e800, g_noExt,  Movex_ea,     2},
		{"move x:-(r0),a",         0x56f800, g_noExt,  Movex_ea,     2},
		{"move x:(r0+$1234),a",    0x0a70ce, 0x001234, Movex_Rnxxxx, 3},
		{"jmp >$1234",             0x0af080, 0x001234, Jmp_ea,       4},
		{"brclr #1,x:$ffffc0,*",   0x0cc001, 0x000100, Brclr_pp,     5},
	};

	struct Fixture
	{
		Peripherals56311 p{96000};
		Memory mem{g_memoryValidator, 0x080000, 0x800000, 0x200000};
		DSP dsp{mem, &p, &p.ySpace()};
	};

	void pricesMatchTableA1()
	{
		Fixture f;
		Opcodes opcodes;

		constexpr TWord pc = 0x100;

		std::vector<std::string> failures;

		for(const auto& c : g_cases)
		{
			f.dsp.memWriteP(pc, c.op);
			f.dsp.memWriteP(pc + 1, c.ext);

			Instruction instA;
			Instruction instB;
			opcodes.getInstructionTypes(c.op, instA, instB);

			const auto cycles = f.dsp.calcOpcodeCycles(pc);

			std::cout << c.source << ": inst=" << instA << " cycles=" << cycles << " expected=" << c.expectedCycles << std::endl;

			if(instA != c.inst)
			{
				std::stringstream ss;
				ss << c.source << " decodes as instruction " << instA << ", expected " << c.inst;
				failures.push_back(ss.str());
			}
			else if(cycles != c.expectedCycles)
			{
				std::stringstream ss;
				ss << c.source << " charged " << cycles << " cycles, Table A-1 gives " << c.expectedCycles;
				failures.push_back(ss.str());
			}
		}

		for(const auto& msg : failures)
			std::cout << "MISMATCH " << msg << std::endl;

		verify(failures.empty());
	}
}

int main()
{
	try
	{
		pricesMatchTableA1();
	}
	catch(const std::string& _err)
	{
		std::cout << "dsp56k_instruction_cycle_pricing FAILED: " << _err << std::endl;
		return -1;
	}
	catch(const std::exception& _err)
	{
		std::cout << "dsp56k_instruction_cycle_pricing FAILED: " << _err.what() << std::endl;
		return -1;
	}

	std::cout << "dsp56k_instruction_cycle_pricing passed" << std::endl;
	return 0;
}
