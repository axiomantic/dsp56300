// DSP-4, part 2 - the DMA dispatch branch for a Peripherals56311.
//
// DmaChannel::arm tests m_peripherals.getType() against Peripherals56303 and
// Peripherals56362 only, and its else is
// assert(false && "TODO unknown peripherals, not supported yet").
// DmaChannel::setDCR calls arm() unconditionally, so before this task any DCR
// write on a Peripherals56311 aborted a build with assertions enabled, and did
// nothing at all in a build without them.
//
// "No assertion fires" is not falsifiable on its own: source/base.cmake sets
// CMAKE_BUILD_TYPE to Release when it is unset, so the default build defines
// NDEBUG and every assert() in this repository compiles to nothing. The test
// therefore asserts the branch's OWN effect - the channel is registered as a
// trigger target - which fails in a build with assertions and in a build
// without them alike.

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

	constexpr TWord g_channel = 3;
	constexpr TWord g_source = 0x001000;
	constexpr TWord g_destination = 0x002000;

	DefaultMemoryValidator g_memoryValidator;

	constexpr TWord dcrForRequestSource(const TWord _hardwareRequestSource)
	{
		return (1u << DmaChannel::De)
			| (static_cast<TWord>(DmaChannel::TransferMode::WordTriggerRequest) << 19)
			| (_hardwareRequestSource << 11)
			| (static_cast<TWord>(DmaChannel::AddressGenMode::SingleCounterAnoUpdate) << 7)
			| (static_cast<TWord>(DmaChannel::AddressGenMode::SingleCounterApostInc) << 4);
	}

	void armWordChannel(Dma& _dma, const TWord _hardwareRequestSource)
	{
		_dma.setDSR(g_channel, g_source);
		_dma.setDDR(g_channel, g_destination);
		_dma.setDCO(g_channel, 0);
		_dma.setDCR(g_channel, dcrForRequestSource(_hardwareRequestSource));
	}

	constexpr TWord g_hardwareEsaiReceiveData = 11;		// EsaiReceiveData = 0b01011

	void aChannelArmedOnA56311ReachesTheDispatchBranch()
	{
		Peripherals56311 p(g_frameRate96k);
		Memory mem(g_memoryValidator, 0x080000, 0x800000, 0x200000);
		DSP dsp(mem, &p, &p.ySpace());

		verify(p.getType() == PeripheralType::Peripherals56311);
		verify(!p.getDMA().hasTrigger(RequestSource::EsaiReceiveData));

		armWordChannel(p.getDMA(), g_hardwareEsaiReceiveData);

		// Only the dispatch branch calls Dma::addTriggerTarget.
		verify(p.getDMA().hasTrigger(RequestSource::EsaiReceiveData));
	}

	// hasTrigger discriminates: a source with no armed channel answers false.
	// Without this the assertion above would hold against a hasTrigger that
	// always answered true.
	void anUnarmedSourceHasNoTrigger()
	{
		Peripherals56311 p(g_frameRate96k);
		Memory mem(g_memoryValidator, 0x080000, 0x800000, 0x200000);
		DSP dsp(mem, &p, &p.ySpace());

		armWordChannel(p.getDMA(), g_hardwareEsaiReceiveData);

		verify(p.getDMA().hasTrigger(RequestSource::EsaiReceiveData));
		verify(!p.getDMA().hasTrigger(RequestSource::EsaiTransmitData));
		verify(!p.getDMA().hasTrigger(RequestSource::Timer0));
	}

	// The two upstream branches keep working. The branch this task adds must
	// not change how an existing set arms a channel.
	void theUpstreamBranchesStillRegister()
	{
		{
			Peripherals56362 p;
			PeripheralsNop nop;
			Memory mem(g_memoryValidator, 0x080000, 0x800000, 0x200000);
			DSP dsp(mem, &p, &nop);

			verify(!p.getDMA().hasTrigger(RequestSource::EsaiReceiveData));
			armWordChannel(p.getDMA(), g_hardwareEsaiReceiveData);
			verify(p.getDMA().hasTrigger(RequestSource::EsaiReceiveData));
		}

		{
			Peripherals56303 p;
			PeripheralsNop nop;
			Memory mem(g_memoryValidator, 0x080000, 0x800000, 0x200000);
			DSP dsp(mem, &p, &nop);

			constexpr TWord hardwareEssi0ReceiveData = 10;	// Essi0ReceiveData = 0b01010

			verify(!p.getDMA().hasTrigger(RequestSource::Essi0ReceiveData));
			armWordChannel(p.getDMA(), hardwareEssi0ReceiveData);
			verify(p.getDMA().hasTrigger(RequestSource::Essi0ReceiveData));
		}
	}
}

int main()
{
	try
	{
		aChannelArmedOnA56311ReachesTheDispatchBranch();
		anUnarmedSourceHasNoTrigger();
		theUpstreamBranchesStillRegister();
	}
	catch(const std::string& _err)
	{
		std::cout << "dsp56k_peripheral_type_dispatch FAILED: " << _err << std::endl;
		return -1;
	}

	std::cout << "dsp56k_peripheral_type_dispatch passed" << std::endl;
	return 0;
}
