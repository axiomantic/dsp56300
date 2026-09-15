// Tier T0: the guest program is assembled into P memory from this file; no firmware, kernel or
// .pch2 corpus is touched, so the check runs with NMG2_ARTIFACTS unset.
//
// WHAT IT MEASURES. DSP56300 Family Manual Rev. 5, 2.3.2.2: the Illegal Instruction Interrupt is "serviced
// immediately after the illegal instruction executes or attempts to execute (any undefined operation
// code)". It is IPL 3 (Table 2-2), which no mask level blocks, and a long interrupt routine "can be
// interrupted ... by a higher priority interrupt" (2.3.2.8). So every undefined word the program runs is
// serviced once, whatever else is pending or running.
//
// The order is the emulator's, not the pipeline's. The ILLEGAL page says control returns to "the second
// word following" the illegal instruction, and in the pipelines of Table 2-7 and Table 2-8 the word after
// the one that raises executes before the vector. This emulator services every interrupt with no pipeline
// delay, so the service runs before that word, and the log below requires that order.
//
// The vector at VBA:$04 is move r1,x:(r6)+ and a NOP. Each service therefore appends the value R1 holds
// at that moment to a log in X memory. The program writes a new value into R1 right after an undefined
// word, so the log shows both how many services happened and whether each one ran before that write.
//
// Each scenario is a situation that held a raise back past the next instruction or dropped it:
//   - nothing else pending: undefined words spaced apart, back to back, and inside a DO body
//   - a maskable interrupt pending but masked, which stays at the head of the interrupt queue
//   - the undefined words inside a long interrupt routine, which services nothing until its RTI
//   - an undefined word inside a fast interrupt routine, which nothing may interrupt
//
// BOTH ENGINES ARE DRIVEN. g_useJIT is a compile-time constant, so the interpreter is driven through
// the public DSP::execInterpreter() and the JIT through DSP::exec().

#include "dsp56kEmu/assembler.h"
#include "dsp56kEmu/dsp.h"
#include "dsp56kEmu/interrupts.h"
#include "dsp56kEmu/jit.h"
#include "dsp56kEmu/memory.h"
#include "dsp56kEmu/peripherals.h"
#include "dsp56kEmu/unittests.h"

#include <functional>
#include <iostream>
#include <string>
#include <vector>

namespace
{
	using namespace dsp56k;

	DefaultMemoryValidator g_memoryValidator;

	// $000040 matches no DSP56300 encoding. It is the word the Nord Modular G2 engine runs.
	constexpr TWord g_undefined = 0x000040;

	constexpr TWord g_logBase = 0x300;
	constexpr TWord g_main = 0x200;
	constexpr TWord g_handler = 0x280;
	constexpr uint32_t g_maxSteps = 4096;

	struct Fixture
	{
		PeripheralsNop px;
		PeripheralsNop py;
		Memory mem{g_memoryValidator, 0x080000, 0x800000, 0x200000};
		DSP dsp{mem, &px, &py};
	};

	struct Line
	{
		std::string source;
		TWord raw = 0;
	};

	Line asmLine(const char* _source) { return Line{_source, 0}; }
	Line undefinedWord() { return Line{std::string(), g_undefined}; }

	TWord writeLines(DSP& _dsp, TWord _addr, const std::vector<Line>& _lines)
	{
		for(const auto& line : _lines)
		{
			if(line.source.empty())
			{
				_dsp.memWriteP(_addr++, line.raw);
				continue;
			}

			Assembler assembler;
			const auto r = assembler.assemble(line.source.c_str());
			if(!r.success())
				throw std::string("cannot assemble: ") + line.source;
			for(uint32_t i = 0; i < r.wordCount; ++i)
				_dsp.memWriteP(_addr++, r.word[i]);
		}
		return _addr;
	}

	using TStep = std::function<void(DSP&)>;

	struct Scenario
	{
		const char* name;
		std::function<void(Fixture&)> setup;
		// the value of R1 each service must log, in order
		std::vector<TWord> expectedLog;
	};

	void run(const char* _engine, const TStep& _step, const Scenario& _scenario)
	{
		Fixture f;

		for(TWord a = 0; a < Vba_End; ++a)
			f.dsp.memWriteP(a, 0);

		writeLines(f.dsp, Vba_Illegalinstruction, {asmLine("move r1,x:(r6)+"), asmLine("nop")});

		f.dsp.regs().r[6].var = g_logBase;
		f.dsp.regs().r[1].var = 0;

		_scenario.setup(f);

		const TWord park = f.dsp.regs().r[5].toWord();
		f.dsp.regs().r[5].var = 0;

		uint32_t steps = 0;
		while(steps < g_maxSteps && f.dsp.getPC().toWord() != park)
		{
			_step(f.dsp);
			++steps;
		}

		const TWord services = f.dsp.regs().r[6].toWord() - g_logBase;

		std::cout << _engine << " / " << _scenario.name << ": steps=" << steps << std::hex << " pc=$" << f.dsp.getPC().toWord()
			<< " r1=$" << f.dsp.regs().r[1].toWord() << std::dec << " services=" << services << " log=";
		for(TWord i = 0; i < services && i < 32; ++i)
			std::cout << (i ? "," : "") << f.mem.get(MemArea_X, g_logBase + i);
		std::cout << " expected=";
		for(size_t i = 0; i < _scenario.expectedLog.size(); ++i)
			std::cout << (i ? "," : "") << _scenario.expectedLog[i];
		std::cout << std::endl;

		verify(f.dsp.getPC().toWord() == park);

		// One service per undefined word run: fewer is a raise dropped or still held back.
		verify(services == _scenario.expectedLog.size());

		// Each service logs R1 as it was before the instruction after the undefined word wrote it.
		for(TWord i = 0; i < services; ++i)
			verify(f.mem.get(MemArea_X, g_logBase + i) == _scenario.expectedLog[i]);
	}

	// R5 carries the park address from setup to run(); the program never touches R5.
	void setPark(Fixture& _f, const TWord _park)
	{
		_f.dsp.regs().r[5].var = _park;
		_f.dsp.setPC(g_main);
	}

	void writePark(DSP& _dsp, const TWord _addr)
	{
		_dsp.memWriteP(_addr, 0x0af080);	// jmp >_addr
		_dsp.memWriteP(_addr + 1, _addr);
	}

	Scenario nothingElsePending()
	{
		return Scenario{"nothing else pending", [](Fixture& _f)
		{
			// Mask level 3 keeps every maskable source out; the interrupt under test is IPL 3.
			_f.dsp.regs().sr.var |= (SR_I0 | SR_I1);

			auto a = writeLines(_f.dsp, g_main, {
				undefinedWord(),
				asmLine("move #1,r1"),
				asmLine("nop"),
				undefinedWord(),
				undefinedWord(),
				asmLine("move #2,r1"),
			});

			// DO #3 over a body that holds two undefined words, neither at LA.
			const TWord doAddr = a;
			const TWord la = doAddr + 2 + 3;
			_f.dsp.memWriteP(a++, 0x060080 | (3 << 8));
			_f.dsp.memWriteP(a++, la);
			a = writeLines(_f.dsp, a, {
				undefinedWord(),
				asmLine("move (r1)+"),
				undefinedWord(),
				asmLine("nop"),
			});
			verify(a == la + 1);

			writePark(_f.dsp, a);
			setPark(_f, a);
		},
		{0, 1, 1, 2, 3, 3, 4, 4, 5}};
	}

	Scenario maskedInterruptQueued()
	{
		return Scenario{"masked interrupt queued", [](Fixture& _f)
		{
			// IRQA is IPL 0-2. With the mask at 3 it stays pending for the whole run.
			_f.dsp.regs().sr.var |= (SR_I0 | SR_I1);
			_f.dsp.injectInterrupt(Vba_IRQA);

			auto a = writeLines(_f.dsp, g_main, {
				asmLine("nop"),
				undefinedWord(),
				asmLine("move #1,r1"),
				asmLine("nop"),
				undefinedWord(),
				asmLine("move #2,r1"),
				undefinedWord(),
				undefinedWord(),
				asmLine("move #3,r1"),
			});

			writePark(_f.dsp, a);
			setPark(_f, a);
		},
		{0, 1, 2, 2}};
	}

	Scenario insideLongInterrupt()
	{
		return Scenario{"inside a long interrupt", [](Fixture& _f)
		{
			// Unmasked, so IRQA is taken; its vector JSR forms a long interrupt routine.
			_f.dsp.regs().sr.var &= ~(SR_I0 | SR_I1);

			_f.dsp.memWriteP(Vba_IRQA, 0x0bf080);	// jsr >g_handler
			_f.dsp.memWriteP(Vba_IRQA + 1, g_handler);

			auto h = writeLines(_f.dsp, g_handler, {
				undefinedWord(),
				asmLine("move #1,r1"),
				undefinedWord(),
				undefinedWord(),
				asmLine("move #2,r1"),
				asmLine("nop"),
				undefinedWord(),
				asmLine("move #3,r1"),
				asmLine("rti"),
			});
			verify(h < g_logBase);

			auto a = writeLines(_f.dsp, g_main, {
				asmLine("nop"),
				asmLine("nop"),
				asmLine("nop"),
				undefinedWord(),
				asmLine("move #4,r1"),
			});

			writePark(_f.dsp, a);
			setPark(_f, a);

			_f.dsp.injectInterrupt(Vba_IRQA);
		},
		{0, 1, 1, 2, 3}};
	}

	Scenario insideFastInterrupt()
	{
		return Scenario{"inside a fast interrupt", [](Fixture& _f)
		{
			// A fast interrupt routine is not interruptible (2.3.2.8), so the raise from its first word is
			// serviced once the routine ends, and it sees what the second word wrote.
			_f.dsp.regs().sr.var &= ~(SR_I0 | SR_I1);

			writeLines(_f.dsp, Vba_IRQA, {undefinedWord(), asmLine("move #7,r1")});

			auto a = writeLines(_f.dsp, g_main, {
				asmLine("move #6,r1"),
				asmLine("nop"),
				undefinedWord(),
				asmLine("move #8,r1"),
			});

			writePark(_f.dsp, a);
			setPark(_f, a);

			_f.dsp.injectInterrupt(Vba_IRQA);
		},
		{7, 6}};
	}

	// Every scenario runs even after one fails, so a red run shows which situations drop a raise.
	void runAll(const char* _engine, const TStep& _step, std::vector<std::string>& _failures)
	{
		for(const auto& scenario : {nothingElsePending(), maskedInterruptQueued(), insideLongInterrupt(), insideFastInterrupt()})
		{
			try
			{
				run(_engine, _step, scenario);
			}
			catch(const std::string& _err)
			{
				_failures.push_back(std::string(_engine) + " / " + scenario.name + ": " + _err);
			}
		}
	}
}

int main()
{
	try
	{
		std::vector<std::string> failures;

		runAll("interpreter", [](DSP& _dsp) { _dsp.execInterpreter(); }, failures);

		if constexpr(g_useJIT)
			runAll("jit", [](DSP& _dsp) { _dsp.exec(); }, failures);
		else
			std::cout << "jit not supported on this build, skipped" << std::endl;

		for(const auto& failure : failures)
			std::cout << "FAILED " << failure << std::endl;

		if(!failures.empty())
			throw std::string("see the failures above");
	}
	catch(const std::string& _err)
	{
		std::cout << "dsp56k_illegal_interrupt_each_raise_serviced FAILED: " << _err << std::endl;
		return -1;
	}
	catch(const std::exception& _err)
	{
		std::cout << "dsp56k_illegal_interrupt_each_raise_serviced FAILED: " << _err.what() << std::endl;
		return -1;
	}

	std::cout << "dsp56k_illegal_interrupt_each_raise_serviced passed" << std::endl;
	return 0;
}
