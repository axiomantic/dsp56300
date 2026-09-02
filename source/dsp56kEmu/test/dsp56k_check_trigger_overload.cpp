// The third checkTrigger overload.
//
// checkTrigger is NOT a member of DmaChannel. It is two free functions in an
// anonymous namespace in dma.cpp, one for Peripherals56303 and one for
// Peripherals56362, and EACH OPENS WITH return false; above an unreachable
// switch. This task adds a third overload for a Peripherals56311.
//
// The overload cannot be named from here, so the test observes the answer
// through the only thing the answer changes: DmaChannel::arm calls
// triggerByRequest when checkTrigger returns true, and a word-trigger channel
// then moves one word. The negative case runs the SAME condition on a
// Peripherals56362, where the upstream return false; must keep the word where
// it was. Without that half the test would accept any answer.

#include "dsp56kEmu/dsp.h"
#include "dsp56kEmu/memory.h"
#include "dsp56kEmu/peripherals56311.h"
#include "dsp56kEmu/unittests.h"

#include <iostream>

namespace
{
	using namespace dsp56k;
	using RequestSource = DmaChannel::RequestSource;

	constexpr uint32_t g_frameRate96k = 96000;

	constexpr TWord g_channel = 2;
	constexpr TWord g_source = 0x001000;
	constexpr TWord g_destination = 0x002000;
	constexpr TWord g_payload = 0x123456;

	DefaultMemoryValidator g_memoryValidator;

	// Source: single counter A, post increment (5). Destination: single counter
	// A, no update (4). Transfer mode: word, trigger = request (5). DE set.
	// Both spaces are X.
	constexpr TWord dcrForRequestSource(const TWord _hardwareRequestSource)
	{
		return (1u << DmaChannel::De)
			| (static_cast<TWord>(DmaChannel::TransferMode::WordTriggerRequest) << 19)
			| (_hardwareRequestSource << 11)
			| (static_cast<TWord>(DmaChannel::AddressGenMode::SingleCounterAnoUpdate) << 7)
			| (static_cast<TWord>(DmaChannel::AddressGenMode::SingleCounterApostInc) << 4);
	}

	void armWordChannel(Dma& _dma, Memory& _mem, const TWord _hardwareRequestSource)
	{
		_mem.set(MemArea_X, g_source, g_payload);
		_mem.set(MemArea_X, g_destination, 0);

		_dma.setDSR(g_channel, g_source);
		_dma.setDDR(g_channel, g_destination);
		_dma.setDCO(g_channel, 0);
		_dma.setDCR(g_channel, dcrForRequestSource(_hardwareRequestSource));
	}

	// Esai sets M_TDE in its constructor, so the transmit condition is pending
	// from the start and needs no ESAI traffic to raise it.
	constexpr TWord g_hardwareEsaiTransmitData = 12;	// EsaiTransmitData = 0b01100

	void theOverloadIsSelectedForA56311()
	{
		Peripherals56311 p(g_frameRate96k);
		Memory mem(g_memoryValidator, 0x080000, 0x800000, 0x200000);
		DSP dsp(mem, &p, &p.ySpace());

		verify(p.getEsai().readStatusRegister() & (1 << Esai::M_TDE));

		armWordChannel(p.getDMA(), mem, g_hardwareEsaiTransmitData);

		// The third overload answered true, so arm() triggered the channel.
		verify(mem.get(MemArea_X, g_destination) == g_payload);
	}

	// The negative case. Upstream's checkTrigger(Peripherals56362&, ...) opens
	// with return false;. Leave it alone: five shipping products run on this
	// path, and this half is what makes the positive half falsifiable.
	void theOverloadIsNotSelectedForA56362()
	{
		Peripherals56362 p;
		PeripheralsNop nop;
		Memory mem(g_memoryValidator, 0x080000, 0x800000, 0x200000);
		DSP dsp(mem, &p, &nop);

		verify(p.getEsai().readStatusRegister() & (1 << Esai::M_TDE));

		armWordChannel(p.getDMA(), mem, g_hardwareEsaiTransmitData);

		verify(mem.get(MemArea_X, g_destination) == 0);
	}

	// And for a Peripherals56303, whose own checkTrigger opens the same way.
	void theOverloadIsNotSelectedForA56303()
	{
		Peripherals56303 p;
		PeripheralsNop nop;
		Memory mem(g_memoryValidator, 0x080000, 0x800000, 0x200000);
		DSP dsp(mem, &p, &nop);

		constexpr TWord hardwareEssi0TransmitData = 11;		// Essi0TransmitData = 0b01011

		armWordChannel(p.getDMA(), mem, hardwareEssi0TransmitData);

		verify(mem.get(MemArea_X, g_destination) == 0);
	}
}

int main()
{
	try
	{
		theOverloadIsSelectedForA56311();
		theOverloadIsNotSelectedForA56362();
		theOverloadIsNotSelectedForA56303();
	}
	catch(const std::string& _err)
	{
		std::cout << "dsp56k_check_trigger_overload FAILED: " << _err << std::endl;
		return -1;
	}

	std::cout << "dsp56k_check_trigger_overload passed" << std::endl;
	return 0;
}
