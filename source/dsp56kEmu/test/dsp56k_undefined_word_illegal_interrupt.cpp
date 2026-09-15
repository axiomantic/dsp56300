// Tier T0: the guest program is written into P memory word by word from this file; no
// firmware, kernel or .pch2 corpus is touched, so the check runs with NMG2_ARTIFACTS unset.
//
// WHAT IT MEASURES. An instruction word that matches no DSP56300 encoding is not a fault of the
// emulator but a defined event on silicon. DSP56300 Family Manual Rev. 5, 2.3.2.2: the Illegal
// Instruction Interrupt is "serviced immediately after the illegal instruction executes or attempts
// to execute (any undefined operation code)". It is IPL 3, so it cannot be masked, and its vector is
// VBA:$04 (Table 2-2). A vector holding two single-word instructions is a fast interrupt, which
// "returns without an RTI" and leaves the PC unchanged (2.3.2.8), so execution carries on after the
// undefined word.
//
// The guest is the Nord Modular G2 voice engine entry exactly as the MCU loads it at P:$236-$245.
// The word at $23d, $000040, is undefined, and $23b/$23c are a two-word move x:>$40,y0 in front of
// it, so a linear run executes $23d on every pass. A counted DO over $236-$245 stands in for the
// firmware's DO FOREVER, so that the run ends at a known address after a known number of passes.
// The G2 image leaves VBA:$04/$05 as two NOPs, which makes the undefined word harmless on the
// hardware. This test puts move (r7)+ in the first vector word instead, so each service of the
// interrupt leaves a count in R7.
//
// BOTH ENGINES ARE DRIVEN. g_useJIT is a compile-time constant, so the interpreter is driven
// through the public DSP::execInterpreter() and the JIT through DSP::exec().
//
// HOW IT FAILS. Before the change the JIT block walk rejects $23d and the run terminates inside
// exec(), and the interpreter dereferences the opcode table entry it did not find. A change that
// steps over the word without raising the interrupt leaves R7 at zero, which the count check
// rejects.

#include "dsp56kEmu/assembler.h"
#include "dsp56kEmu/dsp.h"
#include "dsp56kEmu/interrupts.h"
#include "dsp56kEmu/jit.h"
#include "dsp56kEmu/jitblock.h"
#include "dsp56kEmu/jitblockinfo.h"
#include "dsp56kEmu/memory.h"
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

	constexpr TWord g_engineBase = 0x236;
	constexpr TWord g_undefinedAddr = 0x23d;
	constexpr TWord g_doAddr = 0x234;
	constexpr TWord g_loopEnd = 0x245;
	constexpr TWord g_park = 0x246;

	const std::vector<TWord> g_engine = {
		0x56db00,	// $236 move x:(r3)+,a
		0x000000,	// $237 nop
		0xf09b00,	// $238 move x:(r3)+,x0 y:(r4)+,y0
		0x561000,	// $239 move a,x:$10
		0x4585d8,	// $23a mpy y0,x0,b x:$5,x1
		0x46f000,	// $23b move x:>$40,y0
		0x000040,	// $23c   (extension word of $23b)
		0x000040,	// $23d undefined
		0x21e700,	// $23e move b,y1
		0x209af8,	// $23f mpy y1,x1,b x0,n2
		0x0c1d85,	// $240 asl #2,b,b
		0x000000,	// $241 nop
		0x5edc00,	// $242 move y:(r4)+,a
		0x58d400,	// $243 move y:(r4)-,a0
		0x268010,	// $244 add b,a #$80,y0
		0x21e509,	// $245 tfr a,b b,x1
	};

	constexpr TWord g_dataX = 0x400;
	constexpr TWord g_dataY = 0x500;

	struct Fixture
	{
		Peripherals56311 p{96000};
		Memory mem{g_memoryValidator, 0x080000, 0x800000, 0x200000};
		DSP dsp{mem, &p, &p.ySpace()};

		Fixture()
		{
			// Maskable interrupts off, as the other tests here do: the 56311 ESAI raises
			// transmit-data-empty from construction. The interrupt under test is IPL 3 and
			// is not affected by the mask.
			dsp.regs().sr.var |= (SR_I0 | SR_I1);
		}
	};

	void writeP(DSP& _dsp, const TWord _addr, const std::vector<TWord>& _words)
	{
		for(size_t i = 0; i < _words.size(); ++i)
			_dsp.memWriteP(_addr + static_cast<TWord>(i), _words[i]);
	}

	std::vector<TWord> assemble(const char* _source)
	{
		Assembler assembler;
		const auto r = assembler.assemble(_source);
		verify(r.success());

		std::vector<TWord> out;
		for(uint32_t i = 0; i < r.wordCount; ++i)
			out.push_back(r.word[i]);
		return out;
	}

	constexpr uint32_t g_passes = 4;
	constexpr uint32_t g_maxSteps = 4096;

	void writeProgram(Fixture& _f)
	{
		for(TWord a = 0; a < 0x10; ++a)
			_f.dsp.memWriteP(a, 0);

		const auto countService = assemble("move (r7)+");
		verify(countService.size() == 1);
		writeP(_f.dsp, Vba_Illegalinstruction, countService);

		// DO #xxx,expr: 00000110 iiiiiiii 1000hhhh, then LA as the extension word.
		writeP(_f.dsp, g_doAddr, {0x060080 | (g_passes << 8), g_loopEnd});
		writeP(_f.dsp, g_engineBase, g_engine);
		writeP(_f.dsp, g_park, {0x0af080, g_park});

		_f.dsp.regs().r[3].var = g_dataX;
		_f.dsp.regs().r[4].var = g_dataY;
		_f.dsp.regs().r[7].var = 0;

		// Distinct inputs per word, so each observable below names the pass it came from.
		for(TWord i = 0; i < 32; ++i)
		{
			_f.mem.set(MemArea_X, g_dataX + i, 0x010000 * (i + 1));
			_f.mem.set(MemArea_Y, g_dataY + i, 0x001000 * (i + 1));
		}
	}

	template<typename TStep> void runPasses(const char* _engine, TStep&& _step)
	{
		Fixture f;
		writeProgram(f);

		f.dsp.setPC(g_doAddr);

		uint32_t steps = 0;

		while(steps < g_maxSteps && f.dsp.getPC().toWord() != g_park)
		{
			_step(f);
			++steps;
		}

		const auto r7 = f.dsp.regs().r[7].toWord();
		const auto r3 = f.dsp.regs().r[3].toWord();
		const auto x10 = f.mem.get(MemArea_X, 0x10);
		const auto n2 = f.dsp.regs().n[2].toWord();

		std::cout << _engine << ": steps=" << steps << std::hex
			<< " pc=$" << f.dsp.getPC().toWord() << " r7=$" << r7 << " r3=$" << r3
			<< " x:$10=$" << x10 << " n2=$" << n2 << std::dec << std::endl;

		verify(f.dsp.getPC().toWord() == g_park);
		verify((f.dsp.regs().sr.var & SR_LF) == 0);

		// Each pass executes the undefined word once, and each execution raises exactly one
		// interrupt. More than one per pass would mean the word was queued repeatedly.
		verify(r7 == g_passes);

		// $236 and $238 each read one word through R3 per pass.
		verify(r3 == g_dataX + 2 * g_passes);

		// $239 stores what $236 read on the last pass, which is the first word of that pass.
		verify(x10 == 0x010000 * (2 * (g_passes - 1) + 1));

		// $23f, after the undefined word, copies into N2 what $238 read on the last pass. A run that
		// stopped at the word, or resumed somewhere other than the word after it, leaves N2 at zero.
		verify(n2 == 0x010000 * (2 * (g_passes - 1) + 2));
	}

	void interpreterServicesTheInterruptAndContinues()
	{
		runPasses("interpreter", [](Fixture& _f) { _f.dsp.execInterpreter(); });
	}

	void jitServicesTheInterruptAndContinues()
	{
		if constexpr(!g_useJIT)
		{
			std::cout << "jit not supported on this build, skipped" << std::endl;
			return;
		}
		else
		{
			runPasses("jit", [](Fixture& _f) { _f.dsp.exec(); });
		}
	}

	// The block walk ends the block on the undefined word, so the interrupt is serviced before the
	// word after it. The known positive is the ordinary block, which still ends on its branch.
	void theBlockWalkEndsAfterTheUndefinedWord()
	{
		Fixture f;
		writeProgram(f);

		const MmuArray<JitCacheEntry> cache;
		const std::set<TWord> volatileP;
		const std::map<TWord, TWord> loopStarts;
		const std::set<TWord> loopEnds;
		const JitConfig config;

		JitBlockInfo info;
		JitBlock::getInfo(info, f.dsp, g_engineBase, config, cache, volatileP, loopStarts, loopEnds);

		std::cout << "block at $" << std::hex << g_engineBase << std::dec << ": memSize=" << info.memSize
			<< " instructionCount=" << info.instructionCount
			<< " terminationReason=" << static_cast<int>(info.terminationReason) << std::endl;

		verify(info.terminationReason == JitBlockInfo::TerminationReason::IllegalInstruction);
		verify(info.memSize == g_undefinedAddr + 1 - g_engineBase);
		verify(info.instructionCount == 7);

		JitBlockInfo after;
		JitBlock::getInfo(after, f.dsp, g_undefinedAddr + 1, config, cache, volatileP, loopStarts, loopEnds);

		verify(after.terminationReason == JitBlockInfo::TerminationReason::Branch);
		verify(after.memSize == g_park + 2 - (g_undefinedAddr + 1));
		verify(after.branchTarget == g_park);
	}
}

int main()
{
	try
	{
		theBlockWalkEndsAfterTheUndefinedWord();
		interpreterServicesTheInterruptAndContinues();
		jitServicesTheInterruptAndContinues();
	}
	catch(const std::string& _err)
	{
		std::cout << "dsp56k_undefined_word_illegal_interrupt FAILED: " << _err << std::endl;
		return -1;
	}
	catch(const std::exception& _err)
	{
		std::cout << "dsp56k_undefined_word_illegal_interrupt FAILED: " << _err.what() << std::endl;
		return -1;
	}

	std::cout << "dsp56k_undefined_word_illegal_interrupt passed" << std::endl;
	return 0;
}
