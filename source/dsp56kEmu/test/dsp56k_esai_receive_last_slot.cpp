// RCR bit 23 (RLIE) arms the ESAI receive last slot interrupt. The DSP56362
// user manual, section 8.4.3 item 4, places the request "after the last slot of
// the frame ended [...] regardless of the receive mask register setting", which
// is the frame wrap and not the slot.
//
// The vector is identified rather than merely counted: each candidate vector
// carries a distinct `move #vector,x0`, so a request routed to the wrong vector
// leaves a different marker than a request routed to the right one, and no
// request at all leaves the sentinel this fixture wrote into x0.

#include "dsp56kEmu/assembler.h"
#include "dsp56kEmu/dsp.h"
#include "dsp56kEmu/interrupts.h"
#include "dsp56kEmu/memory.h"
#include "dsp56kEmu/peripherals56311.h"
#include "dsp56kEmu/unittests.h"

#include <iostream>

namespace
{
	using namespace dsp56k;

	constexpr uint32_t g_frameRate96k = 96000;

	// Written into x0 during setup so that "no vector ran" is a value this
	// fixture put there, not whatever a fresh register happened to hold.
	constexpr TWord g_noMarker = 0xa5a5a5;

	// Every ESAI vector that a receive frame could plausibly reach. The marker
	// value IS the vector address, so a failure names the vector that ran.
	constexpr TWord g_instrumentedVectors[] =
	{
		Vba_ESAI_Receive_Data,
		Vba_ESAI_Receive_Even_Data,
		Vba_ESAI_Receive_Data_With_Exception_Status,
		Vba_ESAI_Receive_Last_Slot,
		Vba_ESAI_Transmit_Data,
		Vba_ESAI_Transmit_Last_Slot,
		Vba_ESAI_1_Receive_Data,
		Vba_ESAI_1_Receive_Even_Data,
		Vba_ESAI_1_Receive_Data_With_Exception_Status,
		Vba_ESAI_1_Receive_Last_Slot,
		Vba_ESAI_1_Transmit_Data,
		Vba_ESAI_1_Transmit_Last_Slot,
	};

	// MOVE #xx,D left-aligns the 8-bit immediate in the 24-bit destination.
	constexpr TWord markerFor(const TWord _vba) { return _vba << 16; }

	constexpr TWord g_pcIdle = 0x100;

	// NOP assembles to $000000, and DSP::memWriteP only notifies the JIT when
	// the written word DIFFERS from what P memory already holds. Writing NOP
	// over freshly zeroed memory is therefore invisible to the JIT, which then
	// leaves a null entry to jump through. Every word this fixture emits is a
	// non-zero instruction for that reason.
	//
	// The idle instruction also has to be a branch to itself: a straight-line
	// pad compiles into one JIT block that runs off the end of P memory.
	constexpr const char* g_idleInstruction = "jmp $100";

	// The second word of each fast interrupt routine. It must not touch x0.
	constexpr const char* g_vectorPadInstruction = "move #$00,y0";

	DefaultMemoryValidator g_memoryValidator;

	TWord assembleOne(const Assembler& _asm, const std::string& _text)
	{
		const auto r = _asm.assemble(_text.c_str());
		verify(r.success());
		verify(r.wordCount == 1);
		verify(r.word[0] != 0);
		return r.word[0];
	}

	std::string hex2(const TWord _v)
	{
		constexpr char digits[] = "0123456789abcdef";
		return std::string("$") + digits[(_v >> 4) & 0xf] + digits[_v & 0xf];
	}

	// A DSP whose interrupt vector table reports which vector was taken.
	struct Fixture
	{
		Peripherals56311 periph;
		Memory mem;
		DSP dsp;
		Assembler assembler;

		Fixture()
			: periph(g_frameRate96k)
			, mem(g_memoryValidator, 0x080000, 0x800000, 0x200000)
			, dsp(mem, &periph, &periph.ySpace())
		{
			// Program words go through memWriteP, not Memory::set: the write
			// path notifies the JIT, which sizes its entry table for the
			// written range. A bare memory write leaves m_jitEntries unsized
			// and the first exec() jumps through a null pointer.
			for (const auto vba : g_instrumentedVectors)
			{
				verify(vba <= 0xff);
				dsp.memWriteP(vba, assembleOne(assembler, "move #" + hex2(vba) + ",x0"));
				dsp.memWriteP(vba + 1, assembleOne(assembler, g_vectorPadInstruction));
			}

			dsp.memWriteP(g_pcIdle, assembleOne(assembler, g_idleInstruction));

			// A hardware reset leaves the interrupt mask at level 3, which
			// masks every ESAI vector - they sit above Vba_IRQA and so carry
			// priority 2. DSP's setSR is private; getSR is public and returns
			// a const ref to the live register.
			const_cast<TReg24&>(dsp.getSR()).var &= ~static_cast<TWord>(SR_I0 | SR_I1);

			const_cast<DspRegs&>(dsp.readRegs()).x.var = g_noMarker;

			dsp.setPC(g_pcIdle);

			// Nothing is armed yet, so these instructions cannot inject
			// anything; they exist to let the JIT compile the idle block
			// before an interrupt can be taken.
			for (uint32_t i = 0; i < 2; ++i)
				dsp.exec();

			verify(dsp.getPC().toWord() == g_pcIdle);
			verify(!dsp.hasPendingInterrupts());
			verify(marker() == g_noMarker);
		}

		TWord marker() const { return static_cast<TWord>(dsp.readRegs().x.var & 0xffffff); }

		// Give the DSP enough instructions to take a pending interrupt and run
		// both words of the fast interrupt routine.
		void serviceInterrupts()
		{
			for (int i = 0; i < 4; ++i)
				dsp.exec();
		}
	};

	void silentReceiver(uint64_t&, Audio::RxFrame& _frame)
	{
		_frame.resize(1);
		_frame[0].fill(0);
	}

	// RDC[4:0] in RCCR is the frame rate divider; the emulated frame is
	// RDC+1 slots long.
	TWord rccrForSlotCount(const TWord _slots)
	{
		verify(_slots >= 1);
		return (_slots - 1) << Esai::M_RDC0;
	}

	void thePrimaryEsaiRaisesItsOwnLastSlotVectorWhenRlieIsSet()
	{
		Fixture f;
		auto& esai = f.periph.getEsai();
		esai.setReadRxCallback(&silentReceiver);

		f.periph.write(Esai::M_RSMA, 0xffff);
		f.periph.write(Esai::M_RCCR, rccrForSlotCount(1));
		f.periph.write(Esai::M_RCR, (1u << Esai::M_RE0) | (1u << Esai::M_RLIE));

		esai.execRX();

		verify(f.dsp.hasPendingInterrupts());

		f.serviceInterrupts();

		verify(f.marker() == markerFor(Vba_ESAI_Receive_Last_Slot));
	}

	// The paired negative. Without it the change is an unguarded firehose: an
	// injection that ignores RLIE would interrupt on every frame of every
	// receiver the DSP ever enables.
	void thePrimaryEsaiRaisesNothingWhenRlieIsClear()
	{
		Fixture f;
		auto& esai = f.periph.getEsai();
		esai.setReadRxCallback(&silentReceiver);

		f.periph.write(Esai::M_RSMA, 0xffff);
		f.periph.write(Esai::M_RCCR, rccrForSlotCount(1));
		f.periph.write(Esai::M_RCR, 1u << Esai::M_RE0);

		esai.execRX();

		verify(!f.dsp.hasPendingInterrupts());

		f.serviceInterrupts();

		verify(f.marker() == g_noMarker);
	}

	// "after the last slot of the frame ended" - once per frame, not once per
	// slot. An injection placed outside the wrap branch fires on slot 1 of 4.
	void theRequestArrivesOnTheFrameWrapAndNotOnEverySlot()
	{
		constexpr TWord slotsPerFrame = 4;

		Fixture f;
		auto& esai = f.periph.getEsai();
		esai.setReadRxCallback(&silentReceiver);

		f.periph.write(Esai::M_RSMA, 0xffff);
		f.periph.write(Esai::M_RCCR, rccrForSlotCount(slotsPerFrame));
		f.periph.write(Esai::M_RCR, (1u << Esai::M_RE0) | (1u << Esai::M_RLIE));

		for (TWord slot = 0; slot < slotsPerFrame - 1; ++slot)
		{
			esai.execRX();
			verify(!f.dsp.hasPendingInterrupts());
		}

		esai.execRX();

		verify(f.dsp.hasPendingInterrupts());

		f.serviceInterrupts();

		verify(f.marker() == markerFor(Vba_ESAI_Receive_Last_Slot));
	}

	// "regardless of the receive mask register setting" - the last slot of the
	// frame being masked out does not suppress the request.
	void theRequestIgnoresTheReceiveSlotMask()
	{
		constexpr TWord slotsPerFrame = 2;

		Fixture f;
		auto& esai = f.periph.getEsai();
		esai.setReadRxCallback(&silentReceiver);

		f.periph.write(Esai::M_RSMA, 0x0000);
		f.periph.write(Esai::M_RSMB, 0x0000);
		f.periph.write(Esai::M_RCCR, rccrForSlotCount(slotsPerFrame));
		f.periph.write(Esai::M_RCR, (1u << Esai::M_RE0) | (1u << Esai::M_RLIE));

		esai.execRX();
		verify(!f.dsp.hasPendingInterrupts());
		esai.execRX();

		verify(f.dsp.hasPendingInterrupts());

		f.serviceInterrupts();

		verify(f.marker() == markerFor(Vba_ESAI_Receive_Last_Slot));
	}

	// The G2 kernel arms RLIE on the SECOND bus, whose vector base is offset by
	// Vba_ESAI_1_Receive_Data - Vba_ESAI_Receive_Data. A request that reached
	// $36 instead of $76 would leave the kernel waiting.
	void theSecondEsaiRaisesTheEsai1LastSlotVector()
	{
		Fixture f;
		auto& esai1 = f.periph.getEsai1();
		esai1.setReadRxCallback(&silentReceiver);

		f.periph.ySpace().write(Esai::M_RSMA_1, 0xffff);
		f.periph.ySpace().write(Esai::M_RCCR_1, rccrForSlotCount(1));
		f.periph.ySpace().write(Esai::M_RCR_1, (1u << Esai::M_RE0) | (1u << Esai::M_RLIE));

		esai1.execRX();

		verify(f.dsp.hasPendingInterrupts());

		f.serviceInterrupts();

		verify(f.marker() == markerFor(Vba_ESAI_1_Receive_Last_Slot));
	}

	void theSecondEsaiRaisesNothingWhenRlieIsClear()
	{
		Fixture f;
		auto& esai1 = f.periph.getEsai1();
		esai1.setReadRxCallback(&silentReceiver);

		f.periph.ySpace().write(Esai::M_RSMA_1, 0xffff);
		f.periph.ySpace().write(Esai::M_RCCR_1, rccrForSlotCount(1));
		f.periph.ySpace().write(Esai::M_RCR_1, 1u << Esai::M_RE0);

		esai1.execRX();

		verify(!f.dsp.hasPendingInterrupts());

		f.serviceInterrupts();

		verify(f.marker() == g_noMarker);
	}
}

int main()
{
	try
	{
		thePrimaryEsaiRaisesItsOwnLastSlotVectorWhenRlieIsSet();
		thePrimaryEsaiRaisesNothingWhenRlieIsClear();
		theRequestArrivesOnTheFrameWrapAndNotOnEverySlot();
		theRequestIgnoresTheReceiveSlotMask();
		theSecondEsaiRaisesTheEsai1LastSlotVector();
		theSecondEsaiRaisesNothingWhenRlieIsClear();
	}
	catch(const std::string& _err)
	{
		std::cout << "dsp56k_esai_receive_last_slot FAILED: " << _err << std::endl;
		return -1;
	}

	std::cout << "dsp56k_esai_receive_last_slot passed" << std::endl;
	return 0;
}
