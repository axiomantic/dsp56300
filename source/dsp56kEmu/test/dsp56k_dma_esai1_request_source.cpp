// The 56311 carries two ESAIs, and a DMA request source names which one.
//
// checkTrigger's 56311 arm has four ESAI cases: two read getEsai(), the X-space
// instance, and two read getEsai1(), the Y-space one. Every other case in this
// suite leaves the two instances holding the same status bits -- both raise
// M_TDE from construction and neither has been fed a receive word -- so an
// accessor read from the wrong instance returns the right answer by accident.
// Swapping getEsai() for getEsai1() on either pair changes nothing any of them
// observes.
//
// This test makes the two disagree before it asks. Each case drives the status
// registers apart, arms one channel per request source, and asserts which one
// moved: the channel whose named ESAI has the flag set transfers at arm time,
// and the channel whose named ESAI does not stays put. A swap turns both
// assertions around at once.
//
// Limit: line-triggered request transfers in X space, post-increment on both
// sides, and the transmit and receive flags of the two ESAIs on a
// Peripherals56311. The status registers are written directly rather than
// driven through a clock, because what is under test is which register the
// request source reads, not how the bit comes to be set.

#include "dsp56kEmu/dsp.h"
#include "dsp56kEmu/memory.h"
#include "dsp56kEmu/peripherals56311.h"
#include "dsp56kEmu/unittests.h"

#include <cstdint>
#include <iostream>

namespace
{
	using namespace dsp56k;
	using AddressGenMode = DmaChannel::AddressGenMode;

	constexpr uint32_t g_frameRate96k = 96000;

	constexpr TWord g_wordA = 0x0a1b2c;
	constexpr TWord g_wordB = 0x3d4e5f;

	// DCOH = 0 -> one line, DCOL = 1 -> two words in it.
	constexpr TWord g_dco = 0x000001;

	DefaultMemoryValidator g_memoryValidator;

	// DCR carries the HARDWARE request-source number, which on the 56311 is a
	// different number space from the library enumerator: getRequestSource maps
	// hardware 21 and 22 onto Esai1ReceiveData and Esai1TransmitData, because
	// 0b10101 was already taken twice. Passing an enumerator here instead of a
	// hardware number selects the wrong peripheral -- writing 22 for ESAI_1
	// receive in fact asks for ESAI_1 transmit, and 23 lands on the enumerator
	// only through the switch's default arm.
	constexpr TWord g_hwEsaiReceiveData = 11;
	constexpr TWord g_hwEsaiTransmitData = 12;
	constexpr TWord g_hwEsai1ReceiveData = 21;
	constexpr TWord g_hwEsai1TransmitData = 22;

	constexpr TWord dcrFor(const TWord _hwRequestSource)
	{
		return (1u << DmaChannel::De)
			| (static_cast<TWord>(DmaChannel::TransferMode::LineTriggerRequestClearDE) << 19)
			| (_hwRequestSource << 11)
			| (static_cast<TWord>(AddressGenMode::SingleCounterApostInc) << 7)
			| (static_cast<TWord>(AddressGenMode::SingleCounterApostInc) << 4);
	}

	struct Fixture
	{
		Peripherals56311 p{g_frameRate96k};
		Memory mem{g_memoryValidator, 0x080000, 0x800000, 0x200000};
		DSP dsp{mem, &p, &p.ySpace()};

		void seed(const TWord _source, const TWord _destination)
		{
			mem.set(MemArea_X, _source, g_wordA);
			mem.set(MemArea_X, _source + 1, g_wordB);

			for(TWord i = 0; i < 3; ++i)
				mem.set(MemArea_X, _destination + i, 0);
		}

		// Arms one channel and reports whether the request was pending at arm time,
		// which is observable as the first line having moved.
		bool armedAndTransferred(const TWord _channel, const TWord _hwRequestSource, const TWord _source, const TWord _destination)
		{
			auto& dma = p.getDMA();

			dma.setDSR(_channel, _source);
			dma.setDDR(_channel, _destination);
			dma.setDCO(_channel, g_dco);
			dma.setDCR(_channel, dcrFor(_hwRequestSource));

			const auto moved = mem.get(MemArea_X, _destination) == g_wordA
				&& mem.get(MemArea_X, _destination + 1) == g_wordB;

			// Whatever happened, it is all-or-nothing: a half-moved line would mean
			// the answer below is not the one this test thinks it is reading.
			if(!moved)
			{
				verify(mem.get(MemArea_X, _destination) == 0);
				verify(mem.get(MemArea_X, _destination + 1) == 0);
			}

			return moved;
		}
	};

	// One measurement, one fixture. Arming two channels in one fixture made the
	// first channel's transfer perturb the state the second one is asked about,
	// and a swapped accessor then read a flag that the earlier transfer had
	// already cleared -- the test passed with the mutation in place.
	bool armOne(const TWord _esaiSR, const TWord _esai1SR, const TWord _hwRequestSource)
	{
		Fixture f;

		constexpr TWord source = 0x001000;
		constexpr TWord destination = 0x002000;

		f.seed(source, destination);

		f.p.getEsai().writestatusRegister(_esaiSR);
		f.p.getEsai1().writestatusRegister(_esai1SR);

		return f.armedAndTransferred(2, _hwRequestSource, source, destination);
	}

	// The X-space ESAI is silent and the Y-space one is asking. Only the channel
	// that names ESAI_1 may move.
	void theTransmitSourcesReadTheirOwnEsai()
	{
		constexpr TWord silent = 0;
		constexpr TWord asking = 1 << Esai::M_TDE;

		const auto esai0Moved = armOne(silent, asking, g_hwEsaiTransmitData);
		const auto esai1Moved = armOne(silent, asking, g_hwEsai1TransmitData);

		std::cout << "transmit: esai=" << esai0Moved << " esai1=" << esai1Moved << std::endl;

		verify(!esai0Moved);
		verify(esai1Moved);
	}

	// The mirror, so a swap is caught from both directions and on the other pair
	// of cases: now the X-space ESAI is the one asking.
	void theReceiveSourcesReadTheirOwnEsai()
	{
		constexpr TWord asking = 1 << Esai::M_RDF;
		constexpr TWord silent = 0;

		const auto esai0Moved = armOne(asking, silent, g_hwEsaiReceiveData);
		const auto esai1Moved = armOne(asking, silent, g_hwEsai1ReceiveData);

		std::cout << "receive: esai=" << esai0Moved << " esai1=" << esai1Moved << std::endl;

		verify(esai0Moved);
		verify(!esai1Moved);
	}
}

int main()
{
	try
	{
		theTransmitSourcesReadTheirOwnEsai();
		theReceiveSourcesReadTheirOwnEsai();
	}
	catch(const std::string& _err)
	{
		std::cout << "dsp56k_dma_esai1_request_source FAILED: " << _err << std::endl;
		return -1;
	}

	std::cout << "dsp56k_dma_esai1_request_source passed" << std::endl;
	return 0;
}
