// The two-dimensional SOURCE transfer, which the G2 kernel programs.
//
// execTransfer handled a 2D DESTINATION (SingleCounterApostInc source, AGM
// 0-3 destination) and nothing on the mirrored side. A booted G2 kernel
// programs the mirror on every DSP: DCR $965AA0 on channel 2, i.e. DAM $2A,
// so DAM[2:0]=010 (source, two-dimensional, offset DOR2) and DAM[5:3]=101
// (destination, post-increment by 1), D3D=0, DTM=010 line transfer,
// DRS=01011 ESAI receive data, with DSR = $FFFFA8 (ESAI RX0), DOR2 =
// $FFFFFF (-1) and DCO = $007001. Channel 3 carries $96AAA1, the same DAM
// against the second ESAI. That combination fell through to the
// unsupported-mode limb.
//
// The shape is Table 10-5 of the DSP56362 user manual read in the other
// direction. That table configures a DAX transmit: the peripheral side is
// the two-dimensional one, its DOR is negative so the address returns to
// the first peripheral register after each frame, DCOL is "number of
// registers - 1" and DCOH is "number of frames in block - 1". A receive
// swaps which side is the peripheral, not the counter arithmetic.
//
// This test uses plain X memory on both sides so the two words the 2D side
// walks over carry values this test chose. DOR2 is -1 and the two source
// words differ, so a handler that failed to apply the offset would copy the
// second word twice and a handler that applied it to the wrong side would
// leave the destination stationary. Neither survives the assertions below.
//
// Limit: line-triggered request transfers in X space on a Peripherals56311,
// ESAI transmit as the request source because its TDE flag is set at
// construction. Word mode, other spaces and the three-dimensional modes are
// outside this test's claim.

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

	constexpr TWord g_wordA = 0x0a1b2c;
	constexpr TWord g_wordB = 0x3d4e5f;

	constexpr TWord g_dorSlot = 2;
	constexpr TWord g_dorOffset = 0xffffff;		// -1, the manual's wrap-to-first-register offset

	// DCOH = 1 -> two lines, DCOL = 1 -> two words per line, four words total.
	constexpr TWord g_dco = 0x001001;

	constexpr TWord g_hwEsaiTransmitData = 12;

	DefaultMemoryValidator g_memoryValidator;

	// Source AGM 2 (DualCounterDOR2), destination AGM 5 (SingleCounterApostInc),
	// line transfer triggered by request with DE cleared afterwards, both
	// spaces X, DE set. This is the G2's DAM $2A with a request source whose
	// flag is already raised at construction.
	constexpr TWord dcr2dSource()
	{
		return (1u << DmaChannel::De)
			| (static_cast<TWord>(DmaChannel::TransferMode::LineTriggerRequestClearDE) << 19)
			| (g_hwEsaiTransmitData << 11)
			| (static_cast<TWord>(AddressGenMode::SingleCounterApostInc) << 7)
			| (static_cast<TWord>(AddressGenMode::DualCounterDOR2) << 4);
	}

	void twoDimensionalSourceWalksTheSourceAndAdvancesTheDestination()
	{
		// The literal pins the encoding: a builder compared only against
		// itself would agree with any bit layout. This word is NOT the G2's
		// $965AA0 - that one also carries DRS=01011 (ESAI receive) and DPR=11,
		// and this test drives ESAI transmit at the default priority. DAM is
		// the field under test and it is $2A in both.
		verify(dcr2dSource() == 0x9062a0);
		verify(((dcr2dSource() >> 4) & 0x3f) == 0x2a);

		Peripherals56311 p(g_frameRate96k);
		Memory mem(g_memoryValidator, 0x080000, 0x800000, 0x200000);
		DSP dsp(mem, &p, &p.ySpace());

		// ESAI sets M_TDE in its constructor, so arm() triggers the first line.
		verify(p.getEsai().readStatusRegister() & (1 << Esai::M_TDE));

		mem.set(MemArea_X, g_source, g_wordA);
		mem.set(MemArea_X, g_source + 1, g_wordB);

		for(TWord i = 0; i < 5; ++i)
			mem.set(MemArea_X, g_destination + i, 0);

		auto& dma = p.getDMA();

		dma.setDOR(g_dorSlot, g_dorOffset);
		dma.setDSR(g_channel, g_source);
		dma.setDDR(g_channel, g_destination);
		dma.setDCO(g_channel, g_dco);
		dma.setDCR(g_channel, dcr2dSource());

		// First line: two words, then the source returns to its first word via
		// DOR2 and the destination has advanced by two. The block is not done,
		// so DE is still set and DCR is unchanged.
		verify(mem.get(MemArea_X, g_destination + 0) == g_wordA);
		verify(mem.get(MemArea_X, g_destination + 1) == g_wordB);
		verify(mem.get(MemArea_X, g_destination + 2) == 0);
		verify(mem.get(MemArea_X, g_destination + 3) == 0);
		verify(mem.get(MemArea_X, g_destination + 4) == 0);

		verify(dma.getDSR(g_channel) == g_source);
		verify(dma.getDDR(g_channel) == g_destination + 2);
		verify(dma.getDCR(g_channel) == 0x9062a0);

		// Second line ends the block: two more words, DE cleared.
		verify(dma.trigger(DmaChannel::RequestSource::EsaiTransmitData));

		verify(mem.get(MemArea_X, g_destination + 0) == g_wordA);
		verify(mem.get(MemArea_X, g_destination + 1) == g_wordB);
		verify(mem.get(MemArea_X, g_destination + 2) == g_wordA);
		verify(mem.get(MemArea_X, g_destination + 3) == g_wordB);
		verify(mem.get(MemArea_X, g_destination + 4) == 0);

		verify(dma.getDSR(g_channel) == g_source);
		verify(dma.getDDR(g_channel) == g_destination + 4);
		verify(dma.getDCR(g_channel) == (0x9062a0u & ~(1u << DmaChannel::De)));
	}
}

int main()
{
	try
	{
		twoDimensionalSourceWalksTheSourceAndAdvancesTheDestination();
	}
	catch(const std::string& _err)
	{
		std::cout << "dsp56k_dma_2d_source FAILED: " << _err << std::endl;
		return -1;
	}

	std::cout << "dsp56k_dma_2d_source passed" << std::endl;
	return 0;
}
