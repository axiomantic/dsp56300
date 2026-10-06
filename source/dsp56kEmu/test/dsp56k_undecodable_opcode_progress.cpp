// Test that undecodable instruction words have a minimum length of 1 word and terminate the JIT block walk.

#include "dsp56kEmu/dsp.h"
#include "dsp56kEmu/jit.h"
#include "dsp56kEmu/jitblock.h"
#include "dsp56kEmu/jitblockinfo.h"
#include "dsp56kEmu/memory.h"
#include "dsp56kEmu/opcodes.h"
#include "dsp56kEmu/peripherals56311.h"
#include "dsp56kEmu/unittests.h"

#include <iostream>
#include <map>
#include <set>
#include <vector>

namespace
{
	using namespace dsp56k;

	DefaultMemoryValidator g_memoryValidator;

	constexpr TWord g_base = 0x100;

	/*	$000040 is the word this was found on: six of them sit inside one generated routine of a
		Nord Modular G2 DSP image. It is used here only because it decodes to nothing.
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

			// Without the fix this call does not return, because the walk inside it never advances
			// past the word.
			f.dsp.getJit().create(g_base, false);
			std::cout << "create returned" << std::endl;

			JitBlockInfo info;
			const MmuArray<JitCacheEntry> cache;
			const std::set<TWord> volatileP;
			const std::map<TWord, TWord> loopStarts;
			const std::set<TWord> loopEnds;
			const JitConfig config;

			JitBlock::getInfo(info, f.dsp, g_base, config, cache, volatileP, loopStarts, loopEnds);

			std::cout << "block at $" << std::hex << g_base << std::dec
				<< ": memSize=" << info.memSize
				<< " instructionCount=" << info.instructionCount
				<< " terminationReason=" << static_cast<int>(info.terminationReason) << std::endl;

			// The move and the word, and not the jmp: the block stops on the word rather than
			// carrying on past it, and the word occupies exactly one address.
			verify(info.terminationReason == JitBlockInfo::TerminationReason::IllegalInstruction);
			verify(info.memSize == 3);
			verify(info.instructionCount == 2);
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
