// Tier T0: the guest programs live in this file as assembler invocations plus one
// pinned literal; no firmware, kernel or .pch2 corpus is touched, so the check runs
// with NMG2_ARTIFACTS unset.
//
// WHAT IT MEASURES. LA is a writable register (DSP56300 Family Manual rev 2.0,
// Loop Address Register, p.5-13), so a guest may move the end of a loop that is
// already running. DSP::do_execImpl compares the PC against the live reg.la on
// every pass and therefore honours such a write on the pass that follows it. The
// JIT derives the end of a loop from the operand word of the DO instruction when
// it compiles the block, so a write to LA has to reach the block boundaries and
// the loop table as well, or the JIT keeps running the loop the program used to
// have.
//
// The write goes through MOVEP because that is the instruction whose peripheral to
// register direction declared no register effects at all, which is what kept the
// JIT from noticing. Its destination here is LA, where an undeclared write changes
// control flow rather than data.
//
// BOTH ENGINES ARE DRIVEN. g_useJIT is a compile-time constant, so DSP::exec()
// reaches only one of the two engines on any given build. The interpreter is
// driven through the public DSP::execInterpreter() directly, and the JIT through
// DSP::exec() guarded by g_useJIT.
//
// THE MIRAGE THIS TEST REFUSES. Asserting only that the loop eventually left, or
// that the instructions past the original end ran at all, passes against an engine
// that ignores the write: once a loop retires at the end it was compiled with,
// execution falls through the extension anyway. Both programs therefore record a
// value that can only be produced from INSIDE the extended loop -- the moved LA in
// the first, the live loop count in the second -- against a sentinel the engine
// that ignores the write leaves standing.

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

	// DO FOREVER has no instruction fields at all (p.13-60), so the whole 24 bit word
	// is opcodeinfo.h's DoForever pattern.
	constexpr TWord g_opDoForever = 0x000203;

	// X:<aa> short absolute is a 6 bit field, so every observable lives below $40.
	constexpr TWord g_addrRecordedLA = 0x31;
	constexpr TWord g_addrRecordedLC = 0x32;

	// Neither program can produce these, so a recorded value that still reads as one
	// means the instruction that writes it never ran inside the extended loop.
	constexpr TWord g_sentinelLA = 0x0a0a0a;
	constexpr TWord g_sentinelLC = 0x0b0b0b;

	// The loop count in force before the counted DO. The counted program records LC
	// from inside its extension: an engine that runs the extension only as fall
	// through after the loop retired sees this value restored off the system stack
	// instead of a live count.
	constexpr TWord g_outerLC = 0x123456;

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

			mem.set(MemArea_X, g_addrRecordedLA, g_sentinelLA);
			mem.set(MemArea_X, g_addrRecordedLC, g_sentinelLC);
		}
	};

	std::vector<TWord> assembleOne(Assembler& _assembler, const char* _source)
	{
		const auto r = _assembler.assemble(_source);

		if(!r.success())
			std::cout << "assemble error " << static_cast<int>(r.error) << " for: " << _source << std::endl;

		verify(r.success());

		std::vector<TWord> out;
		for(uint32_t i = 0; i < r.wordCount; ++i)
			out.push_back(r.word[i]);
		return out;
	}

	// Program words go through memWriteP, not Memory::set: the write path notifies the
	// JIT, which sizes its entry table for the written range. A bare memory write
	// leaves m_jitEntries unsized and the first exec() jumps through a null pointer.
	void writeP(DSP& _dsp, const TWord _addr, const std::vector<TWord>& _words)
	{
		for(uint32_t i = 0; i < _words.size(); ++i)
			_dsp.memWriteP(_addr + i, _words[i]);
	}

	void append(std::vector<TWord>& _dst, const std::vector<TWord>& _src)
	{
		for(const auto w : _src)
			_dst.push_back(w);
	}

	// HORX sits at $ffffc6, inside the pppppp page, so this is Movep_Spp with W = 0.
	std::vector<TWord> assembleReadHorxToLA(Assembler& _assembler)
	{
		const auto words = assembleOne(_assembler, "movep x:<<$ffffc6,la");
		verify(words.size() == 1);
		return words;
	}

	// The extension is fed one word per pass. A run that ignores the write to LA
	// re-executes the MOVEP instead of walking on, so the queue has to outlast the
	// bound the test drives the engine to rather than exactly match the correct pass
	// count.
	void fillHostReceive(Fixture& _f, const TWord _value, const uint32_t _count)
	{
		std::vector<TWord> data(_count, _value);
		_f.p.getHDI08().writeRX(data);
	}

	struct ForeverProgram
	{
		// $100  DO FOREVER
		// $101    (extension word: LA = $102, the MOVEP alone)
		// $102  movep x:$ffffc6,la    <- moves LA to the ENDDO below
		// ---- everything past here is inside the loop only after that write ----
		// $103  move la,x0
		// $104  move x0,x:$31
		// $105  enddo                 <- LA after the write
		// $106  jmp $106              <- the program parks here
		static constexpr TWord base = 0x100;

		TWord park = 0;
		TWord movedLA = 0;

		void write(DSP& _dsp)
		{
			Assembler assembler;

			std::vector<TWord> body;
			append(body, assembleReadHorxToLA(assembler));

			const auto originalLA = base + 2;
			verify(body.size() == 1);

			append(body, assembleOne(assembler, "move la,x0"));
			append(body, assembleOne(assembler, "move x0,x:$31"));

			const auto enddoIndex = static_cast<TWord>(body.size());
			append(body, assembleOne(assembler, "enddo"));
			verify(body.size() == enddoIndex + 1);

			movedLA = base + 2 + enddoIndex;
			park = base + 2 + static_cast<TWord>(body.size());

			std::vector<TWord> program;
			program.push_back(g_opDoForever);
			program.push_back(originalLA);
			append(program, body);

			// A self jump is the only way to stop without leaving the engine to decode
			// whatever follows the program in a P memory nobody wrote.
			char parkSrc[32];
			snprintf(parkSrc, sizeof(parkSrc), "jmp $%x", park);
			append(program, assembleOne(assembler, parkSrc));

			writeP(_dsp, base, program);
		}
	};

	struct CountedProgram
	{
		// $200  do #2,$202
		// $202  movep x:$ffffc6,la    <- moves LA past the recording instructions
		// $203  move lc,x0
		// $204  move x0,x:$32
		// $205  nop                   <- LA after the write
		// $206  jmp $206
		static constexpr TWord base = 0x200;

		TWord park = 0;
		TWord movedLA = 0;

		void write(DSP& _dsp)
		{
			Assembler assembler;

			std::vector<TWord> body;
			append(body, assembleReadHorxToLA(assembler));
			verify(body.size() == 1);

			append(body, assembleOne(assembler, "move lc,x0"));
			append(body, assembleOne(assembler, "move x0,x:$32"));

			const auto lastIndex = static_cast<TWord>(body.size());
			append(body, assembleOne(assembler, "nop"));
			verify(body.size() == lastIndex + 1);

			const auto originalLA = base + 2;
			movedLA = base + 2 + lastIndex;
			park = base + 2 + static_cast<TWord>(body.size());

			char doSrc[32];
			snprintf(doSrc, sizeof(doSrc), "do #2,$%x", originalLA);

			std::vector<TWord> program = assembleOne(assembler, doSrc);
			verify(program.size() == 2);

			append(program, body);

			char parkSrc[32];
			snprintf(parkSrc, sizeof(parkSrc), "jmp $%x", park);
			append(program, assembleOne(assembler, parkSrc));

			writeP(_dsp, base, program);
		}
	};

	// One exec() is one JIT block or one interpreted instruction, except for the
	// interpreted DO, which runs its whole loop inside a single call. Both programs
	// are a handful of instructions, so this bound is loose; what it buys is that an
	// engine that keeps running the loop it was compiled with fails as an assertion
	// rather than as a hang.
	constexpr uint32_t g_maxSteps = 512;

	template<typename TStep> void runToPark(Fixture& _f, const TWord _base, const TWord _park, TStep&& _step)
	{
		_f.dsp.setPC(_base);

		uint32_t steps = 0;

		while(steps < g_maxSteps && _f.dsp.getPC().toWord() != _park)
		{
			_step();
			++steps;
		}

		verify(steps < g_maxSteps);
		verify(_f.dsp.getPC().toWord() == _park);
	}

	void report(const char* _engine, const Fixture& _f, const char* _name)
	{
		std::cout << _engine << " " << _name
			<< ": x:$31=$" << std::hex << _f.mem.get(MemArea_X, g_addrRecordedLA)
			<< " x:$32=$" << _f.mem.get(MemArea_X, g_addrRecordedLC)
			<< " pc=$" << _f.dsp.getPC().toWord()
			<< std::dec << std::endl;
	}

	template<typename TStep> void foreverLoopEndFollowsLA(const char* _engine, TStep&& _makeStep)
	{
		Fixture f;
		ForeverProgram prog;
		prog.write(f.dsp);

		fillHostReceive(f, prog.movedLA, g_maxSteps);

		auto step = _makeStep(f);
		runToPark(f, ForeverProgram::base, prog.park, step);

		report(_engine, f, "forever");

		// Written by an instruction that lies past the end the DO named. Its value is
		// the moved LA itself, read back out of the register from inside the loop, so
		// neither the sentinel nor the original end can produce it.
		verify(f.mem.get(MemArea_X, g_addrRecordedLA) == prog.movedLA);

		// ENDDO sits at the moved end and is the only thing that can retire a forever
		// loop, so reaching the park address at all means the loop ran to that end.
		verify((f.dsp.regs().sr.var & SR_LF) == 0);
		verify((f.dsp.regs().sr.var & SR_FV) == 0);
	}

	template<typename TStep> void countedLoopEndFollowsLA(const char* _engine, TStep&& _makeStep)
	{
		Fixture f;
		CountedProgram prog;
		prog.write(f.dsp);

		fillHostReceive(f, prog.movedLA, g_maxSteps);

		f.dsp.regs().lc.var = g_outerLC;

		auto step = _makeStep(f);
		runToPark(f, CountedProgram::base, prog.park, step);

		report(_engine, f, "counted");

		// The recorded count is taken past the end the DO named. Inside the extended
		// loop the last pass records 1; an engine that retires the loop at the end it
		// compiled runs those instructions only as fall through, with LC already
		// restored off the system stack, and records the outer count instead.
		verify(f.mem.get(MemArea_X, g_addrRecordedLC) == 1);

		verify((f.dsp.regs().sr.var & SR_LF) == 0);
		verify(f.dsp.regs().lc.toWord() == g_outerLC);
	}

	auto interpreterStep()
	{
		return [](Fixture& _f) { return [&_f]() { _f.dsp.execInterpreter(); }; };
	}

	auto jitStep()
	{
		return [](Fixture& _f) { return [&_f]() { _f.dsp.exec(); }; };
	}
}

int main()
{
	try
	{
		foreverLoopEndFollowsLA("interpreter", interpreterStep());
		countedLoopEndFollowsLA("interpreter", interpreterStep());

		if constexpr(g_useJIT)
		{
			foreverLoopEndFollowsLA("jit", jitStep());
			countedLoopEndFollowsLA("jit", jitStep());
		}
		else
		{
			std::cout << "jit: not supported on this build, skipped" << std::endl;
		}
	}
	catch(const std::string& _err)
	{
		std::cout << "dsp56k_do_loop_address_written FAILED: " << _err << std::endl;
		return -1;
	}

	std::cout << "dsp56k_do_loop_address_written passed" << std::endl;
	return 0;
}
