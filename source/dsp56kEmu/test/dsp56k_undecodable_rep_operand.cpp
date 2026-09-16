// Tier T0: the guest program is written into P memory word by word from this file; no
// firmware, kernel or .pch2 corpus is touched, so the check runs with NMG2_ARTIFACTS unset.
//
// WHAT IT MEASURES. JitBlock::getInfo scans a REP and the instruction it repeats as one step,
// because JitOps::rep_exec emits both together. The repeated word is decoded inside that step, not
// by the walk's own read, so the undecodable-word check that dsp56k_undecodable_opcode_progress
// covers for the walk has to apply to it separately. Without that, a REP followed by a word the
// opcode table does not describe passes analysis and reaches the emitter.
//
// WHAT EACH CASE EXERCISES.
//
//   theRepWordReallyIsARep -- the REP used below decodes to REP #xxx, so the scan of a repeated
//   instruction is the path being taken and not some other one.
//
//   aRepOverDecodableCodeStillProducesTheSameBlock -- the known positive. The same REP over a
//   decodable instruction analyses as one block with both words counted, and creates without
//   raising anything.
//
//   aRepOverAnUndecodableWordIsReportedAsUndecodable -- the case. Block creation must end with the
//   same undecodable-instruction-word error the walk raises, naming the repeated word, rather than
//   any other failure further on.

#include "dsp56kEmu/dsp.h"
#include "dsp56kEmu/jit.h"
#include "dsp56kEmu/jitblock.h"
#include "dsp56kEmu/jitblockinfo.h"
#include "dsp56kEmu/memory.h"
#include "dsp56kEmu/opcodes.h"
#include "dsp56kEmu/peripherals56311.h"
#include "dsp56kEmu/unittests.h"

#include <iostream>
#include <string>
#include <vector>

namespace
{
	using namespace dsp56k;

	DefaultMemoryValidator g_memoryValidator;

	constexpr TWord g_base = 0x100;

	// Any word that decodes to nothing serves; this one is the value
	// dsp56k_undecodable_opcode_progress uses, so the two tests exercise the same word.
	constexpr TWord g_undecodable = 0x000040;

	constexpr TWord g_rep2 = 0x0600a0 | (2 << 8);	// rep #2
	constexpr TWord g_addX0A = 0x200040;				// add x0,a
	constexpr TWord g_jmp = 0x0af080;					// jmp, absolute address in the next word

	struct Fixture
	{
		Peripherals56311 p{96000};
		Memory mem{g_memoryValidator, 0x080000, 0x800000, 0x200000};
		DSP dsp{mem, &p, &p.ySpace()};

		Fixture()
		{
			// Mask interrupts (SR.I1 = SR.I0 = 1), as the other JIT tests here do: the 56311 ESAI
			// raises transmit-data-empty from construction, and an unmasked interrupt would hijack
			// the guest to a vector this fixture never populated.
			dsp.regs().sr.var |= (SR_I0 | SR_I1);
		}
	};

	void writeProgram(Fixture& _f, const std::vector<TWord>& _program)
	{
		for(uint32_t i = 0; i < _program.size(); ++i)
			_f.dsp.memWriteP(g_base + i, _program[i]);
	}

	void theRepWordReallyIsARep()
	{
		Opcodes opcodes;

		Instruction instA, instB;
		opcodes.getInstructionTypes(g_rep2, instA, instB);
		verify(instA == Rep_xxx);

		opcodes.getInstructionTypes(g_undecodable, instA, instB);
		verify(instA == Invalid);
		verify(instB == Invalid);
	}

	void aRepOverDecodableCodeStillProducesTheSameBlock()
	{
		Fixture f;

		writeProgram(f, {g_rep2, g_addX0A, g_jmp, g_base});

		JitBlockInfo info;
		MmuArray<JitCacheEntry> cache;
		const std::set<TWord> volatileP;
		const std::map<TWord, TWord> loopStarts;
		const std::set<TWord> loopEnds;

		JitConfig config;

		JitBlock::getInfo(info, f.dsp, g_base, config, cache, volatileP, loopStarts, loopEnds);

		std::cout << "block at $" << std::hex << g_base << std::dec
			<< ": memSize=" << info.memSize
			<< " instructionCount=" << info.instructionCount
			<< " terminationReason=" << static_cast<int>(info.terminationReason) << std::endl;

		// rep #2 and add x0,a are one word each and the jmp is two.
		verify(info.terminationReason == JitBlockInfo::TerminationReason::Branch);
		verify(info.memSize == 4);
		verify(info.instructionCount == 3);
		verify(info.branchTarget == g_base);

		if constexpr(g_useJIT)
			f.dsp.getJit().create(g_base, false);
	}

	void aRepOverAnUndecodableWordIsReportedAsUndecodable()
	{
		if constexpr(!g_useJIT)
		{
			std::cout << "aRepOverAnUndecodableWordIsReportedAsUndecodable: jit not supported on this build, skipped" << std::endl;
			return;
		}
		else
		{
			Fixture f;

			writeProgram(f, {g_rep2, g_undecodable, g_jmp, g_base});

			std::string reported;

			try
			{
				f.dsp.getJit().create(g_base, false);
			}
			catch(const std::string& _err)
			{
				reported = _err;
			}
			catch(const std::exception& _err)
			{
				reported = _err.what();
			}

			std::cout << "reported: \"" << reported << "\"" << std::endl;

			// The message, not merely that something was raised: the emitter raises its own
			// illegal-instruction error for this word if analysis lets it through, and that would
			// otherwise read as a pass.
			verify(reported.find("undecodable instruction word") != std::string::npos);
		}
	}
}

int main()
{
	try
	{
		theRepWordReallyIsARep();
		aRepOverDecodableCodeStillProducesTheSameBlock();
		aRepOverAnUndecodableWordIsReportedAsUndecodable();
	}
	catch(const std::string& _err)
	{
		std::cout << "dsp56k_undecodable_rep_operand FAILED: " << _err << std::endl;
		return -1;
	}
	catch(const std::exception& _err)
	{
		std::cout << "dsp56k_undecodable_rep_operand FAILED: " << _err.what() << std::endl;
		return -1;
	}

	std::cout << "dsp56k_undecodable_rep_operand passed" << std::endl;
	return 0;
}
