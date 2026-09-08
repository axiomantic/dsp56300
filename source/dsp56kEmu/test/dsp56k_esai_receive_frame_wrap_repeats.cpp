// The receive frame wrap has to keep happening, frame after frame.
//
// Esai::execRX counts slots and, when the count passes the frame length, wraps:
// it clears the slot counter, advances the frame counter, and raises the receive
// last-slot request. Every existing case in this suite runs a single frame, so
// the clear is never load-bearing -- the counter starts at zero, reaches the
// frame length once, and the case ends. Deleting it leaves all of them green,
// including the one whose name says it checks that the request arrives on the
// wrap and not on every slot: one frame cannot tell the two apart.
//
// From the second frame on, the two are nothing alike. Without the clear the
// slot counter never comes back below the frame length, so every later slot
// wraps: the request that is specified once per frame arrives on every slot,
// and RFS, which marks the first slot of a frame, is never set again.
//
// This case runs two frames and asserts the shape of both. It is the same
// program twice, which is the whole point.
//
// Limit: the receive side of the X-space ESAI on a Peripherals56311, four slots
// to the frame, RLIE armed and the slot mask fully open.

#include "dsp56kEmu/dsp.h"
#include "dsp56kEmu/interrupts.h"
#include "dsp56kEmu/memory.h"
#include "dsp56kEmu/peripherals56311.h"
#include "dsp56kEmu/unittests.h"

#include <iostream>
#include <string>

namespace
{
	using namespace dsp56k;

	constexpr uint32_t g_frameRate96k = 96000;

	constexpr TWord g_noMarker = 0xa5a5a5;

	constexpr TWord g_instrumentedVectors[] =
	{
		Vba_ESAI_Receive_Data,
		Vba_ESAI_Receive_Even_Data,
		Vba_ESAI_Receive_Data_With_Exception_Status,
		Vba_ESAI_Receive_Last_Slot,
	};

	// MOVE #xx,D left-aligns the 8-bit immediate in the 24-bit destination.
	constexpr TWord markerFor(const TWord _vba) { return _vba << 16; }

	constexpr TWord g_pcIdle = 0x100;

	// NOP assembles to $000000, and memWriteP only notifies the JIT when the
	// written word differs from what P memory holds, so every word here is a
	// non-zero instruction. The idle instruction branches to itself because a
	// straight-line pad compiles into one block that runs off the end.
	constexpr const char* g_idleInstruction = "jmp $100";
	constexpr const char* g_vectorPadInstruction = "move #$00,y0";

	DefaultMemoryValidator g_memoryValidator;

	// The parameter is not called _asm: MSVC treats that spelling as the __asm
	// keyword, and the declaration does not parse there.
	TWord assembleOne(const Assembler& _assembler, const std::string& _text)
	{
		const auto r = _assembler.assemble(_text.c_str());
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
			for (const auto vba : g_instrumentedVectors)
			{
				verify(vba <= 0xff);
				dsp.memWriteP(vba, assembleOne(assembler, "move #" + hex2(vba) + ",x0"));
				dsp.memWriteP(vba + 1, assembleOne(assembler, g_vectorPadInstruction));
			}

			dsp.memWriteP(g_pcIdle, assembleOne(assembler, g_idleInstruction));

			// A hardware reset leaves the interrupt mask at level 3, which masks
			// every ESAI vector.
			const_cast<TReg24&>(dsp.getSR()).var &= ~static_cast<TWord>(SR_I0 | SR_I1);

			resetMarker();

			dsp.setPC(g_pcIdle);

			for (uint32_t i = 0; i < 2; ++i)
				dsp.exec();

			verify(dsp.getPC().toWord() == g_pcIdle);
			verify(!dsp.hasPendingInterrupts());
			verify(marker() == g_noMarker);
		}

		TWord marker() const { return static_cast<TWord>(dsp.readRegs().x.var & 0xffffff); }

		void resetMarker() { const_cast<DspRegs&>(dsp.readRegs()).x.var = g_noMarker; }

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

	// RDC[4:0] in RCCR is the frame rate divider; the emulated frame is RDC+1
	// slots long.
	TWord rccrForSlotCount(const TWord _slots)
	{
		verify(_slots >= 1);
		return (_slots - 1) << Esai::M_RDC0;
	}

	void theFrameWrapRepeatsOnEveryFrame()
	{
		constexpr TWord slotsPerFrame = 4;
		constexpr TWord frames = 2;

		Fixture f;
		auto& esai = f.periph.getEsai();
		esai.setReadRxCallback(&silentReceiver);

		f.periph.write(Esai::M_RSMA, 0xffff);
		f.periph.write(Esai::M_RCCR, rccrForSlotCount(slotsPerFrame));
		f.periph.write(Esai::M_RCR, (1u << Esai::M_RE0) | (1u << Esai::M_RLIE));

		for (TWord frame = 0; frame < frames; ++frame)
		{
			for (TWord slot = 0; slot < slotsPerFrame; ++slot)
			{
				esai.execRX();

				const auto rfs = (esai.readStatusRegister() & (1u << Esai::M_RFS)) != 0;
				const auto pending = f.dsp.hasPendingInterrupts();
				const auto isLastSlot = slot == slotsPerFrame - 1;

				std::cout << "frame " << frame << " slot " << slot
					<< ": rfs=" << rfs << " pending=" << pending << std::endl;

				// RFS marks the first slot of a frame. On the second frame this
				// can only be true if the slot counter came back to zero.
				verify(rfs == (slot == 0));

				// The last-slot request belongs on the wrap, once per frame.
				verify(pending == isLastSlot);
			}

			f.serviceInterrupts();

			verify(f.marker() == markerFor(Vba_ESAI_Receive_Last_Slot));

			f.resetMarker();
		}
	}
}

int main()
{
	try
	{
		theFrameWrapRepeatsOnEveryFrame();
	}
	catch(const std::string& _err)
	{
		std::cout << "dsp56k_esai_receive_frame_wrap_repeats FAILED: " << _err << std::endl;
		return -1;
	}

	std::cout << "dsp56k_esai_receive_frame_wrap_repeats passed" << std::endl;
	return 0;
}
