// Task DSP-21. Tier T0: the guest program lives in this file as literal
// words plus two assembler invocations; no firmware, kernel or .pch2 corpus
// is touched, so the check runs with NMG2_ARTIFACTS unset.
//
// WHAT IT MEASURES. A guest positioned at a DO FOREVER ($000203) whose loop
// body stores a sentinel word to X memory. The discriminating observable is
// the sentinel reading strictly above its non-zero base after N >= 1 exec()
// calls. HOW THE ENGINES DIFFER: the interpreter runs the ENTIRE loop inside
// the first exec() -- its blocking while in dsp.cpp wraps the body
// internally -- while the JIT executes ONE BLOCK per exec(), so the DO-head
// block returns after arming LA/LC/SC/LF and the body block runs on the next
// call, wrapping internally through its jumpIfLoop back-edge until LC
// exhausts. The protocol is therefore: repeat exec() until the sentinel
// moves (bounded), then assert. One PC claim IS made, the loop-exit
// contract: once the loop has completed, PC == LA + 1, where LA is the DO's
// absolute-address extension word, the address of the loop's last
// instruction.
//
// WHY THE BASE VALUE IS NON-ZERO. A default-zero read compared against a
// default-zero write proves nothing; the fixture writes the base and the
// loop body must move it.
//
// THE MIRAGE THIS TEST REFUSES. An assertion that exec() returned, or that
// dispatch reached op_DoForever, passes against the unrepaired code by
// catching the not-implemented error. This test catches nothing: against a
// stub, the first exec() aborts the process and the check goes red.

#include "dsp56kEmu/dsp.h"
#include "dsp56kEmu/memory.h"
#include "dsp56kEmu/peripherals56311.h"
#include "dsp56kEmu/unittests.h"

#include <iostream>

namespace
{
	using namespace dsp56k;

	DefaultMemoryValidator g_memoryValidator;

	constexpr TWord g_sentinelAddress = 0x3f;	// X:<aa> short absolute, 6-bit range
	constexpr TWord g_sentinelBase = 0x111111;
	constexpr TWord g_sentinelValue = 0x222222;

	constexpr TWord g_programBase = 0x100;

	// DO FOREVER, pinned literals and not assembler output: the opcode word
	// is opcodeinfo.h's DoForever pattern; the extension word is the body
	// address, computed below.
	constexpr TWord g_opDoForever = 0x000203;

	void theLoopRunsAndExitsToLAPlusOne()
	{
		Peripherals56311 p(96000);
		Memory mem(g_memoryValidator, 0x080000, 0x800000, 0x200000);
		DSP dsp(mem, &p, &p.ySpace());

		Assembler assembler;
		// The sentinel travels through x0, a plain 24-bit register: a long
		// immediate loaded into the accumulator "a" lands in A1, and a store
		// of a0 would write zero.
		const auto load = assembler.assemble("move #$222222,x0");
		if(!load.success())
			std::cout << "sentinel-load assemble error " << static_cast<int>(load.error) << std::endl;
		verify(load.success());
		verify(load.wordCount == 2);

		const auto store = assembler.assemble("move x0,x:$3f");
		if(!store.success())
			std::cout << "sentinel-store assemble error " << static_cast<int>(store.error) << std::endl;
		verify(store.success());
		verify(store.wordCount == 1);

		const TWord bodyAddr = g_programBase + load.wordCount + 2;

		// Program words go through memWriteP, not Memory::set: the write path
		// notifies the JIT, which sizes its entry table for the written
		// range. A bare memory write leaves m_jitEntries unsized and the
		// first exec() jumps through a null pointer.
		for(uint32_t i = 0; i < load.wordCount; ++i)
			dsp.memWriteP(g_programBase + i, load.word[i]);
		dsp.memWriteP(g_programBase + load.wordCount, g_opDoForever);
		dsp.memWriteP(g_programBase + load.wordCount + 1, bodyAddr);
		dsp.memWriteP(bodyAddr, store.word[0]);

		mem.set(MemArea_X, g_sentinelAddress, g_sentinelBase);

		// Mask interrupts (SR.I1 = SR.I0 = 1). The 56311 ESAI raises its
		// transmit-data-empty condition from construction; an unmasked
		// interrupt would hijack the guest to a vector this fixture never
		// populated.
		// Mask interrupts. DSP's setSR is private; getSR is public and
		// returns a const ref to the live register, so mutate through the
		// const_cast of that reference rather than subclassing a friend.
		const_cast<TReg24&>(dsp.getSR()).var |= (SR_I0 | SR_I1);

		dsp.setPC(g_programBase);

		// The interpreter finishes the loop inside the first exec(); the JIT
		// needs a second call to enter the body block. Bound the loop so a
		// regression cannot hang the suite.
		constexpr uint32_t g_maxExecCalls = 16;
		uint32_t execCalls = 0;

		TWord sentinelAfter = mem.get(MemArea_X, g_sentinelAddress);

		while(sentinelAfter != g_sentinelValue && execCalls < g_maxExecCalls)
		{
			dsp.exec();
			++execCalls;
			sentinelAfter = mem.get(MemArea_X, g_sentinelAddress);
		}

		if(sentinelAfter != g_sentinelValue)
		{
			const auto& r = dsp.regs();
			std::cout << "sentinel after: $" << std::hex << sentinelAfter << ", pc after: $" << r.pc.toWord()
				<< ", la: $" << r.la.toWord() << ", lc: $" << r.lc.toWord() << ", sc: $" << static_cast<TWord>(r.sc.var)
				<< " after " << std::dec << execCalls << " exec calls" << std::endl;
		}

		verify(sentinelAfter == g_sentinelValue);
		verify(dsp.getPC().toWord() == bodyAddr + 1);
	}
}

int main()
{
	try
	{
		theLoopRunsAndExitsToLAPlusOne();
	}
	catch(const std::string& _err)
	{
		std::cout << "dsp56k_do_forever FAILED: " << _err << std::endl;
		return -1;
	}

	std::cout << "dsp56k_do_forever passed" << std::endl;
	return 0;
}
