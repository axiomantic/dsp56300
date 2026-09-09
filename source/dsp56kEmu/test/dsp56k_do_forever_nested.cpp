// Tier T0: the guest program lives in this file as literal words plus assembler
// invocations; no firmware, kernel or .pch2 corpus is touched, so the check runs
// with NMG2_ARTIFACTS unset.
//
// WHAT IT MEASURES. SR.FV names the INNERMOST loop, and an ordinary counted DO
// nested inside a DO FOREVER must say so. DSP56300 Family Manual rev 2.0
// specifies ENDDO and BRKcc as SSL(LF,FV) -> SR: FV travels on the system stack
// beside LF, which is only coherent if every DO states its own kind. A counted
// DO that inherited the enclosing forever loop's FV would describe itself as a
// loop no count can retire.
//
// THE SHAPE THIS DISCRIMINATES. The JIT decides at RUN TIME, from SR.FV, whether
// the loop count may retire a loop, because a loop whose body spans several
// blocks gives the block at the loop end no compile-time knowledge of the
// instruction that opened it. An implementation that sets FV on DO FOREVER and
// then leaves it alone on a counted DO therefore compiles a correct forever loop
// and an inner counted loop that never ends. A check that drives a forever loop
// on its own cannot see this: the defect needs one loop inside the other.
//
// THE ORDER OF THE ASSERTIONS IS LOAD-BEARING. The JIT arm reads SR.FV at the
// instant the inner loop is armed and BEFORE the inner body is allowed to run.
// An implementation that leaves FV set fails there, as an assertion. Were that
// check placed after the inner loop, the same implementation would spin inside
// one compiled block and the failure would arrive as a timeout instead.

#include "dsp56kEmu/dsp.h"
#include "dsp56kEmu/memory.h"
#include "dsp56kEmu/peripherals56311.h"
#include "dsp56kEmu/unittests.h"

#include <iostream>
#include <vector>

namespace
{
	using namespace dsp56k;

	DefaultMemoryValidator g_memoryValidator;

	// DO FOREVER has no instruction fields at all, so the whole 24-bit word is
	// opcodeinfo.h's DoForever pattern.
	constexpr TWord g_opDoForever = 0x000203;

	constexpr TWord g_seedLC = 0x000003;

	// X:<aa> short absolute is a 6-bit field, so every observable lives below $40.
	constexpr TWord g_addrSrInner = 0x3d;
	constexpr TWord g_addrSrOuter = 0x3c;
	constexpr TWord g_addrRecordedLC = 0x3e;

	struct Fixture
	{
		Peripherals56311 p{96000};
		Memory mem{g_memoryValidator, 0x080000, 0x800000, 0x200000};
		DSP dsp{mem, &p, &p.ySpace()};

		Fixture()
		{
			// Mask interrupts. The 56311 ESAI raises its transmit-data-empty condition
			// from construction and this fixture populates no vector.
			dsp.regs().sr.var |= (SR_I0 | SR_I1);
		}
	};

	void writeP(DSP& _dsp, const TWord _addr, const std::vector<TWord>& _words)
	{
		for(uint32_t i = 0; i < _words.size(); ++i)
			_dsp.memWriteP(_addr + i, _words[i]);
	}

	//   $100  DO FOREVER            (extension word: outer LA)
	//   $102    do #$3,innerLA
	//   $104      move sr,x1
	//   $105      move x1,x:$3d     <- inner LA
	//   $106    move sr,x1
	//   $107    move x1,x:$3c
	//   $108    move lc,x0
	//   $109    move x0,x:$3e
	//   $10a  jclr #23,x:$3e,tail
	//   $10c  enddo
	//   $10d  nop                   <- outer LA
	struct Program
	{
		static constexpr TWord base = 0x100;

		TWord outerLA = 0;
		TWord innerLA = 0;
		TWord tail = 0;

		void write(DSP& _dsp)
		{
			Assembler assembler;

			std::vector<TWord> body;

			auto emit = [&](const char* _src)
			{
				const auto r = assembler.assemble(_src);
				if(!r.success())
					std::cout << "assemble error " << static_cast<int>(r.error) << " for: " << _src << std::endl;
				verify(r.success());
				for(uint32_t i = 0; i < r.wordCount; ++i)
					body.push_back(r.word[i]);
				return r.wordCount;
			};

			const auto bodyBase = base + 2;

			// The inner DO's own extension word carries its LA, patched below.
			const auto innerDoIndex = static_cast<TWord>(body.size());
			verify(emit("do #$3,$0") == 2);

			emit("move sr,x1");
			const auto innerLastIndex = static_cast<TWord>(body.size());
			emit("move x1,x:$3d");
			innerLA = bodyBase + innerLastIndex;
			body[innerDoIndex + 1] = innerLA;

			emit("move sr,x1");
			emit("move x1,x:$3c");
			emit("move lc,x0");
			emit("move x0,x:$3e");

			const auto jclrIndex = static_cast<TWord>(body.size());
			verify(emit("jclr #23,x:$3e,$0") == 2);

			emit("enddo");
			emit("nop");

			tail = bodyBase + static_cast<TWord>(body.size()) - 1;
			outerLA = tail;
			body[jclrIndex + 1] = tail;

			std::vector<TWord> program;
			program.push_back(g_opDoForever);
			program.push_back(outerLA);
			for(const auto word : body)
				program.push_back(word);

			writeP(_dsp, base, program);
		}
	};

	void checkRecordedFlags(const Fixture& _f, const char* _engine)
	{
		const auto srInner = _f.mem.get(MemArea_X, g_addrSrInner);
		const auto srOuter = _f.mem.get(MemArea_X, g_addrSrOuter);

		std::cout << _engine << ": sr inside the counted loop=$" << std::hex << srInner
			<< " sr inside the forever loop=$" << srOuter << std::dec << std::endl;

		// Inside the nested counted DO: still in a loop, and NOT in a forever one.
		verify((srInner & SR_LF) != 0);
		verify((srInner & SR_FV) == 0);

		// Back in the enclosing forever loop, once the inner ENDDO has popped: the
		// stacked FV is restored.
		verify((srOuter & SR_LF) != 0);
		verify((srOuter & SR_FV) != 0);
	}

	void checkExit(const Fixture& _f, const Program& _prog)
	{
		const auto& r = _f.dsp.regs();

		// The outer forever loop left on ENDDO and on nothing else: its own LC walked
		// 3, 2, 1, 0 and wrapped, which the nested counted loop must not have disturbed.
		verify(_f.mem.get(MemArea_X, g_addrRecordedLC) == 0xffffff);

		verify((r.sr.var & SR_FV) == 0);
		verify((r.sr.var & SR_LF) == 0);
		verify(r.lc.toWord() == g_seedLC);
		verify(_f.dsp.getPC().toWord() == _prog.tail);
	}

	void theInterpreterClearsFVForACountedLoopInsideAForeverLoop()
	{
		Fixture f;
		Program prog;
		prog.write(f.dsp);

		f.mem.set(MemArea_X, g_addrSrInner, 0);
		f.mem.set(MemArea_X, g_addrSrOuter, 0);
		f.mem.set(MemArea_X, g_addrRecordedLC, 0);

		f.dsp.regs().lc.var = g_seedLC;
		f.dsp.setPC(Program::base);

		// DSP::do_execImpl runs the whole outer loop inside this one call and returns
		// once ENDDO has popped it off the system stack.
		f.dsp.execInterpreter();

		checkRecordedFlags(f, "interpreter");
		checkExit(f, prog);
	}

	void theJitClearsFVForACountedLoopInsideAForeverLoop()
	{
		if constexpr(!g_useJIT)
		{
			std::cout << "jit: not supported on this build, skipped" << std::endl;
			return;
		}
		else
		{
			Fixture f;
			Program prog;
			prog.write(f.dsp);

			f.mem.set(MemArea_X, g_addrSrInner, 0);
			f.mem.set(MemArea_X, g_addrSrOuter, 0);
			f.mem.set(MemArea_X, g_addrRecordedLC, 0);

			f.dsp.regs().lc.var = g_seedLC;
			f.dsp.setPC(Program::base);

			// The DO that opens the inner loop writes LA and LC, which ends the compiled
			// block, so control comes back here with the inner loop armed and its body
			// not yet run. LA carries the arming: it holds the inner loop's end address
			// and nothing else in this program writes that value.
			constexpr uint32_t maxArmingCalls = 64;
			uint32_t execCalls = 0;

			while(execCalls < maxArmingCalls && f.dsp.regs().la.toWord() != prog.innerLA)
			{
				f.dsp.exec();
				++execCalls;
			}

			verify(execCalls < maxArmingCalls);
			verify(f.dsp.regs().la.toWord() == prog.innerLA);

			// The assertion this whole file exists for, taken before the inner body is
			// allowed to run so that a wrong answer is an assertion and not a timeout.
			verify((f.dsp.regs().sr.var & SR_LF) != 0);
			verify((f.dsp.regs().sr.var & SR_FV) == 0);

			// Four outer passes, each running a counted loop of three, plus the head and
			// tail blocks, fit far inside this bound.
			constexpr uint32_t maxExecCalls = 4096;

			while(execCalls < maxExecCalls && f.dsp.getPC().toWord() != prog.tail)
			{
				f.dsp.exec();
				++execCalls;
			}

			verify(execCalls < maxExecCalls);

			checkRecordedFlags(f, "jit");
			checkExit(f, prog);
		}
	}
}

int main()
{
	try
	{
		theInterpreterClearsFVForACountedLoopInsideAForeverLoop();
		theJitClearsFVForACountedLoopInsideAForeverLoop();
	}
	catch(const std::string& _err)
	{
		std::cout << "dsp56k_do_forever_nested FAILED: " << _err << std::endl;
		return -1;
	}

	std::cout << "dsp56k_do_forever_nested passed" << std::endl;
	return 0;
}
