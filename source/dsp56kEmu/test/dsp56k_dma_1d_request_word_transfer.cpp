// A word transfer and a line transfer triggered by a request each move one word
// per request, with post-increment on both sides.
//
// DSP56300FM Table 10-5, DTM: "Word Transfer ... A word-by-word block transfer
// (length set by the counter)". With both address modes post-increment the
// counter is Counter Mode A (Table 10-6), which section 10.5.3.1 describes word
// by word: each transfer decrements DCO and advances the address register, and
// the transfer that finds DCO at zero reloads it and ends the block.
//
// A line transfer (DTM 010) is the undefined case. DCOL is the only thing the
// manual gives a line its length from, and Counter Mode A is a single undivided
// DCO with no DCOL, so hardware defines no line here. The neighbouring
// single-counter address modes move one word per request, and this asserts the
// same for the line transfer rather than a whole block on one request.
//
// Limit: DTM 001 and DTM 010 on a Peripherals56311 in X space, triggered through
// the X-space ESAI transmit request, which raises TDE from construction.

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
	using RequestSource = DmaChannel::RequestSource;

	constexpr uint32_t g_frameRate96k = 96000;

	constexpr TWord g_channel = 2;
	constexpr TWord g_source = 0x001000;
	constexpr TWord g_destination = 0x002000;

	constexpr TWord g_wordA = 0x0a1b2c;
	constexpr TWord g_wordB = 0x3d4e5f;

	// Mode A: two words in the block
	constexpr TWord g_dco = 0x000001;

	constexpr TWord g_hwEsaiTransmitData = 12;

	DefaultMemoryValidator g_memoryValidator;

	struct Fixture
	{
		Peripherals56311 p{g_frameRate96k};
		Memory mem{g_memoryValidator, 0x080000, 0x800000, 0x200000};
		DSP dsp{mem, &p, &p.ySpace()};

		Dma& dma() { return p.getDMA(); }

		TWord destination(const TWord _offset) const { return mem.get(MemArea_X, g_destination + _offset); }

		bool enabled() { return (dma().getDCR(g_channel) >> DmaChannel::De) & 1; }

		void verifyState(const TWord _moved)
		{
			verify(destination(0) == (_moved > 0 ? g_wordA : 0));
			verify(destination(1) == (_moved > 1 ? g_wordB : 0));
			verify(destination(2) == 0);

			verify(dma().getDSR(g_channel) == g_source + _moved);
			verify(dma().getDDR(g_channel) == g_destination + _moved);
		}
	};

	void eachRequestMovesOneWord(const DmaChannel::TransferMode _transferMode, const char* _label)
	{
		Fixture f;

		f.mem.set(MemArea_X, g_source, g_wordA);
		f.mem.set(MemArea_X, g_source + 1, g_wordB);
		f.mem.set(MemArea_X, g_source + 2, 0x777777);

		for(TWord i = 0; i < 3; ++i)
			f.mem.set(MemArea_X, g_destination + i, 0);

		const TWord dcr = (1u << DmaChannel::De)
			| (static_cast<TWord>(_transferMode) << 19)
			| (g_hwEsaiTransmitData << 11)
			| (static_cast<TWord>(AddressGenMode::SingleCounterApostInc) << 7)
			| (static_cast<TWord>(AddressGenMode::SingleCounterApostInc) << 4);

		f.dma().setDSR(g_channel, g_source);
		f.dma().setDDR(g_channel, g_destination);
		f.dma().setDCO(g_channel, g_dco);
		f.dma().setDCR(g_channel, dcr);

		// TDE is already set, so arming serves the first request
		std::cout << _label << " after arm: dst=" << HEX(f.destination(0)) << "," << HEX(f.destination(1)) << " dsr=" << HEX(f.dma().getDSR(g_channel)) << " dco=" << HEX(f.dma().getDCO(g_channel)) << std::endl;
		f.verifyState(1);
		verify(f.dma().getDCO(g_channel) == 0);
		verify(f.enabled());

		f.dma().trigger(RequestSource::EsaiTransmitData);

		std::cout << _label << " after second request: dst=" << HEX(f.destination(0)) << "," << HEX(f.destination(1)) << " dsr=" << HEX(f.dma().getDSR(g_channel)) << " dco=" << HEX(f.dma().getDCO(g_channel)) << std::endl;
		f.verifyState(2);
		verify(f.dma().getDCO(g_channel) == g_dco);
		verify(!f.enabled());

		// the block is done and DE is clear, so a further request moves nothing
		f.dma().trigger(RequestSource::EsaiTransmitData);
		f.verifyState(2);
	}
}

int main()
{
	try
	{
		eachRequestMovesOneWord(DmaChannel::TransferMode::WordTriggerRequestClearDE, "word");
		eachRequestMovesOneWord(DmaChannel::TransferMode::LineTriggerRequestClearDE, "line");
	}
	catch(const std::string& _err)
	{
		std::cout << "dsp56k_dma_1d_request_word_transfer FAILED: " << _err << std::endl;
		return -1;
	}

	std::cout << "dsp56k_dma_1d_request_word_transfer passed" << std::endl;
	return 0;
}
