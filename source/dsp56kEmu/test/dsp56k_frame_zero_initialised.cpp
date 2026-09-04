// A default-constructed frame owns zeroed sample storage.
//
// A frame's storage spans MaxSlotsPerFrame while size() reports 0, so the slots
// a caller reaches after resize() were never written by the frame itself.
// Without the member initialiser those reads are indeterminate, and a frame
// built where a previous one stood would deliver the previous frame's samples
// as audio. The second check below is that reuse case: it fills a frame, lets
// it die, and requires the next frame built at the same depth to read zero.

#include "dsp56kEmu/audio.h"
#include "dsp56kEmu/unittests.h"

#include <cstddef>
#include <iostream>

namespace
{
	using namespace dsp56k;

	constexpr TWord g_sample = 0x5a5a5a;

	template<typename TFrame> void verifyAllSlotsZero(const TFrame& _frame)
	{
		for(size_t slot = 0; slot < Audio::MaxSlotsPerFrame; ++slot)
		{
			for(const TWord word : _frame[slot])
				verify(word == 0);
		}
	}

	void everySlotOfADefaultConstructedFrameReadsZero()
	{
		Audio::TxFrame tx;
		Audio::RxFrame rx;

		verify(tx.empty());
		verify(rx.empty());

		verifyAllSlotsZero(tx);
		verifyAllSlotsZero(rx);
	}

	void fillEverySlot()
	{
		Audio::TxFrame tx;
		Audio::RxFrame rx;

		tx.resize(Audio::MaxSlotsPerFrame);
		rx.resize(Audio::MaxSlotsPerFrame);

		for(size_t slot = 0; slot < Audio::MaxSlotsPerFrame; ++slot)
		{
			for(TWord& word : tx[slot])
				word = g_sample;

			for(TWord& word : rx[slot])
				word = g_sample;
		}

		verify(tx[Audio::MaxSlotsPerFrame - 1][0] == g_sample);
		verify(rx[Audio::MaxSlotsPerFrame - 1][0] == g_sample);
	}

	void aFrameBuiltWhereAFilledOneStoodReadsZero()
	{
		fillEverySlot();
		everySlotOfADefaultConstructedFrameReadsZero();
	}
}

int main()
{
	try
	{
		everySlotOfADefaultConstructedFrameReadsZero();
		aFrameBuiltWhereAFilledOneStoodReadsZero();
	}
	catch(const std::string& _err)
	{
		std::cout << "dsp56k_frame_zero_initialised FAILED: " << _err << std::endl;
		return -1;
	}

	std::cout << "dsp56k_frame_zero_initialised passed" << std::endl;
	return 0;
}
