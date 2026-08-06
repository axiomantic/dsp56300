#pragma once

#include "peripherals.h"
#include "dma.h"
#include "esai.h"
#include "esaiclock.h"
#include "gpio.h"
#include "hdi08.h"
#include "timers.h"
#include <array>

namespace dsp56k
{
	class Peripherals56311 final : public IPeripherals
	{
	public:
		Peripherals56311();
		~Peripherals56311() override = default;

		TWord read(TWord _addr, Instruction _inst) override;
		const TWord* readAsPtr(TWord _addr, Instruction _inst) override;
		void write(TWord _addr, TWord _val) override;

		uint32_t exec() noexcept;
		void reset() override;

		void setDSP(DSP* _dsp) override;
		void setSymbols(Disassembler& _disasm) const override;
		void terminate() override;

		Dma& getDMA() { return m_dma; }
		EsaiClock& getEsaiClock() { return m_esaiClock; }
		Esai& getEsai() { return m_esai; }
		HDI08& getHDI08() { return m_hdi08; }
		const Timers& getTimers() const { return m_timers; }
		EsaiPortC& getPortC() { return m_portC; }

	private:
		std::array<TWord, XIO_Reserved_High_Last - XIO_Reserved_High_First + 1> m_mem;
		Dma m_dma;
		EsaiClock m_esaiClock;
		Esai m_esai;
		HDI08 m_hdi08;
		Timers m_timers;
		EsaiPortC m_portC;
	};
}
