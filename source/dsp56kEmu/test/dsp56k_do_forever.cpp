// Tier T0: the guest programs live in this file as literal words
// plus assembler invocations; no firmware, kernel or .pch2 corpus is touched,
// so the check runs with NMG2_ARTIFACTS unset.
//
// WHAT IT MEASURES. The three properties that separate DO FOREVER from a
// counted DO, as specified by the DSP56300 Family Manual rev 2.0, DO FOREVER,
// p.13-60 and p.13-61 note 2:
//
//   1. LC is PUSHED but NOT WRITTEN. "The LC register is pushed onto the stack
//      but is not updated by this instruction." A program may therefore seed LC
//      before the instruction and use it as its own pass counter.
//   2. LF and FV are both set, and ENDDO restores both: BRKcc and ENDDO are
//      specified as SSL(LF,FV) -> SR.
//   3. "The LC register is never tested by the DO FOREVER instruction, and the
//      only way of terminating the loop process is to use either the ENDDO or
//      BRKcc instructions. LC is decremented every time PC = LA." So the count
//      passes through 1, through 0, and wraps at 24 bits, and none of that ends
//      the loop.
//
// THE MIRAGE THIS TEST REFUSES. A check that only asserts the loop body ran, or
// that the loop eventually exited to LA+1, passes against an implementation
// that arms LC with a large constant and lets the ordinary counted-loop
// machinery retire it. That is precisely the shape this test was written to
// discriminate: seeding LC with 3 and then observing the recorded value reach
// $ffffff can only happen if the instruction left LC alone AND refused to stop
// at 1 or at 0.
//
// BOTH ENGINES ARE DRIVEN. g_useJIT is a compile-time constant, so DSP::exec()
// reaches only one of the two engines on any given build. The interpreter is
// therefore driven through the public DSP::execInterpreter() directly, and the
// JIT through DSP::exec() guarded by g_useJIT.
//
// ONE LIMITATION, STATED RATHER THAN HIDDEN. theJitYieldsOncePerPass() asserts
// that the JIT hands control back after every single pass. If the JIT instead
// closed its own back edge on a forever loop, that call would never return and
// the failure would surface as a hang rather than as an assertion. What the
// per-pass assertion does catch cleanly is a block that runs more than one pass
// before returning, which is the same defect in its survivable form.

#include "dsp56kEmu/dsp.h"
#include "dsp56kEmu/memory.h"
#include "dsp56kEmu/peripherals56311.h"
#include "dsp56kEmu/unittests.h"

#include <iostream>

namespace
{
	using namespace dsp56k;

	DefaultMemoryValidator g_memoryValidator;

	// DO FOREVER, pinned literal and not assembler output: the instruction has no
	// fields at all ("Instruction Fields: None", p.13-60), so the whole 24-bit
	// word is opcodeinfo.h's DoForever pattern.
	constexpr TWord g_opDoForever = 0x000203;

	constexpr TWord g_seedLC = 0x000003;

	// X:<aa> short absolute is a 6-bit field, so every observable lives below $40.
	constexpr TWord g_addrRecordedLC = 0x3e;
	constexpr TWord g_addrRecordedSR = 0x3d;

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
			// DSP::setSR is private, so mutate through the public non-const regs().
			dsp.regs().sr.var |= (SR_I0 | SR_I1);
		}
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
	// the JIT, which sizes its entry table for the written range. A bare memory
	// write leaves m_jitEntries unsized and the first exec() jumps through a null
	// pointer.
	void writeP(DSP& _dsp, const TWord _addr, const std::vector<TWord>& _words)
	{
		for(uint32_t i = 0; i < _words.size(); ++i)
			_dsp.memWriteP(_addr + i, _words[i]);
	}

	// The self-terminating program. Every pass records LC and SR, and leaves the
	// loop only once the recorded LC has bit 23 set -- which, from a seed of 3, can
	// only happen after the counter has walked 3, 2, 1, 0 and wrapped to $ffffff.
	//
	//   $100  DO FOREVER
	//   $101    (extension word: LA)
	//   $102  move lc,x0
	//   $103  move x0,x:$3e
	//   $104  move sr,x1
	//   $105  move x1,x:$3d
	//   $106  jclr #23,x:$3e,tail
	//   $107    (extension word: tail)
	//   $108  enddo
	//   $109  nop                  <- LA, the last instruction in the loop
	struct WrapProgram
	{
		static constexpr TWord base = 0x100;

		TWord la = 0;
		TWord tail = 0;

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
			emit("move x0,x:$3e");
			emit("move sr,x1");
			emit("move x1,x:$3d");

			// jclr assembles to opcode + a 24-bit absolute target extension word. The
			// target is patched below once the layout is known.
			const auto jclrIndex = static_cast<TWord>(body.size());
			emit("jclr #23,x:$3e,$0");
			verify(body.size() == jclrIndex + 2);

			emit("enddo");
			emit("nop");

			// base + 2 words of DO FOREVER, then the body. The nop is the last word.
			const auto bodyBase = base + 2;
			tail = bodyBase + static_cast<TWord>(body.size()) - 1;
			la = tail;

			body[jclrIndex + 1] = tail;

			std::vector<TWord> program;
			program.push_back(g_opDoForever);
			program.push_back(la);
			for(const auto word : body)
				program.push_back(word);

			writeP(_dsp, base, program);
		}
	};

	void checkPostConditions(const Fixture& _f, const WrapProgram& _prog, const TWord _laBefore, const char* _engine)
	{
		const auto recordedLC = _f.mem.get(MemArea_X, g_addrRecordedLC);
		const auto recordedSR = _f.mem.get(MemArea_X, g_addrRecordedSR);
		const auto& r = _f.dsp.regs();

		std::cout << _engine
			<< ": recorded lc=$" << std::hex << recordedLC
			<< " recorded sr=$" << recordedSR
			<< " final lc=$" << r.lc.toWord()
			<< " final la=$" << r.la.toWord()
			<< " final sr=$" << r.sr.var
			<< " final pc=$" << _f.dsp.getPC().toWord()
			<< std::dec << std::endl;

		// (1) and (3). A seed of 3 that survives the instruction, and a loop that
		// refuses to retire on the count, is the ONLY way $ffffff can be recorded. An
		// implementation that armed LC with a large constant records that constant's
		// neighbourhood instead, and one that stopped at LC == 1 or LC == 0 records 1
		// or 0.
		verify(recordedLC == 0xffffff);

		// (2) observed from inside the loop.
		verify((recordedSR & SR_FV) != 0);
		verify((recordedSR & SR_LF) != 0);

		// (2) observed after ENDDO: both flags restored from the stacked SR.
		verify((r.sr.var & SR_FV) == 0);
		verify((r.sr.var & SR_LF) == 0);

		// LC and LA come back off the system stack unchanged.
		verify(r.lc.toWord() == g_seedLC);
		verify(r.la.toWord() == _laBefore);

		// ENDDO does not touch the PC, so control resumes at the instruction that
		// followed it -- here the nop that sits at LA.
		verify(_f.dsp.getPC().toWord() == _prog.tail);
	}

	void theInterpreterKeepsLCAndNeverRetiresOnTheCount()
	{
		Fixture f;
		WrapProgram prog;
		prog.write(f.dsp);

		f.mem.set(MemArea_X, g_addrRecordedLC, 0);
		f.mem.set(MemArea_X, g_addrRecordedSR, 0);

		const auto laBefore = f.dsp.regs().la.toWord();
		f.dsp.regs().lc.var = g_seedLC;
		f.dsp.setPC(WrapProgram::base);

		// DSP::do_execImpl runs the whole loop inside this single call and returns
		// only once ENDDO has popped the loop off the system stack.
		f.dsp.execInterpreter();

		checkPostConditions(f, prog, laBefore, "interpreter");
	}

	void theJitKeepsLCAndNeverRetiresOnTheCount()
	{
		if constexpr(!g_useJIT)
		{
			std::cout << "jit: not supported on this build, skipped" << std::endl;
			return;
		}
		else
		{
			Fixture f;
			WrapProgram prog;
			prog.write(f.dsp);

			f.mem.set(MemArea_X, g_addrRecordedLC, 0);
			f.mem.set(MemArea_X, g_addrRecordedSR, 0);

			const auto laBefore = f.dsp.regs().la.toWord();
			f.dsp.regs().lc.var = g_seedLC;
			f.dsp.setPC(WrapProgram::base);

			// One JIT block per exec(). Five passes plus the head and the tail blocks
			// fit far inside this bound; a regression that hangs is caught by CTest,
			// and one that exits early is caught by the assertions below.
			constexpr uint32_t maxExecCalls = 256;
			uint32_t execCalls = 0;

			while(execCalls < maxExecCalls && f.dsp.getPC().toWord() != prog.tail)
			{
				f.dsp.exec();
				++execCalls;
			}

			verify(execCalls < maxExecCalls);

			checkPostConditions(f, prog, laBefore, "jit");
		}
	}

	// The tight case: a loop body with no branch in it, so the whole loop is one JIT
	// block and the block's own back edge is the thing under test. A forever loop
	// must not close that edge -- if it did, exec() would never return and the
	// interrupt poll in DSP::execJit would never run again.
	//
	//   $200  DO FOREVER
	//   $201    (extension word: LA = $203)
	//   $202  move lc,x0
	//   $203  move x0,x:$3e        <- LA
	void theJitYieldsOncePerPass()
	{
		if constexpr(!g_useJIT)
		{
			std::cout << "jit yield: not supported on this build, skipped" << std::endl;
			return;
		}
		else
		{
			constexpr TWord base = 0x200;

			Fixture f;

			Assembler assembler;
			TWord w[8];

			std::vector<TWord> body;
			auto n = assembleOne(assembler, "move lc,x0", w);
			for(uint32_t i = 0; i < n; ++i) body.push_back(w[i]);
			n = assembleOne(assembler, "move x0,x:$3e", w);
			for(uint32_t i = 0; i < n; ++i) body.push_back(w[i]);

			const auto la = base + 2 + static_cast<TWord>(body.size()) - 1;

			std::vector<TWord> program;
			program.push_back(g_opDoForever);
			program.push_back(la);
			for(const auto word : body)
				program.push_back(word);

			writeP(f.dsp, base, program);

			f.dsp.regs().lc.var = g_seedLC;
			f.dsp.setPC(base);

			// Run until the loop is armed. Arming is observable as SR.LF, and the
			// instruction that arms it must NOT have touched LC.
			uint32_t execCalls = 0;
			while(execCalls < 16 && (f.dsp.regs().sr.var & SR_LF) == 0)
			{
				f.dsp.exec();
				++execCalls;
			}

			verify((f.dsp.regs().sr.var & SR_LF) != 0);
			verify((f.dsp.regs().sr.var & SR_FV) != 0);
			verify(f.dsp.regs().lc.toWord() == g_seedLC);

			// Eight passes from a seed of 3 walk the counter through 1, through 0 and
			// past the 24-bit wrap. Each exec() must advance it by exactly one: more
			// than one means the block ran several passes before handing control back,
			// which is the survivable form of a closed back edge.
			TWord expected = g_seedLC;

			for(uint32_t pass = 0; pass < 8; ++pass)
			{
				f.dsp.exec();
				expected = (expected - 1) & 0xffffff;

				const auto lc = f.dsp.regs().lc.toWord();

				if(lc != expected)
					std::cout << "jit yield: pass " << pass << " lc=$" << std::hex << lc
						<< " expected $" << expected << std::dec << std::endl;

				verify(lc == expected);
				verify((f.dsp.regs().sr.var & SR_LF) != 0);
			}

			// $ffffff was crossed and the loop is still running.
			verify(f.dsp.regs().lc.toWord() == 0xfffffb);
			verify(f.mem.get(MemArea_X, g_addrRecordedLC) != 0);
		}
	}
}

int main()
{
	try
	{
		theInterpreterKeepsLCAndNeverRetiresOnTheCount();
		theJitKeepsLCAndNeverRetiresOnTheCount();
		theJitYieldsOncePerPass();
	}
	catch(const std::string& _err)
	{
		std::cout << "dsp56k_do_forever FAILED: " << _err << std::endl;
		return -1;
	}

	std::cout << "dsp56k_do_forever passed" << std::endl;
	return 0;
}
