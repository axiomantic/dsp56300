#pragma once

// DSP56311 peripheral set for the Nord Modular G2.
//
// The set presents TWO IPeripherals faces, because the DSP selects the face by
// address space before the virtual call (dsp.cpp:1018 and dsp.cpp:1078) and its
// constructor asserts that the two pointers differ (dsp.cpp:88).
//
//   Peripherals56311   the X-space face. It owns every block.
//   Peripherals56311Y  the Y-space face. It is a register window over the
//                      ESAI_1 instance the X-space face owns. It holds no
//                      block of its own and its exec() advances nothing.
//
// The set constructs ZERO EsaiClock objects. The scheduler drives both ESAI
// frames through the public Esai::execTX and Esai::execRX.

#include "dma.h"
#include "esai.h"
#include "hdi08.h"
#include "peripherals.h"
#include "timers.h"
#include "types.h"

#include <array>
#include <cstdint>

namespace dsp56k
{
	class Disassembler;

	class Peripherals56311Y final : public IPeripherals
	{
	public:
		// _frameRateHz records what the second bus IS. It is not a cadence
		// source: nothing here advances a frame. The rate is a parameter
		// because the second bus may be the 96 kHz audio bus or the 24 kHz
		// control bus, and the measurement that settles it is not in yet.
		Peripherals56311Y(Esai& _esai, uint32_t _frameRateHz);

		TWord read(TWord _addr, Instruction _inst) override;
		const TWord* readAsPtr(TWord _addr, Instruction _inst) override;
		void write(TWord _addr, TWord _val) override;

		uint32_t exec() noexcept { return MaxDelayCycles; }

		void reset() override;
		void setSymbols(Disassembler& _disasm) const override;
		void terminate() override;

		Esai& getEsai() const					{ return m_esai; }
		uint32_t getFrameRateHz() const			{ return m_frameRateHz; }

	private:
		Esai& m_esai;
		uint32_t m_frameRateHz;
		std::array<TWord, XIO_Reserved_High_Last - XIO_Reserved_High_First + 1> m_mem;
	};

	class Peripherals56311 final : public IPeripherals
	{
	public:
		explicit Peripherals56311(uint32_t _secondBusFrameRateHz);

		TWord read(TWord _addr, Instruction _inst) override;
		const TWord* readAsPtr(TWord _addr, Instruction _inst) override;
		void write(TWord _addr, TWord _val) override;

		uint32_t exec() noexcept;

		void reset() override;
		void setDSP(DSP* _dsp) override;
		void setSymbols(Disassembler& _disasm) const override;
		void terminate() override;

		Dma& getDMA()							{ return m_dma; }
		Esai& getEsai()							{ return m_esaiX; }		// X space, M_RX0 = $FFFFA8, M_TX0 = $FFFFA0
		Esai& getEsai1()						{ return m_esaiY; }		// Y space, M_RX0_1 = $FFFF88, M_TX2_1 = $FFFF82
		HDI08& getHDI08()						{ return m_hdi08; }
		const Timers& getTimers() const			{ return m_timers; }

		Peripherals56311Y& ySpace()				{ return m_ySpace; }
		const Peripherals56311Y& ySpace() const	{ return m_ySpace; }

	private:
		std::array<TWord, XIO_Reserved_High_Last - XIO_Reserved_High_First + 1> m_mem;
		Dma m_dma;
		Esai m_esaiX;
		Esai m_esaiY;
		HDI08 m_hdi08;
		Timers m_timers;
		Peripherals56311Y m_ySpace;
	};
}
