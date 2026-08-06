// DSP-5 - the fifth PeripheralType enumerator.
//
// The enumeration must hold FIVE distinct values, and the new one must equal
// none of the four that precede it. A value added as a duplicate is a compile
// error here. A build of dsp56kEmu would accept a duplicate silently, because
// duplicate enumerator values are legal C++ and this enumeration already uses
// them elsewhere.

#include "dsp56kEmu/unittests.h"

#include <array>
#include <cstddef>
#include <iostream>

namespace
{
	using dsp56k::PeripheralType;

	constexpr std::array<PeripheralType, 5> g_allTypes
	{
		PeripheralType::PeripheralsNop,
		PeripheralType::Peripherals56303,
		PeripheralType::Peripherals56362,
		PeripheralType::Peripherals56367,
		PeripheralType::Peripherals56311
	};

	constexpr size_t countDistinct()
	{
		size_t distinct = 0;

		for(size_t i = 0; i < g_allTypes.size(); ++i)
		{
			bool seenBefore = false;

			for(size_t j = 0; j < i; ++j)
			{
				if(g_allTypes[j] == g_allTypes[i])
					seenBefore = true;
			}

			if(!seenBefore)
				++distinct;
		}

		return distinct;
	}

	static_assert(g_allTypes.size() == 5, "PeripheralType must name five peripheral sets: the four upstream ones and Peripherals56311");
	static_assert(countDistinct() == 5, "PeripheralType holds a duplicate value: the five enumerators are not five distinct values");

	// The new enumerator equals none of the four that precede it.
	static_assert(PeripheralType::Peripherals56311 != PeripheralType::PeripheralsNop, "Peripherals56311 duplicates PeripheralsNop");
	static_assert(PeripheralType::Peripherals56311 != PeripheralType::Peripherals56303, "Peripherals56311 duplicates Peripherals56303");
	static_assert(PeripheralType::Peripherals56311 != PeripheralType::Peripherals56362, "Peripherals56311 duplicates Peripherals56362");
	static_assert(PeripheralType::Peripherals56311 != PeripheralType::Peripherals56367, "Peripherals56311 duplicates Peripherals56367");

	// Every upstream set reports the enumerator that names it. A set constructed
	// with the wrong enumerator sends dma.cpp's dispatch down the wrong branch.
	void existingSetsReportTheirOwnType()
	{
		dsp56k::PeripheralsNop nop;
		dsp56k::Peripherals56303 p303;
		dsp56k::Peripherals56362 p362;
		dsp56k::Peripherals56367 p367;

		verify(nop.getType() == PeripheralType::PeripheralsNop);
		verify(p303.getType() == PeripheralType::Peripherals56303);
		verify(p362.getType() == PeripheralType::Peripherals56362);
		verify(p367.getType() == PeripheralType::Peripherals56367);
	}
}

int main()
{
	try
	{
		existingSetsReportTheirOwnType();
	}
	catch(const std::string& _err)
	{
		std::cout << "dsp56k_peripheral_type FAILED: " << _err << std::endl;
		return -1;
	}

	std::cout << "dsp56k_peripheral_type passed" << std::endl;
	return 0;
}
