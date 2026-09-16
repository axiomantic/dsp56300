// WHAT THIS MEASURES
//
// Esai::txUnderrunInFrame() is a FRAME-lifetime flag. It must stay true from the
// slot that underran until the frame carrying that slot has been handed to
// writeTXimpl, which is the moment a consumer outside the peripheral sees it.
//
// WHAT MIRAGE IT REFUSES
//
// M_TUE is a SLOT-lifetime status bit and cannot answer the same question.
// writeSlotToFrame raises M_TUE and then triggers the transmit DMA, whose answer
// reaches Esai::writeTX, which clears M_TUE as soon as every enabled transmitter
// has been written -- several slots before the frame is delivered. A test that
// asserted the flag one line after the setter, or that read the flag from
// outside the delivery path, would pass just as happily against an
// implementation that simply forwarded M_TUE. It would measure nothing.
//
// So the load-bearing assertion here is a DIVERGENCE, sampled from inside the
// delivery callback: at the instant the frame is handed over,
// txUnderrunInFrame() is TRUE while M_TUE is ALREADY FALSE. Only a flag with its
// own storage and a frame lifetime can produce that pair. Replace the flag body
// with `return m_sr.test(M_TUE)` and theUnderrunSurvivesToFrameDeliveryThoughTue-
// IsAlreadyGone() goes red, because the impostor reports false at delivery.
//
// The M_TUE clear is driven through Esai::writeTX -- the real function the DMA
// reaches -- rather than by poking the status register, so the erasure under test
// is the one that actually happens on a running machine.

#include "dsp56kEmu/dsp.h"
#include "dsp56kEmu/esai.h"
#include "dsp56kEmu/memory.h"
#include "dsp56kEmu/peripherals56311.h"
#include "dsp56kEmu/unittests.h"

#include <iostream>
#include <vector>

namespace
{
	using namespace dsp56k;

	constexpr uint32_t g_frameRate96k = 96000;

	// An arbitrary non-zero payload. Its value is irrelevant to the flag; it
	// exists so that writeTX marks a transmitter as written.
	constexpr TWord g_payload = 0x123456;

	DefaultMemoryValidator g_memoryValidator;

	// What the consumer saw AT THE MOMENT OF DELIVERY. Sampling anywhere else
	// would not distinguish the latch from M_TUE, which is the whole point.
	struct Delivery
	{
		bool underrunInFrame = false;
		bool tue = false;
		uint32_t slotCount = 0;
	};

	struct Fixture
	{
		Peripherals56311 periph;
		Memory mem;
		DSP dsp;

		std::vector<Delivery> deliveries;

		Fixture()
			: periph(g_frameRate96k)
			, mem(g_memoryValidator, 0x080000, 0x800000, 0x200000)
			, dsp(mem, &periph, &periph.ySpace())
		{
			auto& e = esai();

			// The observation point. writeTXimpl calls exactly this, so a sample
			// taken here is a sample taken at frame delivery.
			e.setWriteTxCallback([this](uint64_t&, const Audio::TxFrame& _frame)
			{
				auto& e2 = esai();
				deliveries.push_back({e2.txUnderrunInFrame(), e2.getSR().test(Esai::M_TUE) != 0, _frame.size()});
			});
		}

		Esai& esai() { return periph.getEsai(); }

		bool tue() { return esai().getSR().test(Esai::M_TUE); }

		// TDC[4:0] in TCCR is the frame rate divider; the emulated frame is
		// TDC+1 slots long.
		void configure(const TWord _slotsPerFrame)
		{
			verify(_slotsPerFrame >= 1);
			periph.write(Esai::M_TSMA, 0xffff);
			periph.write(Esai::M_TCCR, (_slotsPerFrame - 1) << Esai::M_TDC0);
			periph.write(Esai::M_TCR, 1u << Esai::M_TE0);
			verify(esai().getTxWordCount() == _slotsPerFrame - 1);
		}

		// Feed transmitter 0 the way the DMA does. This is also what clears
		// M_TUE, because writeTX clears it once every enabled transmitter has
		// been written.
		void feedTransmitter()
		{
			esai().writeTX(0, g_payload);
		}

		// Enabling the transmitters in writeTransmitControlRegister calls execTX
		// itself, so configure() lands mid-frame with a slot already consumed and
		// the flag already raised. Run out to the next delivery, feeding every
		// slot, and drop what was observed: every test below then starts at slot 0
		// of a frame with the flag down, which is a state this fixture ESTABLISHED
		// rather than assumed.
		void syncToFrameStart()
		{
			for(uint32_t guard = 0; deliveries.empty(); ++guard)
			{
				verify(guard < 64);
				feedTransmitter();
				esai().execTX();
			}

			deliveries.clear();

			// The last slot consumed the fed word, so no transmitter is marked
			// written and slot 0 of the next frame underruns unless a test feeds
			// it. M_TUE went down with that same write.
			verify(!esai().txUnderrunInFrame());
			verify(!tue());
		}
	};

	// THE LOAD-BEARING TEST.
	//
	// Two-slot frame. Slot 0 is left unfed, so it underruns and raises both
	// M_TUE and the flag. The transmitter is then fed, which is what the DMA
	// does and which CLEARS M_TUE. Slot 1 therefore delivers a frame whose
	// underrun M_TUE no longer records -- but the flag must still carry it.
	void theUnderrunSurvivesToFrameDeliveryThoughTueIsAlreadyGone()
	{
		Fixture f;
		f.configure(2);
		f.syncToFrameStart();

		// Slot 0: nothing was written to TX, so this slot underruns.
		f.esai().execTX();
		verify(f.esai().txUnderrunInFrame());
		verify(f.tue());
		verify(f.deliveries.empty());

		// The DMA answers. writeTX clears M_TUE -- the erasure this flag exists
		// to survive.
		f.feedTransmitter();
		verify(!f.tue());

		// The flag is untouched by that erasure while the frame is still open.
		verify(f.esai().txUnderrunInFrame());

		// Slot 1 completes and delivers the frame.
		f.esai().execTX();

		verify(f.deliveries.size() == 1);
		verify(f.deliveries[0].slotCount == 2);

		// The divergence. An M_TUE-backed impostor reports false on the first
		// of these two lines.
		verify(f.deliveries[0].underrunInFrame);
		verify(!f.deliveries[0].tue);
	}

	// The control from the same population: an identically driven frame whose
	// slots are all fed delivers with the flag CLEAR. Without this, a flag
	// hardwired to true would satisfy the test above.
	void aFullyFedFrameIsDeliveredWithTheFlagClear()
	{
		Fixture f;
		f.configure(2);
		f.syncToFrameStart();

		f.feedTransmitter();
		f.esai().execTX();
		verify(!f.esai().txUnderrunInFrame());

		f.feedTransmitter();
		f.esai().execTX();

		verify(f.deliveries.size() == 1);
		verify(f.deliveries[0].slotCount == 2);
		verify(!f.deliveries[0].underrunInFrame);
	}

	// execTX clears the flag AFTER writeTXimpl, so the frame that follows an
	// underrunning one starts clean. A clear placed one line earlier would fail
	// the previous test; a clear that never happened fails this one.
	void theNextFrameStartsCleanAfterAnUnderrunningFrameWasDelivered()
	{
		Fixture f;
		f.configure(2);
		f.syncToFrameStart();

		// Frame 1 underruns on slot 0 and is delivered.
		f.esai().execTX();
		f.feedTransmitter();
		f.esai().execTX();

		verify(f.deliveries.size() == 1);
		verify(f.deliveries[0].underrunInFrame);

		// The flag is already down again the instant the frame is gone.
		verify(!f.esai().txUnderrunInFrame());

		// Frame 2 is fully fed and must deliver clean.
		f.feedTransmitter();
		f.esai().execTX();
		f.feedTransmitter();
		f.esai().execTX();

		verify(f.deliveries.size() == 2);
		verify(!f.deliveries[1].underrunInFrame);
	}

	// A TCR write discards the partly assembled frame, so a flag left standing
	// would be read against the NEXT frame and report an underrun belonging to a
	// frame that was never delivered. The TCR value keeps the same transmitter
	// set, so this exercises the clear itself and not the re-enable path.
	void aTransmitControlRegisterWriteClearsAStandingFlag()
	{
		Fixture f;
		f.configure(4);
		f.syncToFrameStart();

		f.esai().execTX();
		verify(f.esai().txUnderrunInFrame());
		verify(f.deliveries.empty());

		f.periph.write(Esai::M_TCR, 1u << Esai::M_TE0);

		verify(!f.esai().txUnderrunInFrame());
		verify(!f.tue());
	}

	// reset() clears the flag. A standing flag would otherwise outlive the
	// peripheral state it describes.
	void aResetClearsAStandingFlag()
	{
		Fixture f;
		f.configure(4);
		f.syncToFrameStart();

		f.esai().execTX();
		verify(f.esai().txUnderrunInFrame());

		f.esai().reset();

		verify(!f.esai().txUnderrunInFrame());
	}
}

int main()
{
	try
	{
		theUnderrunSurvivesToFrameDeliveryThoughTueIsAlreadyGone();
		aFullyFedFrameIsDeliveredWithTheFlagClear();
		theNextFrameStartsCleanAfterAnUnderrunningFrameWasDelivered();
		aTransmitControlRegisterWriteClearsAStandingFlag();
		aResetClearsAStandingFlag();
	}
	catch(const std::string& _err)
	{
		std::cout << "dsp56k_esai_tx_underrun_latch FAILED: " << _err << std::endl;
		return -1;
	}

	std::cout << "dsp56k_esai_tx_underrun_latch passed" << std::endl;
	return 0;
}
