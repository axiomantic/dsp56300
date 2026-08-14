// Esai::m_dma is a constructor argument that defaults to nullptr, and
// Peripherals56367 passes none, so both m_dma->trigger(...) calls sit behind a
// guard that is false. The 56311 set must construct its SECOND Esai with the
// DMA controller it owns, and with the ESAI_1 request source pair, or channel 3
// and channel 5 never fire.

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

	constexpr TWord g_channelEsai1Rx = 3;			// ESAI_1 RX0 in the G2 DMA map
	constexpr TWord g_hwEsai1ReceiveData = 21;		// the hardware DCR field
	constexpr TWord g_source = 0x001000;
	constexpr TWord g_destination = 0x002000;
	constexpr TWord g_payload = 0x0abcde;

	DefaultMemoryValidator g_memoryValidator;

	constexpr TWord dcrForRequestSource(const TWord _hardwareRequestSource)
	{
		return (1u << DmaChannel::De)
			| (static_cast<TWord>(DmaChannel::TransferMode::WordTriggerRequest) << 19)
			| (_hardwareRequestSource << 11)
			| (static_cast<TWord>(DmaChannel::AddressGenMode::SingleCounterAnoUpdate) << 7)
			| (static_cast<TWord>(DmaChannel::AddressGenMode::SingleCounterApostInc) << 4);
	}

	void theSecondEsaiCarriesTheEsai1RequestSourcePair()
	{
		Peripherals56311 p(g_frameRate96k);

		verify(p.getEsai1().getDmaReceiveSource() == RequestSource::Esai1ReceiveData);
		verify(p.getEsai1().getDmaTransmitSource() == RequestSource::Esai1TransmitData);

		// The primary ESAI keeps the pair it always had.
		verify(p.getEsai().getDmaReceiveSource() == RequestSource::EsaiReceiveData);
		verify(p.getEsai().getDmaTransmitSource() == RequestSource::EsaiTransmitData);
	}

	void anEsai1ReceiveSlotTriggersChannel3()
	{
		Peripherals56311 p(g_frameRate96k);
		Memory mem(g_memoryValidator, 0x080000, 0x800000, 0x200000);
		DSP dsp(mem, &p, &p.ySpace());

		auto& esai1 = p.getEsai1();

		esai1.setReadRxCallback([](uint64_t&, Audio::RxFrame& _frame)
		{
			_frame.resize(1);
			_frame[0].fill(0);
		});

		// Slot 0 active, receiver 0 enabled. RIE stays clear, so no interrupt.
		p.ySpace().write(Esai::M_RSMA_1, 0xffff);
		p.ySpace().write(Esai::M_RCR_1, 1u << Esai::M_RE0);

		// Clear the status register so that arming cannot trigger on a
		// condition that was already pending. The transfer below must be
		// caused by the receive slot and by nothing else.
		p.ySpace().write(Esai::M_SAISR_1, 0);
		verify(!(p.getEsai1().readStatusRegister() & (1 << Esai::M_RDF)));

		mem.set(MemArea_X, g_source, g_payload);
		mem.set(MemArea_X, g_destination, 0);

		auto& dma = p.getDMA();
		dma.setDSR(g_channelEsai1Rx, g_source);
		dma.setDDR(g_channelEsai1Rx, g_destination);
		dma.setDCO(g_channelEsai1Rx, 0);
		dma.setDCR(g_channelEsai1Rx, dcrForRequestSource(g_hwEsai1ReceiveData));

		verify(dma.hasTrigger(RequestSource::Esai1ReceiveData));
		verify(mem.get(MemArea_X, g_destination) == 0);

		// One receive slot on the second ESAI. Nothing in this repository drives
		// these ESAIs yet, so the test calls the public Esai::execRX itself.
		esai1.execRX();

		verify(p.getEsai1().readStatusRegister() & (1 << Esai::M_RDF));
		verify(mem.get(MemArea_X, g_destination) == g_payload);
	}

	// The second ESAI must not reach the PRIMARY pair. If it triggered
	// EsaiReceiveData, a channel armed for the primary ESAI would fire on
	// ESAI_1 traffic.
	void theSecondEsaiDoesNotTriggerThePrimaryPair()
	{
		Peripherals56311 p(g_frameRate96k);
		Memory mem(g_memoryValidator, 0x080000, 0x800000, 0x200000);
		DSP dsp(mem, &p, &p.ySpace());

		auto& esai1 = p.getEsai1();

		esai1.setReadRxCallback([](uint64_t&, Audio::RxFrame& _frame)
		{
			_frame.resize(1);
			_frame[0].fill(0);
		});

		p.ySpace().write(Esai::M_RSMA_1, 0xffff);
		p.ySpace().write(Esai::M_RCR_1, 1u << Esai::M_RE0);
		p.ySpace().write(Esai::M_SAISR_1, 0);

		mem.set(MemArea_X, g_source, g_payload);
		mem.set(MemArea_X, g_destination, 0);

		// Channel 2 carries the PRIMARY ESAI's RX0, hardware field 11.
		constexpr TWord channelEsaiRx = 2;
		constexpr TWord hwEsaiReceiveData = 11;

		auto& dma = p.getDMA();
		dma.setDSR(channelEsaiRx, g_source);
		dma.setDDR(channelEsaiRx, g_destination);
		dma.setDCO(channelEsaiRx, 0);
		dma.setDCR(channelEsaiRx, dcrForRequestSource(hwEsaiReceiveData));

		verify(dma.hasTrigger(RequestSource::EsaiReceiveData));

		esai1.execRX();

		verify(mem.get(MemArea_X, g_destination) == 0);
	}
}

int main()
{
	try
	{
		theSecondEsaiCarriesTheEsai1RequestSourcePair();
		anEsai1ReceiveSlotTriggersChannel3();
		theSecondEsaiDoesNotTriggerThePrimaryPair();
	}
	catch(const std::string& _err)
	{
		std::cout << "dsp56k_second_esai_dma FAILED: " << _err << std::endl;
		return -1;
	}

	std::cout << "dsp56k_second_esai_dma passed" << std::endl;
	return 0;
}
