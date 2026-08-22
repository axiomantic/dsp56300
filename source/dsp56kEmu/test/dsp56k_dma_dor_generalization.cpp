// DSP-20 - generalize execTransfer's DualCounterDOR handler.
//
// The (SingleCounterApostInc, DualCounterDOR1) handler hardcoded getDOR(1);
// every other DualCounterDORn destination fell through to the
// unsupported-mode assert. The repair admits AGM 0-3 and indexes getDOR with
// the enum value, which equals the DOR slot for those modes.
//
// DOR1 is deliberately absent: the pre-existing handler already serves it,
// and a regression test for it belongs to DSP-4's check. Offsets are
// non-zero because a zero offset compares a default-zero DOR read against a
// default-zero address move and passes whether the slot was read or not.
//
// Limit: single-word request-triggered transfers with DCO = 0 in X space on
// a Peripherals56311, ESAI transmit as the request source. Line mode,
// multi-word counts and other spaces are outside this test's claim.

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

	constexpr TWord g_channel = 2;
	constexpr TWord g_source = 0x001000;
	constexpr TWord g_destination = 0x002000;
	constexpr TWord g_payload = 0x123456;

	DefaultMemoryValidator g_memoryValidator;

	// Source AGM 5 (SingleCounterApostInc). Destination AGM is the parameter.
	// Transfer mode: word, trigger=request, clear DE (2). Request source:
	// EsaiTransmitData, whose TDE flag is set at construction. Both spaces
	// are X. DE set.
	constexpr TWord dcrForDorTransfer(const AddressGenMode _destAgm, const TWord _hardwareRequestSource)
	{
		return (1u << DmaChannel::De)
			| (static_cast<TWord>(DmaChannel::TransferMode::WordTriggerRequestClearDE) << 19)
			| (_hardwareRequestSource << 11)
			| (static_cast<TWord>(_destAgm) << 7)
			| (static_cast<TWord>(DmaChannel::AddressGenMode::SingleCounterApostInc) << 4);
	}

	constexpr TWord g_hwEsaiTransmitData = 12;

	// Arm a word-trigger channel with the given destination AGM and DOR slot.
	void armDorChannel(Dma& _dma, Memory& _mem, const AddressGenMode _destAgm,
	                   const TWord _dorIndex, const TWord _dorValue)
	{
		_mem.set(MemArea_X, g_source, g_payload);
		_mem.set(MemArea_X, g_destination, 0);

		_dma.setDOR(_dorIndex, _dorValue);
		_dma.setDSR(g_channel, g_source);
		_dma.setDDR(g_channel, g_destination);
		_dma.setDCO(g_channel, 0);
		_dma.setDCR(g_channel, dcrForDorTransfer(_destAgm, g_hwEsaiTransmitData));
	}

	// The transfer copies one word to the destination and then applies the
	// DOR offset to the destination address, so the payload lands at the
	// programmed DDR and DDR ends at DDR + offset. An exact match on both is
	// what forces the handler to have read THIS slot: every other slot is
	// zero here, so reading the wrong one leaves the address unmoved.
	void dorTransferMovesDestinationByOffset(const AddressGenMode _destAgm,
	                                        const TWord _dorIndex,
	                                        const TWord _dorOffset)
	{
		Peripherals56311 p(g_frameRate96k);
		Memory mem(g_memoryValidator, 0x080000, 0x800000, 0x200000);
		DSP dsp(mem, &p, &p.ySpace());

		// ESAI sets M_TDE in its constructor, so arm() triggers immediately.
		verify(p.getEsai().readStatusRegister() & (1 << Esai::M_TDE));

		armDorChannel(p.getDMA(), mem, _destAgm, _dorIndex, _dorOffset);

		const auto ddrAfter = p.getDMA().getDDR(g_channel);

		verify(mem.get(MemArea_X, g_destination) == g_payload);

		verify(ddrAfter == ((g_destination + _dorOffset) & 0xffffff));
	}

	void dor0TransferUsesDor0Offset()
	{
		constexpr TWord offset = 0x000100;
		dorTransferMovesDestinationByOffset(AddressGenMode::DualCounterDOR0, 0, offset);
	}

	void dor2TransferUsesDor2Offset()
	{
		constexpr TWord offset = 0x000200;
		dorTransferMovesDestinationByOffset(AddressGenMode::DualCounterDOR2, 2, offset);
	}

	void dor3TransferUsesDor3Offset()
	{
		constexpr TWord offset = 0x000300;
		dorTransferMovesDestinationByOffset(AddressGenMode::DualCounterDOR3, 3, offset);
	}
}

int main()
{
	try
	{
		dor0TransferUsesDor0Offset();
		dor2TransferUsesDor2Offset();
		dor3TransferUsesDor3Offset();
	}
	catch(const std::string& _err)
	{
		std::cout << "dsp56k_dma_dor_generalization FAILED: " << _err << std::endl;
		return -1;
	}

	std::cout << "dsp56k_dma_dor_generalization passed" << std::endl;
	return 0;
}
