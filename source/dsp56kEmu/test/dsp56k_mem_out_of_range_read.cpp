// Test that out-of-range data memory reads yield zero in both interpreter and JIT.

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
	constexpr TWord g_markerAliasY = 0x333333;
	constexpr TWord g_markerOutOfRange = 0xaaaaaa;
	constexpr TWord g_markerOutOfRangeY = 0xcccccc;

	// X:<aa> short absolute is a 6-bit field, so the observables live below $40.
	constexpr TWord g_addrRecorded = 0x3e;
	constexpr TWord g_addrRecordedLongX = 0x3d;
	constexpr TWord g_addrRecordedLongY = 0x3c;

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

	// The three words the guest records: the single-area read, and the two halves
	// of the L: read that takes the parallel path.
	struct Observed
	{
		TWord single = 0;
		TWord longX = 0;
		TWord longY = 0;

		bool operator == (const Observed& _o) const
		{
			return single == _o.single && longX == _o.longX && longY == _o.longY;
		}
	};

	Observed report(const Fixture& _f, const char* _engine)
	{
		Observed o;
		o.single = _f.mem.get(MemArea_X, g_addrRecorded);
		o.longX = _f.mem.get(MemArea_X, g_addrRecordedLongX);
		o.longY = _f.mem.get(MemArea_X, g_addrRecordedLongY);

		std::cout << _engine
			<< ": x:$" << std::hex << g_addrOutOfRange << " read as $" << o.single
			<< ", l:$" << g_addrOutOfRange << " read as $" << o.longX << ":$" << o.longY
			<< ", alias x:$" << g_addrAlias << " holds $" << _f.mem.get(MemArea_X, g_addrAlias)
			<< ", alias y:$" << g_addrAlias << " holds $" << _f.mem.get(MemArea_Y, g_addrAlias)
			<< std::dec << std::endl;

		// The out-of-range writes must not have disturbed the addresses that masking
		// would have folded onto.
		verify(_f.mem.get(MemArea_X, g_addrAlias) == g_markerAlias);
		verify(_f.mem.get(MemArea_Y, g_addrAlias) == g_markerAliasY);

		return o;
	}

	// Program words go through memWriteP, not Memory::set: the write path notifies
	// the JIT, which sizes its entry table for the written range.
	void writeP(DSP& _dsp, const TWord _addr, const std::vector<TWord>& _words)
	{
		for(uint32_t i = 0; i < _words.size(); ++i)
			_dsp.memWriteP(_addr + i, _words[i]);
	}

	// Assemble guest test program exercising single X: and parallel L: out-of-range reads:
	//   move #>g_addrAlias,r1; move #>g_markerAlias,x0; move x0,x:(r1)
	//   move #>g_addrOutOfRange,r0; move #>g_markerOutOfRange,x0; move x0,x:(r0)
	//   move x:(r0),x0; move x0,x:$3e
	//   ... parallel L: read ...
	//   nop
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

			// L: addresses X and Y from one offset, which is the parallel read path.
			// Its two halves land in x1 (X) and x0 (Y).
			emit("move #>" + hex(g_markerAliasY) + ",y0");
			emit("move y0,y:(r1)");
			emit("move #>" + hex(g_markerOutOfRangeY) + ",y0");
			emit("move #>" + hex(g_markerOutOfRange) + ",x0");
			emit("move x0,x:(r0)");
			emit("move y0,y:(r0)");
			emit("move l:(r0),x");
			emit("move x1,x:" + hex(g_addrRecordedLongX));
			emit("move x0,x:" + hex(g_addrRecordedLongY));

			emit("nop");

			end = base + static_cast<TWord>(program.size()) - 1;

			writeP(_dsp, base, program);
		}
	};

	void seedObservables(Fixture& _f)
	{
		_f.mem.set(MemArea_X, g_addrRecorded, 0xffffff);
		_f.mem.set(MemArea_X, g_addrRecordedLongX, 0xffffff);
		_f.mem.set(MemArea_X, g_addrRecordedLongY, 0xffffff);
	}

	Observed runInterpreter()
	{
		Fixture f;
		Program prog;
		prog.write(f.dsp);

		seedObservables(f);
		f.dsp.setPC(Program::base);

		while(f.dsp.getPC().toWord() < prog.end)
			f.dsp.execInterpreter();

		return report(f, "interpreter");
	}

	Observed runJit(bool& _ran)
	{
		_ran = false;

		if constexpr(!g_useJIT)
		{
			std::cout << "jit: not supported on this build, skipped" << std::endl;
			return {};
		}
		else
		{
			Fixture f;
			Program prog;
			prog.write(f.dsp);

			seedObservables(f);
			f.dsp.setPC(Program::base);

			constexpr uint32_t maxExecCalls = 64;
			uint32_t execCalls = 0;

			while(execCalls < maxExecCalls && f.dsp.getPC().toWord() < prog.end)
			{
				f.dsp.exec();
				++execCalls;
			}

			verify(execCalls < maxExecCalls);

			_ran = true;
			return report(f, "jit");
		}
	}

	void bothEnginesReadZeroOutOfRange()
	{
		const auto interpreted = runInterpreter();

		bool jitRan = false;
		const auto jitted = runJit(jitRan);

		verify(interpreted.single == 0);
		verify(interpreted.longX == 0);
		verify(interpreted.longY == 0);

		if(!jitRan)
			return;

		verify(jitted.single != g_markerAlias && jitted.single != g_markerOutOfRange);
		verify(jitted.longX != g_markerAlias && jitted.longX != g_markerOutOfRange);
		verify(jitted.longY != g_markerAliasY && jitted.longY != g_markerOutOfRangeY);

		verify(jitted.single == 0);
		verify(jitted.longX == 0);
		verify(jitted.longY == 0);

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
