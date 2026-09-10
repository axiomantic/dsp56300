// Writing a DMA register on a channel that is ALREADY armed must not arm it a
// second time.
//
// DmaChannel::arm() is reached from four setters. Three of them guard on
// m_armed, and arm() itself returns early when the flag is set, so the flag is
// the whole of the protection. Dropping it is invisible to a test that
// configures a channel once and never touches it again: every register write
// happens while the channel is still unarmed, so the guard is never asked a
// question it can answer wrongly.
//
// What a second arm() does is not subtle. It reloads the working counters from
// the current DCO, discarding a partly walked block; it calls addTriggerTarget
// again, and that push_backs unconditionally, so the channel is now registered
// twice against one request source and one trigger moves two lines; and because
// the ESAI raises M_TDE from construction, the request is already pending, so
// arm() transfers immediately rather than waiting.
//
// This test arms a line-triggered channel, lets the first line run, and then
// writes DCO, DSR and DDR on the armed channel. None of the three may move a
// word or disturb the walk, and the block must still finish on exactly one more
// trigger.
//
// Limit: line-triggered request transfers in X space on a Peripherals56311 with
// ESAI transmit as the request source. The DCO write is the one that reaches
// arm() on this path; DSR and DDR are guarded by their first-write flag as well
// and are here because they are the other two registers a driver rewrites. They
// are rewritten with the values the walk already holds, because those two
// registers are themselves the working pointers.

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

	// DCOH = 1 -> two lines, DCOL = 1 -> two words per line.
	constexpr TWord g_dco = 0x001001;

	// A different value, so setDCO does not take its early return. Its size is
	// what makes a re-arm visible: were the working counters reloaded from this,
	// the block would no longer end on the second line.
	constexpr TWord g_dcoRewrite = 0x003003;

	constexpr TWord g_hwEsaiTransmitData = 12;

	DefaultMemoryValidator g_memoryValidator;

	constexpr TWord dcr2dSource()
	{
		return (1u << DmaChannel::De)
			| (static_cast<TWord>(DmaChannel::TransferMode::LineTriggerRequestClearDE) << 19)
			| (g_hwEsaiTransmitData << 11)
			| (static_cast<TWord>(AddressGenMode::SingleCounterApostInc) << 7)
			| (static_cast<TWord>(AddressGenMode::DualCounterDOR2) << 4);
	}

	void aRegisterWriteOnAnArmedChannelDoesNotReArmIt()
	{
		verify(dcr2dSource() == 0x9062a0);

		Peripherals56311 p(g_frameRate96k);
		Memory mem(g_memoryValidator, 0x080000, 0x800000, 0x200000);
		DSP dsp(mem, &p, &p.ySpace());

		// ESAI sets M_TDE in its constructor, so the request is pending throughout
		// and any arm() call transfers rather than waiting.
		verify(p.getEsai().readStatusRegister() & (1 << Esai::M_TDE));

		mem.set(MemArea_X, g_source, g_wordA);
		mem.set(MemArea_X, g_source + 1, g_wordB);

		for(TWord i = 0; i < 6; ++i)
			mem.set(MemArea_X, g_destination + i, 0);

		auto& dma = p.getDMA();

		dma.setDOR(g_dorSlot, g_dorOffset);
		dma.setDSR(g_channel, g_source);
		dma.setDDR(g_channel, g_destination);
		dma.setDCO(g_channel, g_dco);
		dma.setDCR(g_channel, dcr2dSource());

		// The channel is armed and one line has run.
		verify(mem.get(MemArea_X, g_destination + 0) == g_wordA);
		verify(mem.get(MemArea_X, g_destination + 1) == g_wordB);
		verify(mem.get(MemArea_X, g_destination + 2) == 0);
		verify(dma.getDDR(g_channel) == g_destination + 2);

		// The writes under test. Each one reaches a setter that can call arm(). The
		// two address registers are rewritten with the values the walk is already
		// holding: DSR and DDR ARE the working pointers, so writing a different
		// address would move the walk legitimately and the assertions below could
		// not tell that apart from a re-arm.
		dma.setDCO(g_channel, g_dcoRewrite);
		dma.setDSR(g_channel, g_source);
		dma.setDDR(g_channel, g_destination + 2);

		// Nothing moved. A re-arm would have transferred a line here and left
		// $0a1b2c at destination+2, because the request was already pending.
		verify(mem.get(MemArea_X, g_destination + 2) == 0);
		verify(mem.get(MemArea_X, g_destination + 3) == 0);

		// The walk is where the first line left it, not back at the start.
		verify(dma.getDDR(g_channel) == g_destination + 2);
		verify(dma.getDSR(g_channel) == g_source);

		// DCO itself does take the new value -- it is the working counters that are
		// left alone, which is what separates this from a write the setter ignored.
		verify(dma.getDCO(g_channel) == g_dcoRewrite);

		// Still armed, still enabled, and still counting against the original block.
		verify(dma.getDCR(g_channel) == 0x9062a0);

		// One trigger, one line, and the block ends: the second line completes the
		// two the original DCO asked for. A channel registered twice would move two
		// lines here and reach destination+6.
		verify(dma.trigger(DmaChannel::RequestSource::EsaiTransmitData));

		verify(mem.get(MemArea_X, g_destination + 2) == g_wordA);
		verify(mem.get(MemArea_X, g_destination + 3) == g_wordB);
		verify(mem.get(MemArea_X, g_destination + 4) == 0);
		verify(mem.get(MemArea_X, g_destination + 5) == 0);

		verify(dma.getDDR(g_channel) == g_destination + 4);

		// DE cleared: the block finished on the count the channel was armed with.
		verify(dma.getDCR(g_channel) == (0x9062a0u & ~(1u << DmaChannel::De)));
	}
}

int main()
{
	try
	{
		aRegisterWriteOnAnArmedChannelDoesNotReArmIt();
	}
	catch(const std::string& _err)
	{
		std::cout << "dsp56k_dma_rearm_guard FAILED: " << _err << std::endl;
		return -1;
	}

	std::cout << "dsp56k_dma_rearm_guard passed" << std::endl;
	return 0;
}
