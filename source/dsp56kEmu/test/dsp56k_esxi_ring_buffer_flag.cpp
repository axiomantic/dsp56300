// Esxi forwards its construction flag to Audio.
//
// Esxi's own constructor is the only place the flag crosses from the peripheral
// into Audio. A body that dropped the parameter and called Audio() would still
// compile, because Audio's parameter carries a default, and the peripheral would
// then always own ring buffers no matter what its caller asked for.

#include "dsp56kEmu/esxi.h"
#include "dsp56kEmu/unittests.h"

#include <iostream>

namespace
{
	using namespace dsp56k;

	class TestEsxi final : public Esxi
	{
	public:
		using Esxi::Esxi;

		void execTX() override {}
		void execRX() override {}

		TWord hasEnabledTransmitters() const override { return 0; }
		TWord hasEnabledReceivers() const override { return 0; }
	};

	void theFlagReachesAudioInBothPositions()
	{
		const TestEsxi without(false);
		verify(!without.hasRingBuffers());

		const TestEsxi with(true);
		verify(with.hasRingBuffers());

		const TestEsxi defaulted;
		verify(defaulted.hasRingBuffers());
	}
}

int main()
{
	try
	{
		theFlagReachesAudioInBothPositions();
	}
	catch(const std::string& _err)
	{
		std::cout << "dsp56k_esxi_ring_buffer_flag FAILED: " << _err << std::endl;
		return -1;
	}

	std::cout << "dsp56k_esxi_ring_buffer_flag passed" << std::endl;
	return 0;
}
