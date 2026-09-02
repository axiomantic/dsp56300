// The findExecPeripheralsFunc line for the 56311 pair.
//
// DSP::findExecPeripheralsFunc selects the peripheral-exec function by
// dynamic_cast against hard-coded type combinations, and its else is
// assert(false && "Peripherals configuration is not supported") with a null
// return. The board passes _pX = &m_periph and _pY = &m_periph.ySpace(), so the
// combination this project needs is <Peripherals56311, Peripherals56311Y>.

#include "dsp56kEmu/dsp.h"
#include "dsp56kEmu/memory.h"
#include "dsp56kEmu/peripherals56311.h"
#include "dsp56kEmu/unittests.h"

#include <iostream>

namespace dsp56k
{
	// Defined in dsp.cpp with external linkage and declared in no header. This
	// re-declaration lets the test name it without a change to dsp.h.
	DSP::TInterruptFunc findExecPeripheralsFunc(IPeripherals* _pX, IPeripherals* _pY) noexcept;
}

namespace
{
	using namespace dsp56k;

	constexpr uint32_t g_frameRate96k = 96000;

	DefaultMemoryValidator g_memoryValidator;

	void theNewPairSelectsAFunction()
	{
		Peripherals56311 p(g_frameRate96k);

		const auto func = findExecPeripheralsFunc(&p, &p.ySpace());

		verify(func != nullptr);
	}

	// The new line must be its own instantiation. A line that returned the
	// 56362 instantiation would execute Peripherals56362::exec on a
	// Peripherals56311, which is undefined behaviour and would still be
	// non-null above.
	void theNewPairSelectsItsOwnInstantiation()
	{
		Peripherals56311 p(g_frameRate96k);
		Peripherals56362 p362;
		Peripherals56303 p303;
		PeripheralsNop nopX;
		PeripheralsNop nopY;

		const auto func311 = findExecPeripheralsFunc(&p, &p.ySpace());
		const auto func362 = findExecPeripheralsFunc(&p362, &nopY);
		const auto func303 = findExecPeripheralsFunc(&p303, &nopY);
		const auto funcNop = findExecPeripheralsFunc(&nopX, &nopY);

		verify(func362 != nullptr);
		verify(func303 != nullptr);
		verify(funcNop != nullptr);

		verify(func311 != func362);
		verify(func311 != func303);
		verify(func311 != funcNop);
	}

	// The consumer. A DSP built over the pair must reach the same function.
	void aDspConstructsOverThePair()
	{
		Peripherals56311 p(g_frameRate96k);
		Memory mem(g_memoryValidator, 0x080000, 0x800000, 0x200000);
		DSP dsp(mem, &p, &p.ySpace());

		verify(p.hasDSP());
		verify(p.ySpace().hasDSP());
		verify(&p.getDSP() == &dsp);
		verify(&p.ySpace().getDSP() == &dsp);

		verify(findExecPeripheralsFunc(&p, &p.ySpace()) != nullptr);
	}
}

int main()
{
	try
	{
		theNewPairSelectsAFunction();
		theNewPairSelectsItsOwnInstantiation();
		aDspConstructsOverThePair();
	}
	catch(const std::string& _err)
	{
		std::cout << "dsp56k_find_exec_peripherals FAILED: " << _err << std::endl;
		return -1;
	}

	std::cout << "dsp56k_find_exec_peripherals passed" << std::endl;
	return 0;
}
