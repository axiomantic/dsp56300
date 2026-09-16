// Tier T0: the guest program lives in this file as literal words plus assembler
// invocations; no firmware, kernel or .pch2 corpus is touched, so the check runs
// with NMG2_ARTIFACTS unset.
//
// WHAT IT MEASURES. DSP56300 Family Manual rev 2.0, DO FOREVER, p.13-61 note 2:
// "the only way of terminating the loop process is to use either the ENDDO or
// BRKcc instructions". A DO FOREVER whose body reaches a taken BRKcc must leave
// the loop; one whose BRKcc is not taken must stay in it.
//
// BRKcc unstacks the loop the way ENDDO does -- SSL(LF,FV) -> SR, then LC and LA
// off the stack -- and resumes at LA+1 rather than falling through, so the
// post-conditions below separate a real implementation from a jump.
//
// WHY THIS TEST CANNOT HANG. The interpreter runs an entire DO loop inside one
// DSP::execInterpreter() call, so a BRKcc that fails to terminate the loop does
// not return. A test that detected that by not returning would be no test at
// all: it would report a defect as an unbounded run. DSP::do_execImpl polls
// m_terminate on every pass for exactly this reason, so a watchdog thread trips
// it after a wall-clock bound, the call returns, and the bound itself is
// asserted on. A build where BRKcc does nothing fails here in a bounded time
// with a named reason instead of spinning.
//
// BOTH ENGINES ARE DRIVEN. g_useJIT is a compile-time constant, so DSP::exec()
// reaches only one of the two engines on any given build. The interpreter is
// driven through DSP::execInterpreter() directly, the JIT through DSP::exec()
// guarded by g_useJIT. The JIT hands control back at the end of every block, so
// there a bound on the number of exec() calls is enough.

#include "dsp56kEmu/dsp.h"
#include "dsp56kEmu/memory.h"
#include "dsp56kEmu/peripherals56311.h"
#include "dsp56kEmu/unittests.h"

#include <atomic>
#include <chrono>
#include <iostream>
#include <thread>

namespace
{
	using namespace dsp56k;

	DefaultMemoryValidator g_memoryValidator;

	// Both instructions are pinned literals rather than assembler output. DO FOREVER
	// has no fields at all ("Instruction Fields: None", p.13-60), and BRKcc carries
	// only CCCC, so opcodeinfo.h's patterns fix the whole 24-bit word:
	//   DO FOREVER  000000000000001000000011
	//   BRKcc       00000000000000100001CCCC   with CCCC = 1010, "eq"
	constexpr TWord g_opDoForever = 0x000203;
	constexpr TWord g_opBrkEq = 0x00021a;

	// The loop runs until LC counts down to this value, so the recorded LC proves
	// how many passes happened rather than merely that one did.
	constexpr TWord g_seedLC = 0x000004;
	constexpr TWord g_breakAtLC = 0x000001;

	// The pass after which the break is taken is the one the trailing probe last saw:
	// LC is decremented on the wrap, so the pass before the breaking one carries
	// g_breakAtLC + 1.
	constexpr TWord g_lastLCbeforeBreak = g_breakAtLC + 1;

	// X:<aa> short absolute is a 6-bit field, so every observable lives below $40.
	// One probe sits ahead of the break and one behind it.
	constexpr TWord g_addrLCbeforeBreak = 0x3d;
	constexpr TWord g_addrLCafterBreak = 0x3e;

	struct Fixture
	{
		Peripherals56311 p{96000};
		Memory mem{g_memoryValidator, 0x080000, 0x800000, 0x200000};
		DSP dsp{mem, &p, &p.ySpace()};

		Fixture()
		{
			// Mask interrupts (SR.I1 = SR.I0 = 1). The 56311 ESAI raises its
			// transmit-data-empty condition from construction; an unmasked interrupt
			// would hijack the guest to a vector this fixture never populated.
			dsp.regs().sr.var |= (SR_I0 | SR_I1);
		}
	};

	// Trips DSP::terminate() after a wall-clock bound. do_execImpl polls the flag on
	// every pass, so an interpreter loop with no working exit returns instead of
	// spinning, and fired() says which of the two happened.
	class Watchdog
	{
	public:
		Watchdog(DSP& _dsp, const unsigned _ms) : m_dsp(_dsp)
		{
			m_thread = std::thread([this, _ms]
			{
				const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(_ms);

				while(!m_stop.load(std::memory_order_relaxed))
				{
					if(std::chrono::steady_clock::now() >= deadline)
					{
						m_fired.store(true, std::memory_order_relaxed);
						m_dsp.terminate();
						return;
					}
					std::this_thread::sleep_for(std::chrono::milliseconds(2));
				}
			});
		}

		~Watchdog() { stop(); }

		void stop()
		{
			m_stop.store(true, std::memory_order_relaxed);
			if(m_thread.joinable())
				m_thread.join();
		}

		bool fired() const { return m_fired.load(std::memory_order_relaxed); }

	private:
		DSP& m_dsp;
		std::atomic<bool> m_stop{false};
		std::atomic<bool> m_fired{false};
		std::thread m_thread;
	};

	TWord assembleOne(Assembler& _assembler, const char* _source, TWord* _out)
	{
		const auto r = _assembler.assemble(_source);

		if(!r.success())
			std::cout << "assemble error " << static_cast<int>(r.error) << " for: " << _source << std::endl;

		verify(r.success());

		for(uint32_t i = 0; i < r.wordCount; ++i)
			_out[i] = r.word[i];

		return r.wordCount;
	}

	// Program words go through memWriteP, not Memory::set: the write path notifies
	// the JIT, which sizes its entry table for the written range.
	void writeP(DSP& _dsp, const TWord _addr, const std::vector<TWord>& _words)
	{
		for(uint32_t i = 0; i < _words.size(); ++i)
			_dsp.memWriteP(_addr + i, _words[i]);
	}

	// A forever loop whose only exit is a conditional BRKcc, taken on the fourth
	// pass and not taken on the first three:
	//
	//   $100  DO FOREVER
	//   $101    (extension word: LA)
	//   $102  move lc,x0
	//   $103  move x0,x:$3d      record the count this pass saw, before the break
	//   $104  move lc,a
	//   $105  cmp #1,a           Z set only when LC has counted down to 1
	//   $106  brkeq
	//   $107  move lc,x0
	//   $108  move x0,x:$3e      record it again, behind the break
	//   $109  nop                <- LA, the last instruction in the loop
	//
	// LC is seeded with 4 and decremented on every wrap, so the taken pass is the
	// fourth. Both arms of the condition are therefore exercised by one run.
	//
	// THE SECOND PROBE IS THE POINT. Everything ahead of the break runs on every
	// pass and so cannot tell a break that stops the pass from one that merely sets
	// the PC. The probe behind it can: a taken break must not reach it, so it holds
	// the count of the pass before the break, and it holds the count restored off
	// the system stack -- the seed -- if the instructions between the break and the
	// loop address run with the loop already unstacked.
	struct BreakProgram
	{
		static constexpr TWord base = 0x100;

		TWord la = 0;
		TWord exit = 0;

		// breakEver=false compares a constant against a different constant, so the
		// condition is false on every pass and the loop has no reachable exit.
		bool breakEver = true;

		void write(DSP& _dsp)
		{
			Assembler assembler;
			TWord w[8];

			std::vector<TWord> body;

			auto emit = [&](const char* _src)
			{
				const auto n = assembleOne(assembler, _src, w);
				for(uint32_t i = 0; i < n; ++i)
					body.push_back(w[i]);
			};

			emit("move lc,x0");
			emit("move x0,x:$3d");
			emit(breakEver ? "move lc,a" : "move #2,a");
			emit("cmp #1,a");

			body.push_back(g_opBrkEq);

			emit("move lc,x0");
			emit("move x0,x:$3e");

			emit("nop");

			// base + 2 words of DO FOREVER, then the body. The nop is the last word.
			const auto bodyBase = base + 2;
			la = bodyBase + static_cast<TWord>(body.size()) - 1;
			exit = la + 1;

			std::vector<TWord> program;
			program.push_back(g_opDoForever);
			program.push_back(la);
			for(const auto word : body)
				program.push_back(word);

			// Landing area after the loop. The break target has to be real program
			// memory: the JIT sizes its entry table from what memWriteP has seen, and
			// chaining into an address that was never written jumps through a null
			// entry. A guest that breaks out of a loop always has code to break out to.
			verify(assembleOne(assembler, "nop", w) == 1);
			for(uint32_t i = 0; i < 4; ++i)
				program.push_back(w[0]);

			writeP(_dsp, base, program);
		}
	};

	void checkPostConditions(const Fixture& _f, const BreakProgram& _prog, const TWord _laBefore, const char* _engine)
	{
		const auto lcBeforeBreak = _f.mem.get(MemArea_X, g_addrLCbeforeBreak);
		const auto lcAfterBreak = _f.mem.get(MemArea_X, g_addrLCafterBreak);
		const auto& r = _f.dsp.regs();

		std::cout << _engine
			<< ": lc before break=$" << std::hex << lcBeforeBreak
			<< " lc after break=$" << lcAfterBreak
			<< " final lc=$" << r.lc.toWord()
			<< " final la=$" << r.la.toWord()
			<< " final sr=$" << r.sr.var
			<< " final pc=$" << _f.dsp.getPC().toWord()
			<< std::dec << std::endl;

		// The loop ran until the count reached the breaking value. A BRKcc taken on
		// the first pass -- or one that never runs because the condition is ignored --
		// records the seed instead.
		verify(lcBeforeBreak == g_breakAtLC);

		// Nothing behind the break ran on the breaking pass, so the last count it saw
		// is the one from the pass before. g_seedLC here would mean the instructions
		// between the break and the loop address ran after the loop was unstacked and
		// read LC back off the system stack.
		verify(lcAfterBreak == g_lastLCbeforeBreak);

		// BRKcc restores LF and FV from the stacked SR, exactly as ENDDO does.
		verify((r.sr.var & SR_LF) == 0);
		verify((r.sr.var & SR_FV) == 0);

		// LC and LA come back off the system stack unchanged.
		verify(r.lc.toWord() == g_seedLC);
		verify(r.la.toWord() == _laBefore);

		// The exit that separates BRKcc from ENDDO: control resumes past the last
		// instruction of the loop, not at it.
		verify(_f.dsp.getPC().toWord() == _prog.exit);
	}

	void theInterpreterLeavesTheLoopOnTheTakenBreak()
	{
		Fixture f;
		BreakProgram prog;
		prog.write(f.dsp);

		f.mem.set(MemArea_X, g_addrLCbeforeBreak, 0);
		f.mem.set(MemArea_X, g_addrLCafterBreak, 0);

		const auto laBefore = f.dsp.regs().la.toWord();
		f.dsp.regs().lc.var = g_seedLC;
		f.dsp.setPC(BreakProgram::base);

		// Four passes of a six-word body. Two seconds is many orders of magnitude
		// more than that needs, so the watchdog firing means the loop had no exit.
		Watchdog wd(f.dsp, 2000);

		f.dsp.execInterpreter();

		wd.stop();

		if(wd.fired())
			std::cout << "interpreter: watchdog fired, the loop did not terminate" << std::endl;

		verify(!wd.fired());

		checkPostConditions(f, prog, laBefore, "interpreter");
	}

	void theJitLeavesTheLoopOnTheTakenBreak()
	{
		if constexpr(!g_useJIT)
		{
			std::cout << "jit: not supported on this build, skipped" << std::endl;
			return;
		}
		else
		{
			Fixture f;
			BreakProgram prog;
			prog.write(f.dsp);

			f.mem.set(MemArea_X, g_addrLCbeforeBreak, 0);
			f.mem.set(MemArea_X, g_addrLCafterBreak, 0);

			const auto laBefore = f.dsp.regs().la.toWord();
			f.dsp.regs().lc.var = g_seedLC;
			f.dsp.setPC(BreakProgram::base);

			// The JIT returns at the end of every block, so a bound on the number of
			// blocks bounds the whole run. Four passes plus the head and tail blocks fit
			// far inside this, and a loop that never breaks exhausts it.
			constexpr uint32_t maxExecCalls = 256;
			uint32_t execCalls = 0;

			while(execCalls < maxExecCalls && f.dsp.getPC().toWord() != prog.exit)
			{
				f.dsp.exec();
				++execCalls;
			}

			if(execCalls >= maxExecCalls)
				std::cout << "jit: exec bound reached, the loop did not terminate" << std::endl;

			verify(execCalls < maxExecCalls);

			checkPostConditions(f, prog, laBefore, "jit");
		}
	}

	// The condition must be honoured in both directions. The same loop with a
	// condition that is false on every pass has no reachable exit, so the watchdog
	// is what ends this run. A BRKcc that ignores its condition and always breaks
	// ends the loop instead, and the watchdog does not fire -- which is what this
	// case discriminates. It is also the shape the reviewer found hanging: before
	// BRKcc existed, this program ran without bound and printed nothing.
	void anUntakenBreakDoesNotLeaveTheLoop()
	{
		Fixture f;
		BreakProgram prog;
		prog.breakEver = false;
		prog.write(f.dsp);

		f.mem.set(MemArea_X, g_addrLCbeforeBreak, 0);
		f.mem.set(MemArea_X, g_addrLCafterBreak, 0);

		f.dsp.regs().lc.var = g_seedLC;
		f.dsp.setPC(BreakProgram::base);

		Watchdog wd(f.dsp, 300);

		f.dsp.execInterpreter();

		wd.stop();

		std::cout << "interpreter (untaken): watchdog fired=" << wd.fired()
			<< " lc before break=$" << std::hex << f.mem.get(MemArea_X, g_addrLCbeforeBreak)
			<< " lc after break=$" << f.mem.get(MemArea_X, g_addrLCafterBreak)
			<< std::dec << std::endl;

		verify(wd.fired());

		// Still inside the loop when the watchdog stopped it.
		verify((f.dsp.regs().sr.var & SR_LF) != 0);
		verify((f.dsp.regs().sr.var & SR_FV) != 0);

		// The body kept running rather than falling out after one pass, and an untaken
		// break let the pass carry on past it.
		verify(f.mem.get(MemArea_X, g_addrLCbeforeBreak) != 0);
		verify(f.mem.get(MemArea_X, g_addrLCafterBreak) != 0);
	}
}

int main()
{
	try
	{
		theInterpreterLeavesTheLoopOnTheTakenBreak();
		theJitLeavesTheLoopOnTheTakenBreak();
		anUntakenBreakDoesNotLeaveTheLoop();
	}
	catch(const std::string& _err)
	{
		std::cout << "dsp56k_brkcc FAILED: " << _err << std::endl;
		return -1;
	}
	catch(const std::exception& _err)
	{
		std::cout << "dsp56k_brkcc FAILED: " << _err.what() << std::endl;
		return -1;
	}

	std::cout << "dsp56k_brkcc passed" << std::endl;
	return 0;
}
