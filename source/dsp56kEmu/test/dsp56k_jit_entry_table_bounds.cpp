// Tier T0: the guest program lives in this file as assembler invocations; no
// firmware, kernel or .pch2 corpus is touched, so the check runs with
// NMG2_ARTIFACTS unset.
//
// WHAT IT MEASURES. The JIT entry table is indexed by the guest PC. The PC is a
// value the guest program computes -- a jmp through a register, an rts to an
// address it pushed, an interrupt vector it installed -- and nothing about it is
// bounded by how large the table is. The table is sized to P memory; the PC is a
// 24 bit quantity, and P memory here is $080000 words. So the index can be larger
// than the table by any amount up to sixteen million, and the read used to be
// unconditional.
//
// Sizing the table cannot close that. The address the guest can name is larger
// than the address space the emulator has memory for, at any table size the
// emulator can afford. What closes it is that every read is bounded, and that an
// index the table does not have resolves to the same entry an index it does have
// holds until a block is made there -- funcCreate. The out of range case then has
// no behaviour of its own to get wrong: it takes the create path, that path grows
// the table where growing is possible, and it is the single place that decides
// what a PC outside emulated P memory means.
//
// WHAT EACH CASE EXERCISES, BY PLATFORM. This matters here more than usual.
//
//   theReadIsBoundedAtEveryIndex -- every platform. The indices it reads are past
//   the end of the table however the table was allocated, because they are past
//   the end of P memory.
//
//   creatingOutsidePMemoryIsRefused -- every platform, same reason.
//
//   theExecLoopRunsAnUnnotifiedAddress -- the generated exec loop, on every
//   platform, but it is only the OUT OF RANGE case where MmuArray fell back to a
//   grown vector: that is macOS and Android always, and anywhere else only when
//   the MMU reservation fails. Where the MMU path is taken the table already
//   covers all of P from initialisation, the index is in range, and the case
//   degenerates into a control that shows the loop still works. It prints which
//   of the two it was so that a green run says which one it measured.
//
// HOW IT FAILS. Without the bound the read is out of bounds, so an instrumented
// build reports a heap buffer overflow and an ordinary build calls through
// whatever it found. The process dies either way and does not reach the
// post-conditions below.

#include "dsp56kEmu/dsp.h"
#include "dsp56kEmu/jit.h"
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

	// Inside P memory, and never written, so nothing ever notified the JIT for it.
	constexpr TWord g_unnotifiedTarget = 0x400;

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

	void writeProgram(Fixture& _f, const std::vector<TWord>& _program)
	{
		// Program words go through memWriteP, not Memory::set: the write path is what
		// notifies the JIT, and what it does and does not notify for is the subject.
		for(uint32_t i = 0; i < _program.size(); ++i)
			_f.dsp.memWriteP(g_base + i, _program[i]);
	}

	// The read itself. Not the effect of the read -- the read.
	void theReadIsBoundedAtEveryIndex()
	{
		Fixture f;

		const auto pSize = f.mem.sizeP();

		// $ff0000 is the 56300's bootstrap P region and $ffffff is the largest address
		// the PC can hold. Both are addresses a guest can compute and neither has any
		// entry in a table sized to $080000 words.
		const TWord outOfRange[] = { pSize, pSize + 1, 0xff0000, 0xffffff };

		for(const auto pc : outOfRange)
		{
			const auto entry = f.dsp.jitEntry(pc);

			std::cout << "entry for pc=$" << std::hex << pc << " is "
				<< (entry == &funcCreate ? "the create entry" : "something else") << std::dec << std::endl;

			// Reaching this line at all is the measurement: an unbounded read of the
			// table at these indices is out of bounds by up to 128 MB.
			verify(entry == &funcCreate);
		}

		// An index the table does have still answers with what the table holds, so the
		// bound did not swallow the ordinary case.
		verify(f.dsp.jitEntry(g_base) == &funcCreate);
	}

	// Every out of range read arrives at the create path, so the create path is the one
	// place that has to refuse a PC that P memory does not have. It is called here
	// directly rather than by jumping to one: the refusal ends the run, and a guest jump
	// would have to unwind through generated frames to be observed.
	void creatingOutsidePMemoryIsRefused()
	{
		if constexpr(!g_useJIT)
		{
			std::cout << "creatingOutsidePMemoryIsRefused: jit not supported on this build, skipped" << std::endl;
			return;
		}
		else
		{
			Fixture f;

			bool refused = false;

			try
			{
				f.dsp.getJit().create(0xff0000, false);
			}
			catch(const std::string& _err)
			{
				refused = true;
				std::cout << "refused: " << _err << std::endl;
			}
			catch(const std::exception& _err)
			{
				refused = true;
				std::cout << "refused: " << _err.what() << std::endl;
			}

			verify(refused);
		}
	}

	// The block emitter is the other reader. While it generates a block it asks for a
	// child block at the address the block will hand control to, and for a block at the
	// top of P memory that address is one past the end of P -- an index the cache does not
	// have on any platform, and one that cannot be created by growing on the MMU path
	// because the reservation is fixed at P memory.
	//
	// Emitted, not executed: the read under test happens while the block is being made,
	// and running it would end at the refusal instead.
	void emittingAtTheTopOfPMemory()
	{
		if constexpr(!g_useJIT)
		{
			std::cout << "emittingAtTheTopOfPMemory: jit not supported on this build, skipped" << std::endl;
			return;
		}
		else
		{
			Fixture f;

			Assembler assembler;
			TWord w[8];

			// A conditional jump ends the block and leaves TWO successors: the target, and
			// the address after it. Placed at the last address in P, the second of those is
			// one past the end of P, and the emitter asks for a child block at it.
			const auto len = assembleOne(assembler, "jcs $100", w);
			verify(len == 1);

			const auto last = f.mem.sizeP() - 1;
			f.dsp.memWriteP(last, w[0]);

			f.dsp.getJit().create(last, false);

			std::cout << "emitted the block at $" << std::hex << last
				<< ", whose successor is $" << f.mem.sizeP() << std::dec << std::endl;
		}
	}

	// The generated exec loop reads the table too, from its own emitter, and a bound in
	// the C++ readers says nothing about it.
	//
	// The guest is built so that the loop reaches $400 without anything having grown the
	// table to it. Two things are load bearing. The jump is computed, through r0, so the
	// block emitter cannot see the target and cannot link a child block to it -- a static
	// jmp would create the block at emit time and grow the table before the loop ever
	// read it. And the code at $400 is placed with Memory::set rather than DSP::memWriteP,
	// because memWriteP is the path that notifies the JIT, and a notification is exactly
	// what must not happen for this address.
	void theExecLoopRunsAnUnnotifiedAddress()
	{
		if constexpr(!g_useJIT)
		{
			std::cout << "theExecLoopRunsAnUnnotifiedAddress: jit not supported on this build, skipped" << std::endl;
			return;
		}
		else
		{
			Fixture f;

			Assembler assembler;
			TWord w[8];

			std::vector<TWord> program;
			const auto add = [&](const char* _source)
			{
				const auto n = assembleOne(assembler, _source, w);
				for(uint32_t i = 0; i < n; ++i)
					program.push_back(w[i]);
			};

			add("move #$400,r0");
			add("jmp (r0)");
			writeProgram(f, program);

			// Not through memWriteP: see above.
			std::vector<TWord> farBlock;
			{
				const auto n = assembleOne(assembler, "move #>$1234,la", w);
				for(uint32_t i = 0; i < n; ++i)
					farBlock.push_back(w[i]);
				const auto m = assembleOne(assembler, "jmp $100", w);
				for(uint32_t i = 0; i < m; ++i)
					farBlock.push_back(w[i]);
			}
			for(uint32_t i = 0; i < farBlock.size(); ++i)
				f.mem.set(MemArea_P, g_unnotifiedTarget + i, farBlock[i]);

			// The table was grown by the writes at $100 and by nothing else, so whether
			// $400 is inside it depends on how MmuArray allocated.
			const auto tableSize = f.dsp.getJitEntriesSize();
			const auto wasOutOfRange = g_unnotifiedTarget >= tableSize;

			std::cout << "entry table holds " << std::dec << tableSize << " entries, target $"
				<< std::hex << g_unnotifiedTarget << std::dec
				<< (wasOutOfRange ? " is OUT OF RANGE (the grown-vector fallback)"
				                  : " is in range (the MMU reservation covers all of P)") << std::endl;

			f.dsp.setPC(g_base);

			// UnrollSize iterations of the generated loop. The second of them reads the
			// entry table at $400.
			f.dsp.getJit().getTrampoline().exec(&f.dsp, JitTrampoline::UnrollSize);

			std::cout << "the loop ran, la=$" << std::hex << f.dsp.regs().la.toWord()
				<< " pc=$" << f.dsp.getPC().toWord() << std::dec << std::endl;

			// Only the block at $400 writes LA, so this is the block having run rather
			// than the loop merely having survived.
			verify(f.dsp.regs().la.toWord() == 0x1234);
		}
	}
}

int main()
{
	try
	{
		theReadIsBoundedAtEveryIndex();
		emittingAtTheTopOfPMemory();
		creatingOutsidePMemoryIsRefused();
		theExecLoopRunsAnUnnotifiedAddress();
	}
	catch(const std::string& _err)
	{
		std::cout << "dsp56k_jit_entry_table_bounds FAILED: " << _err << std::endl;
		return -1;
	}
	catch(const std::exception& _err)
	{
		std::cout << "dsp56k_jit_entry_table_bounds FAILED: " << _err.what() << std::endl;
		return -1;
	}

	std::cout << "dsp56k_jit_entry_table_bounds passed" << std::endl;
	return 0;
}
