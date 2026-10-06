// Test that Esai::txUnderrunInFrame() latches transmit underruns across the frame lifetime.

#include "dsp56kEmu/dsp.h"
#include "dsp56kEmu/esai.h"
#include "dsp56kEmu/memory.h"
#include "dsp56kEmu/peripherals.h"
#include "dsp56kEmu/unittests.h"

#include <iostream>
#include <vector>

namespace
{
	using namespace dsp56k;

	constexpr uint32_t g_frameRate96k = 96000;
	constexpr TWord g_payload = 0x123456;

	DefaultMemoryValidator g_memoryValidator;

	struct Delivery
	{
		bool underrunInFrame = false;
		bool tue = false;
		uint32_t slotCount = 0;
	};

	struct Fixture
	{
		Peripherals56362 periph;
		PeripheralsNop periphNop;
		Memory mem;
		DSP dsp;

		std::vector<Delivery> deliveries;

		Fixture()
			: periph()
			, periphNop()
			, mem(g_memoryValidator, 0x080000, 0x800000, 0x200000)
			, dsp(mem, &periph, &periphNop)
		{
			auto& e = esai();

			e.setWriteTxCallback([this](uint64_t&, const Audio::TxFrame& _frame)
			{
				auto& e2 = esai();
				deliveries.push_back({e2.txUnderrunInFrame(), e2.getSR().test(Esai::M_TUE) != 0, static_cast<uint32_t>(_frame.size())});
			});
		}

		Esai& esai() { return periph.getEsai(); }

		bool tue() { return esai().getSR().test(Esai::M_TUE); }

		// TDC[4:0] in TCCR is the frame rate divider; the emulated frame is TDC+1 slots long.
		void configure(const TWord _slotsPerFrame)
		{
			verify(_slotsPerFrame >= 1);
			periph.write(Esai::M_TSMA, 0xffff);
			periph.write(Esai::M_TCCR, (_slotsPerFrame - 1) << Esai::M_TDC0);
			periph.write(Esai::M_TCR, 1u << Esai::M_TE0);
			verify(esai().getTxWordCount() == _slotsPerFrame - 1);
		}

		void feedTransmitter()
		{
			esai().writeTX(0, g_payload);
		}

		void syncToFrameStart()
		{
			for(uint32_t guard = 0; deliveries.empty(); ++guard)
			{
				verify(guard < 64);
				feedTransmitter();
				esai().execTX();
			}

			deliveries.clear();
			verify(!esai().txUnderrunInFrame());
			verify(!tue());
		}
	};

	// Two-slot frame where slot 0 underruns, raising M_TUE and setting the frame latch.
	// When the transmitter is fed, M_TUE is cleared, but txUnderrunInFrame remains true
	// through frame delivery.
	void theUnderrunSurvivesToFrameDeliveryThoughTueIsAlreadyGone()
	{
		Fixture f;
		f.configure(2);
		f.syncToFrameStart();

		// Slot 0: unfed, causes underrun.
		f.esai().execTX();
		verify(f.esai().txUnderrunInFrame());
		verify(f.tue());
		verify(f.deliveries.empty());

		// Servicing clears M_TUE, but the frame-lifetime latch persists.
		f.feedTransmitter();
		verify(!f.tue());
		verify(f.esai().txUnderrunInFrame());

		// Slot 1: completes and delivers frame.
		f.esai().execTX();

		verify(f.deliveries.size() == 1);
		verify(f.deliveries[0].slotCount == 2);
		verify(f.deliveries[0].underrunInFrame);
		verify(!f.deliveries[0].tue);
	}

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

		verify(!f.esai().txUnderrunInFrame());

		// Frame 2 is fully fed.
		f.feedTransmitter();
		f.esai().execTX();
		f.feedTransmitter();
		f.esai().execTX();

		verify(f.deliveries.size() == 2);
		verify(!f.deliveries[1].underrunInFrame);
	}

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
