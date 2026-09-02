// The 56311 peripheral set, composed.
//
// The set presents two IPeripherals faces. Peripherals56311 is the X-space
// face and owns every block. Peripherals56311Y is the Y-space face: a register
// window over the ESAI_1 instance the X-space face owns.
//
// The set constructs ZERO EsaiClock objects. The scheduler drives both ESAI
// frames, and Esai::execTX and Esai::execRX are public. That invariant is
// asserted here at COMPILE TIME, in three ways, because a runtime counter
// would need a change to an upstream header.

#include "dsp56kEmu/peripherals56311.h"
#include "dsp56kEmu/unittests.h"

#include <iostream>
#include <type_traits>

namespace
{
	using namespace dsp56k;

	constexpr uint32_t g_frameRate96k = 96000;
	constexpr uint32_t g_frameRate24k = 24000;

	// ---------------------------------------------------------------- the two
	// upstream sets are final, so the new set cannot EXTEND either of them.
	static_assert(std::is_final_v<Peripherals56362>, "Peripherals56362 is no longer final. The 56311 set must not extend it, and must not hold one: Peripherals56362 carries an EsaiClock by value");
	static_assert(std::is_final_v<Peripherals56367>, "Peripherals56367 is no longer final");

	// ---------------------------------------------------------------- both
	// faces derive directly from IPeripherals and from nothing else.
	static_assert(std::is_base_of_v<IPeripherals, Peripherals56311>, "Peripherals56311 is not an IPeripherals");
	static_assert(std::is_base_of_v<IPeripherals, Peripherals56311Y>, "Peripherals56311Y is not an IPeripherals");

	static_assert(!std::is_base_of_v<Peripherals56362, Peripherals56311> && !std::is_base_of_v<Peripherals56367, Peripherals56311>,
		"Peripherals56311 derives from an existing peripheral set. It must derive from IPeripherals directly");
	static_assert(!std::is_base_of_v<Peripherals56362, Peripherals56311Y> && !std::is_base_of_v<Peripherals56367, Peripherals56311Y>,
		"Peripherals56311Y derives from an existing peripheral set");

	static_assert(std::is_final_v<Peripherals56311>, "Peripherals56311 must be final");
	static_assert(std::is_final_v<Peripherals56311Y>, "Peripherals56311Y must be final");

	// ---------------------------------------------------------------- ZERO
	// EsaiClock, part 1: the upstream constructor still takes a
	// Peripherals56362&, so no other type can hold an EsaiClock by value.
	// Widening it to IPeripherals& is the one change that makes the invariant
	// breakable, and it fails to build here.
	static_assert(std::is_constructible_v<EsaiClock, Peripherals56362&>,
		"EsaiClock's constructor no longer accepts a Peripherals56362&. Upstream changed, and this assertion must be re-read before it is relaxed");
	static_assert(!std::is_constructible_v<EsaiClock, IPeripherals&>,
		"EsaiClock's constructor was WIDENED to IPeripherals&. That change exists only to let a set which is not a Peripherals56362 hold an EsaiClock by value. Revert it: the scheduler drives the ESAI frames");
	static_assert(!std::is_constructible_v<EsaiClock, Peripherals56311&>,
		"An EsaiClock can be constructed from a Peripherals56311. The set must construct ZERO EsaiClock objects");
	static_assert(!std::is_constructible_v<EsaiClock, Peripherals56311Y&>,
		"An EsaiClock can be constructed from a Peripherals56311Y");

	// ---------------------------------------------------------------- ZERO
	// EsaiClock, part 2: the member assertion. Each mirror repeats one face's
	// base and members in declaration order, so it has that face's layout. A
	// member added to a face - an EsaiClock, or a whole Peripherals5636x -
	// changes the size and fails the comparison. sizeof needs a complete type
	// and not a constructible one, so the mirrors need no constructor.
	struct XFaceMirror : IPeripherals
	{
		std::array<TWord, XIO_Reserved_High_Last - XIO_Reserved_High_First + 1> m_mem;
		Dma m_dma;
		Esai m_esaiX;
		Esai m_esaiY;
		HDI08 m_hdi08;
		Timers m_timers;
		Peripherals56311Y m_ySpace;
	};

	struct XFaceMirrorWithEsaiClock : IPeripherals
	{
		std::array<TWord, XIO_Reserved_High_Last - XIO_Reserved_High_First + 1> m_mem;
		Dma m_dma;
		Esai m_esaiX;
		Esai m_esaiY;
		HDI08 m_hdi08;
		Timers m_timers;
		Peripherals56311Y m_ySpace;
		EsaiClock m_esaiClock;
	};

	struct YFaceMirror : IPeripherals
	{
		Esai& m_esai;
		uint32_t m_frameRateHz;
		std::array<TWord, XIO_Reserved_High_Last - XIO_Reserved_High_First + 1> m_mem;
	};

	struct YFaceMirrorWithEsaiClock : IPeripherals
	{
		Esai& m_esai;
		uint32_t m_frameRateHz;
		std::array<TWord, XIO_Reserved_High_Last - XIO_Reserved_High_First + 1> m_mem;
		EsaiClock m_esaiClock;
	};

	// The detector is known to be sensitive: an added EsaiClock changes the
	// size of a mirror. Without this the two comparisons below could both hold
	// while an EsaiClock hid in tail padding.
	static_assert(sizeof(XFaceMirror) != sizeof(XFaceMirrorWithEsaiClock), "an EsaiClock member no longer changes the size of the X face. The member detector below proves nothing");
	static_assert(sizeof(YFaceMirror) != sizeof(YFaceMirrorWithEsaiClock), "an EsaiClock member no longer changes the size of the Y face");

	static_assert(sizeof(Peripherals56311) == sizeof(XFaceMirror),
		"Peripherals56311 gained or lost a member. If the new member is an EsaiClock or a Peripherals5636x, remove it: the set holds neither. If it is a legitimate block, add it to XFaceMirror and to XFaceMirrorWithEsaiClock");
	static_assert(sizeof(Peripherals56311Y) == sizeof(YFaceMirror),
		"Peripherals56311Y gained or lost a member. The Y face is a register window and holds no peripheral block of its own");

	// ---------------------------------------------------------------- runtime
	void bothFacesReportTheNewType()
	{
		Peripherals56311 p(g_frameRate96k);

		verify(p.getType() == PeripheralType::Peripherals56311);
		verify(p.ySpace().getType() == PeripheralType::Peripherals56311);

		// DSP::DSP asserts _pX != _pY, so the two faces must be two objects.
		verify(static_cast<IPeripherals*>(&p) != static_cast<IPeripherals*>(&p.ySpace()));
	}

	void theYFaceTakesItsFrameRateAsAParameter()
	{
		Peripherals56311 fast(g_frameRate96k);
		Peripherals56311 slow(g_frameRate24k);

		verify(fast.ySpace().getFrameRateHz() == g_frameRate96k);
		verify(slow.ySpace().getFrameRateHz() == g_frameRate24k);
	}

	void theYFaceAdvancesNothing()
	{
		Peripherals56311 p(g_frameRate96k);

		verify(p.ySpace().exec() == IPeripherals::MaxDelayCycles);
	}

	// The Y face is a WINDOW over the ESAI_1 the X face owns, and not a second
	// ESAI. A write through the Y face lands in the object the X face exposes.
	// The slot mask registers are plain stores, so this exercises the decode
	// and nothing else. A control register would enable transmitters, and
	// Esai::writeTransmitControlRegister then reaches m_periph.getDSP().
	void theYFaceIsAWindowOverTheXFacesSecondEsai()
	{
		Peripherals56311 p(g_frameRate96k);

		p.ySpace().write(Esai::M_TSMA_1, 0x00abcd);
		verify(p.getEsai1().readTSMA() == 0x00abcd);

		verify(p.ySpace().read(Esai::M_TSMA_1, Instruction::Nop) == 0x00abcd);
	}

	// The two faces answer different address spaces which carry the same
	// numbers. Neither may answer the other's names.
	void neitherFaceAnswersTheOthersRegisters()
	{
		Peripherals56311 p(g_frameRate96k);

		const auto esaiXTsmaBefore = p.getEsai().readTSMA();
		const auto esaiYTsmaBefore = p.getEsai1().readTSMA();

		// M_TSMA is an X-space name. The Y face must not answer it.
		p.ySpace().write(Esai::M_TSMA, 0x00beef);
		verify(p.getEsai().readTSMA() == esaiXTsmaBefore);

		// M_TSMA_1 is a Y-space name. The X face must not answer it.
		p.write(Esai::M_TSMA_1, 0x00feed);
		verify(p.getEsai1().readTSMA() == esaiYTsmaBefore);

		// The X face DOES answer its own ESAI's name.
		p.write(Esai::M_TSMA, 0x001234);
		verify(p.getEsai().readTSMA() == 0x001234);
		verify(p.read(Esai::M_TSMA, Instruction::Nop) == 0x001234);
	}

	// The X face owns the Timers, and Timers spans X:$FFFF82 to X:$FFFF8F,
	// which are the same numbers ESAI_1 carries in the Y space.
	void theXFaceOwnsTheTimers()
	{
		Peripherals56311 p(g_frameRate96k);

		p.write(Timers::M_TCR1, 0x00cafe);					// X:$FFFF88
		verify(p.read(Timers::M_TCR1, Instruction::Nop) == 0x00cafe);

		// $FFFF88 in the Y space is ESAI_1's RX0 and reaches no timer.
		p.ySpace().write(Esai::M_RX0_1, 0x005555);
		verify(p.read(Timers::M_TCR1, Instruction::Nop) == 0x00cafe);
	}

	// An address outside the peripheral window must not index the backing
	// array. The fall-through subtracts XIO_Reserved_High_First, so an address
	// below it underflows and reads out of bounds.
	void anAddressOutsideTheWindowIsRefused()
	{
		Peripherals56311 p(g_frameRate96k);

		verify(p.read(0x000000, Instruction::Nop) == 0);
		verify(p.read(XIO_Reserved_High_First - 1, Instruction::Nop) == 0);
		verify(p.readAsPtr(0x000000, Instruction::Nop) == nullptr);
		p.write(0x000000, 0x123456);						// must not write out of bounds

		verify(p.ySpace().read(0x000000, Instruction::Nop) == 0);
		verify(p.ySpace().readAsPtr(0x000000, Instruction::Nop) == nullptr);
		p.ySpace().write(0x000000, 0x123456);
	}
}

int main()
{
	try
	{
		bothFacesReportTheNewType();
		theYFaceTakesItsFrameRateAsAParameter();
		theYFaceAdvancesNothing();
		theYFaceIsAWindowOverTheXFacesSecondEsai();
		neitherFaceAnswersTheOthersRegisters();
		theXFaceOwnsTheTimers();
		anAddressOutsideTheWindowIsRefused();
	}
	catch(const std::string& _err)
	{
		std::cout << "dsp56k_peripherals56311_surface FAILED: " << _err << std::endl;
		return -1;
	}

	std::cout << "dsp56k_peripherals56311_surface passed" << std::endl;
	return 0;
}
