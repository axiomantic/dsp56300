// Tier T0: the guest program lives in this file as assembler invocations; no
// firmware, kernel or .pch2 corpus is touched, so the check runs with
// NMG2_ARTIFACTS unset.
//
// WHAT IT MEASURES. That a data-memory read from an address above the top of
// the configured memory yields the SAME word under the interpreter and under
// the JIT, and that it does not read the contents of a valid address.
//
// WHY ZERO. The DSP56300 Family Manual rev. 5 does not define the data returned
// by such a read. What it does define is that the address does not fold back
// into internal RAM: Figure 11-1 (§11.1, p. 11-2) gives exactly one Internal
// region per data space, at the bottom, and §11.1.3.2 / §11.1.4.4 (pp. 11-5,
// 11-6) place External above it up to the reserved range. So an aliased word is
// excluded as a model of the hardware, while a fixed value is merely
// unspecified. Zero is the value the interpreter has always produced, so
// choosing it changes one engine rather than two.
//
// THE MIRAGE THIS TEST REFUSES. Asserting only that the two engines agree
// passes against an implementation in which both alias the same in-range word.
// The check therefore pins the value AND pins the in-range word the aliasing
// would have exposed: g_addrAlias is seeded with a marker that must not appear
// in the result, and must itself survive the out-of-range write.
//
// BOTH ENGINES ARE DRIVEN. g_useJIT is a compile-time constant, so DSP::exec()
// reaches only one of the two engines on any given build. The interpreter is
// therefore driven through the public DSP::execInterpreter() directly, and the
// JIT through DSP::exec() guarded by g_useJIT.

#include "dsp56kEmu/dsp.h"
#include "dsp56kEmu/memory.h"
#include "dsp56kEmu/peripherals56311.h"
#include "dsp56kEmu/unittests.h"

#include <iostream>

namespace
{
	using namespace dsp56k;

	DefaultMemoryValidator g_memoryValidator;

	constexpr TWord g_memSizeP = 0x080000;
	constexpr TWord g_memSizeXY = 0x800000;
	constexpr TWord g_bridgedAddress = 0x200000;

	// Above the top of XY memory, and chosen so that masking with sizeXY-1 lands
	// inside internal (non-bridged) XY memory, where the alias marker lives.
	constexpr TWord g_addrOutOfRange = 0x8f0000;
	constexpr TWord g_addrAlias = g_addrOutOfRange & (g_memSizeXY - 1);

	static_assert(g_addrOutOfRange >= g_memSizeXY, "the address under test must be out of range");
	static_assert(g_addrAlias < g_bridgedAddress, "the aliased address must be inside internal XY memory");

	constexpr TWord g_markerAlias = 0x555555;
	constexpr TWord g_markerOutOfRange = 0xaaaaaa;

	// X:<aa> short absolute is a 6-bit field, so the observable lives below $40.
	constexpr TWord g_addrRecorded = 0x3e;

	struct Fixture
	{
		Peripherals56311 p{96000};
		Memory mem{g_memoryValidator, g_memSizeP, g_memSizeXY, g_bridgedAddress};
		DSP dsp{mem, &p, &p.ySpace()};

		Fixture()
		{
			// Mask interrupts (SR.I1 = SR.I0 = 1). The 56311 ESAI raises its
			// transmit-data-empty condition from construction; an unmasked interrupt
			// would hijack the guest to a vector this fixture never populated.
			dsp.regs().sr.var |= (SR_I0 | SR_I1);
		}
	};

	// Program words go through memWriteP, not Memory::set: the write path notifies
	// the JIT, which sizes its entry table for the written range.
	void writeP(DSP& _dsp, const TWord _addr, const std::vector<TWord>& _words)
	{
		for(uint32_t i = 0; i < _words.size(); ++i)
			_dsp.memWriteP(_addr + i, _words[i]);
	}

	// The guest program. Two markers are planted first so that neither candidate
	// wrong answer can be mistaken for the right one: g_markerAlias occupies the
	// address that masking would fold onto, and g_markerOutOfRange is written
	// through the out-of-range address itself, which populates whatever scratch
	// storage the write path may reach.
	//
	//   move #>$0f0000,r1
	//   move #>$555555,x0
	//   move x0,x:(r1)
	//   move #>$8f0000,r0
	//   move #>$aaaaaa,x0
	//   move x0,x:(r0)
	//   move x:(r0),x0
	//   move x0,x:$3e
	//   nop                  <- halt lands here
	struct Program
	{
		static constexpr TWord base = 0x100;

		TWord end = 0;

		void write(DSP& _dsp)
		{
			Assembler assembler;
			std::vector<TWord> program;

			auto emit = [&](const std::string& _src)
			{
				const auto r = assembler.assemble(_src.c_str());

				if(!r.success())
					std::cout << "assemble error " << static_cast<int>(r.error) << " for: " << _src << std::endl;

				verify(r.success());

				for(uint32_t i = 0; i < r.wordCount; ++i)
					program.push_back(r.word[i]);
			};

			auto hex = [](const TWord _v)
			{
				char buf[16];
				snprintf(buf, sizeof(buf), "$%06x", _v);
				return std::string(buf);
			};

			emit("move #>" + hex(g_addrAlias) + ",r1");
			emit("move #>" + hex(g_markerAlias) + ",x0");
			emit("move x0,x:(r1)");
			emit("move #>" + hex(g_addrOutOfRange) + ",r0");
			emit("move #>" + hex(g_markerOutOfRange) + ",x0");
			emit("move x0,x:(r0)");
			emit("move x:(r0),x0");
			emit("move x0,x:" + hex(g_addrRecorded));
			emit("nop");

			end = base + static_cast<TWord>(program.size()) - 1;

			writeP(_dsp, base, program);
		}
	};

	TWord runInterpreter()
	{
		Fixture f;
		Program prog;
		prog.write(f.dsp);

		f.mem.set(MemArea_X, g_addrRecorded, 0xffffff);
		f.dsp.setPC(Program::base);

		while(f.dsp.getPC().toWord() < prog.end)
			f.dsp.execInterpreter();

		const auto recorded = f.mem.get(MemArea_X, g_addrRecorded);

		std::cout << "interpreter: x:$" << std::hex << g_addrOutOfRange << " read as $" << recorded
			<< ", x:$" << g_addrAlias << " holds $" << f.mem.get(MemArea_X, g_addrAlias)
			<< std::dec << std::endl;

		// The out-of-range write must not have disturbed the address that masking
		// would have folded onto.
		verify(f.mem.get(MemArea_X, g_addrAlias) == g_markerAlias);

		return recorded;
	}

	TWord runJit(bool& _ran)
	{
		_ran = false;

		if constexpr(!g_useJIT)
		{
			std::cout << "jit: not supported on this build, skipped" << std::endl;
			return 0;
		}
		else
		{
			Fixture f;
			Program prog;
			prog.write(f.dsp);

			f.mem.set(MemArea_X, g_addrRecorded, 0xffffff);
			f.dsp.setPC(Program::base);

			// One JIT block per exec(). The program is straight-line, so a handful of
			// calls covers it; the bound turns a regression that fails to advance into
			// a red rather than a hang.
			constexpr uint32_t maxExecCalls = 64;
			uint32_t execCalls = 0;

			while(execCalls < maxExecCalls && f.dsp.getPC().toWord() < prog.end)
			{
				f.dsp.exec();
				++execCalls;
			}

			verify(execCalls < maxExecCalls);

			const auto recorded = f.mem.get(MemArea_X, g_addrRecorded);

			std::cout << "jit: x:$" << std::hex << g_addrOutOfRange << " read as $" << recorded
				<< ", x:$" << g_addrAlias << " holds $" << f.mem.get(MemArea_X, g_addrAlias)
				<< std::dec << std::endl;

			verify(f.mem.get(MemArea_X, g_addrAlias) == g_markerAlias);

			_ran = true;
			return recorded;
		}
	}

	void bothEnginesReadZeroOutOfRange()
	{
		const auto interpreted = runInterpreter();

		bool jitRan = false;
		const auto jitted = runJit(jitRan);

		verify(interpreted == 0);

		if(!jitRan)
			return;

		// Not the in-range word the address would fold onto, and not the word the
		// out-of-range write carried.
		verify(jitted != g_markerAlias);
		verify(jitted != g_markerOutOfRange);
		verify(jitted == 0);
		verify(jitted == interpreted);
	}
}

int main()
{
	try
	{
		bothEnginesReadZeroOutOfRange();
	}
	catch(const std::string& _err)
	{
		std::cout << "dsp56k_mem_out_of_range_read FAILED: " << _err << std::endl;
		return -1;
	}

	std::cout << "dsp56k_mem_out_of_range_read passed" << std::endl;
	return 0;
}
