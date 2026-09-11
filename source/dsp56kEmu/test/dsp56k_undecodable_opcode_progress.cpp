// Tier T0: the guest program is written into P memory word by word from this file; no
// firmware, kernel or .pch2 corpus is touched, so the check runs with NMG2_ARTIFACTS unset.
//
// WHAT IT MEASURES. JitBlock::getInfo walks forward from a PC one instruction at a time and
// advances by Opcodes::getOpcodeLength. A word that matches no entry in the opcode table
// decodes to no instruction at all, and getOpcodeLength had nothing to measure for it, so it
// returned 0. The walk then recomputed the same address, read the same word, and did it again:
// no error, no exception, no output, and no exit. The only escape was
// JitConfig::maxInstructionsPerBlock, which is 0 -- off -- by default.
//
// The population is not a corner. Of the 16,777,216 values a 24 bit instruction word can take,
// 366,724 decode to nothing, and the set of words that decode to nothing is exactly the set for
// which getOpcodeLength returned 0. So this was reachable from 2.19% of the encoding space, at
// any address, with a stall that looks from outside like a run that is still working.
//
// WHAT EACH CASE EXERCISES.
//
//   theLengthOfAnUndecodableWordIsNotZero -- getOpcodeLength itself, which is where the floor
//   is. Three callers add its return to a program counter, one of them the debugger's
//   disassembly walk, so the guarantee belongs at the source rather than at each caller.
//
//   decodableWordsKeepTheirLength -- the control for the case above. The floor must not have
//   moved a length that was already correct, including the two word case.
//
//   anUndecodableWordTerminatesTheBlockWalk -- the walk. Before the fix this call does not
//   return; the test binary is registered with a CTest TIMEOUT, which is what turns a stall
//   into a red rather than into a run that is still going.
//
//   ordinaryCodeStillProducesTheSameBlock -- the known positive for the walk. A block over
//   decodable code has to come out byte for byte as it did, so this pins its termination
//   reason, word count and instruction count rather than merely observing that it returned.
//
// HOW IT FAILS. Without the fix, anUndecodableWordTerminatesTheBlockWalk spins until CTest's
// TIMEOUT kills it, and theLengthOfAnUndecodableWordIsNotZero fails outright with 0 != 1.

#include "dsp56kEmu/dsp.h"
#include "dsp56kEmu/jit.h"
#include "dsp56kEmu/jitblock.h"
#include "dsp56kEmu/jitblockinfo.h"
#include "dsp56kEmu/memory.h"
#include "dsp56kEmu/opcodes.h"
#include "dsp56kEmu/peripherals56311.h"
#include "dsp56kEmu/unittests.h"

#include <iostream>
#include <vector>

namespace
{
	using namespace dsp56k;

	DefaultMemoryValidator g_memoryValidator;

	constexpr TWord g_base = 0x100;

	/*	$000040 is the word this was found on: six of them sit inside one generated routine of a
		Nord Modular G2 DSP image. It is used here only because it decodes to nothing -- this test
		makes no claim about what it means on silicon, and asserts nothing about what it does.
	*/
	constexpr TWord g_undecodable = 0x000040;

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

	// The word really is one the table does not describe, so the cases below are exercising the
	// intended path and not some other rejection. Paired with the decodable controls that follow,
	// which keep a zero from this instrument looking the same as a query that never ran.
	void theWordUsedHereReallyIsUndecodable()
	{
		Opcodes opcodes;

		Instruction instA, instB;
		opcodes.getInstructionTypes(g_undecodable, instA, instB);

		std::cout << "$" << std::hex << g_undecodable << std::dec
			<< " decodes to instA=" << static_cast<int>(instA)
			<< " instB=" << static_cast<int>(instB) << std::endl;

		verify(instA == Invalid);
		verify(instB == Invalid);

		// The control: a word that does decode, through the same call.
		opcodes.getInstructionTypes(0x200040, instA, instB);
		verify(instA != Invalid);
	}

	void theLengthOfAnUndecodableWordIsNotZero()
	{
		Opcodes opcodes;

		const auto len = opcodes.getOpcodeLength(g_undecodable);

		std::cout << "getOpcodeLength($" << std::hex << g_undecodable << std::dec << ") = " << len << std::endl;

		// One word is the smallest step that reaches a different address. Zero is what stalled the
		// walk, and any caller stepping a PC by this value has to move off the word it read.
		verify(len >= 1);
	}

	void decodableWordsKeepTheirLength()
	{
		Opcodes opcodes;

		// NOP, and the two values the existing opcode length unit test pins, so the floor is shown
		// not to have disturbed a length that was already right at either width.
		verify(opcodes.getOpcodeLength(0x000000) == 1);	// nop
		verify(opcodes.getOpcodeLength(0x000218) == 1);	// brkcs
		verify(opcodes.getOpcodeLength(0x200040) == 1);	// add x0,a
		verify(opcodes.getOpcodeLength(0x0c1800) == 2);
		verify(opcodes.getOpcodeLength(0x0141c1) == 2);
		verify(opcodes.getOpcodeLength(0x0141c0) == 2);
	}

	/*	The walk, driven through Jit::create because that is the only caller of JitBlock::getInfo
		and it is how a block comes to be built in a real run. The undecodable word is placed after
		one ordinary instruction, so the walk has already advanced once and the case is about
		meeting the word mid block rather than about entering a block on one.
	*/
	void anUndecodableWordTerminatesTheBlockWalk()
	{
		if constexpr(!g_useJIT)
		{
			std::cout << "anUndecodableWordTerminatesTheBlockWalk: jit not supported on this build, skipped" << std::endl;
			return;
		}
		else
		{
			Fixture f;

			// move #$1,x0 is two words; then the undecodable word; then a jmp that would end the
			// block if the walk ever reached it.
			writeProgram(f, {0x44f400, 0x000001, g_undecodable, 0x0af080, g_base});

			verify(f.dsp.getJit().getConfig().maxInstructionsPerBlock == 0);

			bool reported = false;

			try
			{
				f.dsp.getJit().create(g_base, false);
			}
			catch(const std::string& _err)
			{
				reported = true;
				std::cout << "reported: " << _err << std::endl;
			}
			catch(const std::exception& _err)
			{
				reported = true;
				std::cout << "reported: " << _err.what() << std::endl;
			}

			// Returning from create at all is half the measurement: without the fix this line is
			// unreachable, because the walk inside it never advances past the word.
			std::cout << "create returned" << std::endl;

			// The other half. An undecodable word cannot be translated, and a block that silently
			// omitted it would compute something other than what P memory holds, so ending the run
			// is the outcome. A quiet return here would mean the word was skipped.
			verify(reported);
		}
	}

	// The known positive: the same walk over decodable code still terminates where it did, after
	// the words and instructions it did.
	void ordinaryCodeStillProducesTheSameBlock()
	{
		Fixture f;

		// move #$1,x0 / move #$2,y0 / add x0,a / jmp $100 -- two words each except the one word
		// add, so four instructions over seven words, ending on an unconditional branch.
		writeProgram(f, {0x44f400, 0x000001, 0x46f400, 0x000002, 0x200040, 0x0af080, g_base});

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

		verify(info.terminationReason == JitBlockInfo::TerminationReason::Branch);
		verify(info.memSize == 7);
		verify(info.instructionCount == 4);
		verify(info.branchTarget == g_base);
	}
}

int main()
{
	try
	{
		theWordUsedHereReallyIsUndecodable();
		theLengthOfAnUndecodableWordIsNotZero();
		decodableWordsKeepTheirLength();
		ordinaryCodeStillProducesTheSameBlock();
		anUndecodableWordTerminatesTheBlockWalk();
	}
	catch(const std::string& _err)
	{
		std::cout << "dsp56k_undecodable_opcode_progress FAILED: " << _err << std::endl;
		return -1;
	}
	catch(const std::exception& _err)
	{
		std::cout << "dsp56k_undecodable_opcode_progress FAILED: " << _err.what() << std::endl;
		return -1;
	}

	std::cout << "dsp56k_undecodable_opcode_progress passed" << std::endl;
	return 0;
}
