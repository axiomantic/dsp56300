// Tier T0: the guest program lives in this file as assembler invocations; no
// firmware, kernel or .pch2 corpus is touched, so the check runs with
// NMG2_ARTIFACTS unset.
//
// WHAT IT MEASURES. A JIT block ends at an address it does not itself cover, and
// hands control back there. DSP::execJit then indexes the chain's entry table at
// that address with no bounds check, so the table has to reach it.
//
// The table grows in two places only: a P write whose value differed from what
// was already in memory, and a block being occupied. Neither covers an address
// holding an opcode word of zero over already-zero P memory -- and nop is
// $000000. A block that ends immediately in front of such an address therefore
// used to return into a read past the end of the table.
//
// WHY THIS IS LOOP-FREE. The shape has nothing to do with loops or with BRKcc.
// Any block terminator that leaves the following address to a separate block
// reaches it; a write to LA is one of them and needs no loop, no branch and no
// stack. The check is written that way so that a regression here reads as what it
// is rather than as a loop defect.
//
// HOW IT FAILS. The read is out of bounds, so an instrumented build reports it as
// a heap buffer overflow and an ordinary build takes the value it finds -- a null
// entry, on the machine this was written on, which is a jump to address zero. The
// process dies either way. It does not reach the post-conditions below.

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

	constexpr TWord g_base = 0x100;

	// nop assembles to $000000, which is also what P memory already holds, so
	// memWriteP sees no change and never notifies the JIT for the address. That is
	// the whole point of the case; the control below writes something else there.
	constexpr const char* g_zeroFiller = "nop";
	constexpr const char* g_nonZeroFiller = "move #1,x1";

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

	// A write to LA ends a block. What follows it is the filler, which the caller
	// chooses:
	//
	//   $100  move #$5,la     <- terminates the block
	//   $101  <filler>        <- the successor address, in its own block
	//
	// Program words go through memWriteP, not Memory::set: the write path is what
	// notifies the JIT, and what it does and does not notify for is the subject.
	void aBlockReturnsIntoAnEntryThatExists(const char* _filler, const char* _case)
	{
		if constexpr(!g_useJIT)
		{
			std::cout << _case << ": jit not supported on this build, skipped" << std::endl;
			return;
		}
		else
		{
			Fixture f;

			Assembler assembler;
			TWord w[8];

			std::vector<TWord> program;

			const auto terminatorLen = assembleOne(assembler, "move #$5,la", w);
			for(uint32_t i = 0; i < terminatorLen; ++i)
				program.push_back(w[i]);

			const auto successor = g_base + terminatorLen;

			verify(assembleOne(assembler, _filler, w) == 1);
			for(uint32_t i = 0; i < 4; ++i)
				program.push_back(w[0]);

			for(uint32_t i = 0; i < program.size(); ++i)
				f.dsp.memWriteP(g_base + i, program[i]);

			f.dsp.setPC(g_base);

			f.dsp.exec();

			std::cout << _case << ": after the terminating block, pc=$" << std::hex
				<< f.dsp.getPC().toWord() << " successor=$" << successor << std::dec << std::endl;

			// The block covered the terminator and nothing else, so it left the PC on the
			// address it does not cover.
			verify(f.dsp.getPC().toWord() == successor);

			// LA carries the value the terminator wrote, so the block ran rather than
			// merely being counted.
			verify(f.dsp.regs().la.toWord() == 0x5);

			// The read of the entry for that address happens here. Reaching the line after
			// it is the measurement.
			f.dsp.exec();

			std::cout << _case << ": returned from the successor block, pc=$" << std::hex
				<< f.dsp.getPC().toWord() << std::dec << std::endl;

			// The successor block ran forward from the address the first block left.
			verify(f.dsp.getPC().toWord() > successor);
		}
	}
}

int main()
{
	try
	{
		// The control runs the same shape with an address the write path did notify
		// for. It separates "the entry table did not reach the address" from "the
		// emulator cannot run this program at all".
		aBlockReturnsIntoAnEntryThatExists(g_nonZeroFiller, "notified successor");
		aBlockReturnsIntoAnEntryThatExists(g_zeroFiller, "unnotified successor");
	}
	catch(const std::string& _err)
	{
		std::cout << "dsp56k_jit_block_successor_entry FAILED: " << _err << std::endl;
		return -1;
	}
	catch(const std::exception& _err)
	{
		std::cout << "dsp56k_jit_block_successor_entry FAILED: " << _err.what() << std::endl;
		return -1;
	}

	std::cout << "dsp56k_jit_block_successor_entry passed" << std::endl;
	return 0;
}
