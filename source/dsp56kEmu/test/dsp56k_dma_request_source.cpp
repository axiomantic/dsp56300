// The hardware DCR field and the library enumerator are two number spaces for
// the ESAI_1 pair. They agree for the primary ESAI and differ for ESAI_1:
//
//   G2 channel | carries      | hardware DCR field | library RequestSource
//   2          | ESAI RX0     | 11                 | EsaiReceiveData    (11)
//   4          | ESAI TX0     | 12                 | EsaiTransmitData   (12)
//   3          | ESAI_1 RX0   | 21                 | Esai1ReceiveData   (22)
//   5          | ESAI_1 TX2   | 22                 | Esai1TransmitData  (23)
//
// DmaChannel::getRequestSource is chip-agnostic, so the translation is gated on
// PeripheralType::Peripherals56311.

#include "dsp56kEmu/dsp.h"
#include "dsp56kEmu/memory.h"
#include "dsp56kEmu/peripherals56311.h"
#include "dsp56kEmu/unittests.h"

#include <cstdint>
#include <iostream>

namespace
{
	using namespace dsp56k;
	using RequestSource = DmaChannel::RequestSource;

	constexpr uint32_t g_frameRate96k = 96000;

	constexpr TWord g_source = 0x001000;
	constexpr TWord g_destination = 0x002000;

	DefaultMemoryValidator g_memoryValidator;

	// ---------------------------------------------------------- the values are
	// pinned here, because "past the new pair" is not a number. 0b10101 (21) is
	// Dsp56362Reserved AND Dsp56303Reserved, so the pair starts one above it,
	// and Count is one above the pair.
	static_assert(static_cast<uint32_t>(RequestSource::Esai1ReceiveData) == 22, "Esai1ReceiveData must be 0b10110 (22). 21 is Dsp56303Reserved and Dsp56362Reserved");
	static_assert(static_cast<uint32_t>(RequestSource::Esai1TransmitData) == 23, "Esai1TransmitData must be 0b10111 (23)");
	static_assert(static_cast<uint32_t>(RequestSource::Count) == 24, "Count must be 0b11000 (24), one past Esai1TransmitData. m_requestTargets is sized from it");

	static_assert(RequestSource::Esai1ReceiveData != RequestSource::Dsp56303Reserved, "Esai1ReceiveData duplicates Dsp56303Reserved");
	static_assert(RequestSource::Esai1ReceiveData != RequestSource::Dsp56362Reserved, "Esai1ReceiveData duplicates Dsp56362Reserved");
	static_assert(RequestSource::Esai1TransmitData != RequestSource::Esai1ReceiveData, "the ESAI_1 receive and transmit sources are the same value");

	// The index bound. m_requestTargets holds Count elements, so every source
	// must be below Count or the first arm writes out of bounds.
	static_assert(static_cast<uint32_t>(RequestSource::Esai1ReceiveData) < static_cast<uint32_t>(RequestSource::Count), "Esai1ReceiveData indexes past the end of m_requestTargets");
	static_assert(static_cast<uint32_t>(RequestSource::Esai1TransmitData) < static_cast<uint32_t>(RequestSource::Count), "Esai1TransmitData indexes past the end of m_requestTargets");

	// ---------------------------------------------------------- the hardware
	// DCR fields, as the firmware writes them.
	constexpr TWord g_hwEsaiReceiveData = 11;
	constexpr TWord g_hwEsaiTransmitData = 12;
	constexpr TWord g_hwEsai1ReceiveData = 21;
	constexpr TWord g_hwEsai1TransmitData = 22;

	constexpr TWord dcrForRequestSource(const TWord _hardwareRequestSource)
	{
		return (1u << DmaChannel::De)
			| (static_cast<TWord>(DmaChannel::TransferMode::WordTriggerRequest) << 19)
			| (_hardwareRequestSource << 11)
			| (static_cast<TWord>(DmaChannel::AddressGenMode::SingleCounterAnoUpdate) << 7)
			| (static_cast<TWord>(DmaChannel::AddressGenMode::SingleCounterApostInc) << 4);
	}

	void armWordChannel(Dma& _dma, const TWord _channel, const TWord _hardwareRequestSource)
	{
		_dma.setDSR(_channel, g_source);
		_dma.setDDR(_channel, g_destination);
		_dma.setDCO(_channel, 0);
		_dma.setDCR(_channel, dcrForRequestSource(_hardwareRequestSource));
	}

	void the56311TranslatesTheEsai1Pair()
	{
		Peripherals56311 p(g_frameRate96k);
		Memory mem(g_memoryValidator, 0x080000, 0x800000, 0x200000);
		DSP dsp(mem, &p, &p.ySpace());

		auto& dma = p.getDMA();

		armWordChannel(dma, 3, g_hwEsai1ReceiveData);

		verify(dma.hasTrigger(RequestSource::Esai1ReceiveData));
		verify(!dma.hasTrigger(RequestSource::Dsp56303Reserved));
		verify(!dma.hasTrigger(RequestSource::Esai1TransmitData));

		armWordChannel(dma, 5, g_hwEsai1TransmitData);

		verify(dma.hasTrigger(RequestSource::Esai1TransmitData));
		verify(dma.hasTrigger(RequestSource::Esai1ReceiveData));
	}

	// The primary ESAI pair agrees in both number spaces, and the translation
	// must leave it alone.
	void the56311LeavesThePrimaryEsaiPairAlone()
	{
		Peripherals56311 p(g_frameRate96k);
		Memory mem(g_memoryValidator, 0x080000, 0x800000, 0x200000);
		DSP dsp(mem, &p, &p.ySpace());

		auto& dma = p.getDMA();

		armWordChannel(dma, 2, g_hwEsaiReceiveData);
		armWordChannel(dma, 4, g_hwEsaiTransmitData);

		verify(dma.hasTrigger(RequestSource::EsaiReceiveData));
		verify(dma.hasTrigger(RequestSource::EsaiTransmitData));
		verify(!dma.hasTrigger(RequestSource::Esai1ReceiveData));
		verify(!dma.hasTrigger(RequestSource::Esai1TransmitData));
	}

	// getRequestSource is chip-agnostic. An ungated remap would change the
	// decode of the two shipping chips.
	void the56362DecodesUnchanged()
	{
		Peripherals56362 p;
		PeripheralsNop nop;
		Memory mem(g_memoryValidator, 0x080000, 0x800000, 0x200000);
		DSP dsp(mem, &p, &nop);

		auto& dma = p.getDMA();

		armWordChannel(dma, 2, g_hwEsaiReceiveData);
		verify(dma.hasTrigger(RequestSource::EsaiReceiveData));

		armWordChannel(dma, 4, g_hwEsaiTransmitData);
		verify(dma.hasTrigger(RequestSource::EsaiTransmitData));

		// A field of 21 stays 21 here. It must NOT become Esai1ReceiveData.
		armWordChannel(dma, 3, g_hwEsai1ReceiveData);
		verify(dma.hasTrigger(RequestSource::Dsp56362Reserved));
		verify(!dma.hasTrigger(RequestSource::Esai1ReceiveData));

		// And a field of 22 stays 22, which is the raw value and not a
		// translation of anything.
		armWordChannel(dma, 5, g_hwEsai1TransmitData);
		verify(dma.hasTrigger(static_cast<RequestSource>(22)));
		verify(!dma.hasTrigger(RequestSource::Esai1TransmitData));
	}

	void the56303DecodesUnchanged()
	{
		Peripherals56303 p;
		PeripheralsNop nop;
		Memory mem(g_memoryValidator, 0x080000, 0x800000, 0x200000);
		DSP dsp(mem, &p, &nop);

		auto& dma = p.getDMA();

		constexpr TWord hwEssi0ReceiveData = 10;
		constexpr TWord hwEssi0TransmitData = 11;

		armWordChannel(dma, 0, hwEssi0ReceiveData);
		verify(dma.hasTrigger(RequestSource::Essi0ReceiveData));

		armWordChannel(dma, 1, hwEssi0TransmitData);
		verify(dma.hasTrigger(RequestSource::Essi0TransmitData));

		armWordChannel(dma, 3, g_hwEsai1ReceiveData);
		verify(dma.hasTrigger(RequestSource::Dsp56303Reserved));
		verify(!dma.hasTrigger(RequestSource::Esai1ReceiveData));
	}

	// The pair Esai triggers on is a construction parameter, in the same way
	// that HDI08 selects its pair by chip variant.
	void theEsaiTakesItsRequestSourcePairAsAParameter()
	{
		Peripherals56311 p(g_frameRate96k);

		verify(p.getEsai().getDmaReceiveSource() == RequestSource::EsaiReceiveData);
		verify(p.getEsai().getDmaTransmitSource() == RequestSource::EsaiTransmitData);

		PeripheralsNop nop;
		Esai defaulted(nop, MemArea_X);
		verify(defaulted.getDmaReceiveSource() == RequestSource::EsaiReceiveData);
		verify(defaulted.getDmaTransmitSource() == RequestSource::EsaiTransmitData);

		Esai second(nop, MemArea_Y, nullptr, RequestSource::Esai1ReceiveData, RequestSource::Esai1TransmitData);
		verify(second.getDmaReceiveSource() == RequestSource::Esai1ReceiveData);
		verify(second.getDmaTransmitSource() == RequestSource::Esai1TransmitData);
	}
}

int main()
{
	try
	{
		the56311TranslatesTheEsai1Pair();
		the56311LeavesThePrimaryEsaiPairAlone();
		the56362DecodesUnchanged();
		the56303DecodesUnchanged();
		theEsaiTakesItsRequestSourcePairAsAParameter();
	}
	catch(const std::string& _err)
	{
		std::cout << "dsp56k_dma_request_source FAILED: " << _err << std::endl;
		return -1;
	}

	std::cout << "dsp56k_dma_request_source passed" << std::endl;
	return 0;
}
