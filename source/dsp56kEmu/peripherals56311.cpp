#include "peripherals56311.h"

#include "aar.h"
#include "disasm.h"
#include "dsp.h"
#include "interrupts.h"

namespace dsp56k
{
	Peripherals56311::Peripherals56311()
		: IPeripherals(PeripheralType::Peripherals56311)
		, m_mem{}
		, m_dma(*this)
		, m_esaiClock(*this)
		, m_esai(*this, MemArea_Y, &m_dma)
		, m_hdi08(*this)
		, m_timers(*this, Vba_TIMER0_Compare)
	{
		m_mem.fill(0);
		m_esaiClock.setEsaiDivider(&m_esai, 0);
	}

	TWord Peripherals56311::read(const TWord _addr, const Instruction _inst)
	{
		switch (_addr)
		{
		case HDI08::HSR:	return m_hdi08.readStatusRegister();
		case HDI08::HCR:	return m_hdi08.readControlRegister();
		case HDI08::HPCR:	return m_hdi08.readPortControlRegister();
		case HDI08::HORX:	return m_hdi08.readRX(_inst);
		case HDI08::HDR:	return m_hdi08.readHDR();
		case HDI08::HDDR:	return m_hdi08.readHDDR();

		// Y-space ESAI registers
		case Esai::M_RCR_1:		return m_esai.readReceiveControlRegister();
		case Esai::M_RCCR_1:	return m_esai.readReceiveClockControlRegister();
		case Esai::M_SAISR_1:	return m_esai.readStatusRegister();
		case Esai::M_TCR_1:		return m_esai.readTransmitControlRegister();
		case Esai::M_TCCR_1:	return m_esai.readTransmitClockControlRegister();
		case Esai::M_RX0_1:
		case Esai::M_RX1_1:
		case Esai::M_RX2_1:
		case Esai::M_RX3_1:		return m_esai.readRX(_addr - Esai::M_RX0_1);
		case Esai::M_TSMA_1:	return m_esai.readTSMA();
		case Esai::M_TSMB_1:	return m_esai.readTSMB();
		case Esai::M_RSMA_1:	return m_esai.readRSMA();
		case Esai::M_RSMB_1:	return m_esai.readRSMB();
		case Esai::M_PCRC:		return m_portC.getControl();
		case Esai::M_PDRC:		return m_portC.dspRead();
		case Esai::M_PRRC:		return m_portC.getDirection();

		case XIO_PCTL:			return m_esaiClock.getPCTL();

		case XIO_IDR:			return 0x56311;

		case M_AAR0:
		case M_AAR1:
		case M_AAR2:
		case M_AAR3:
			return m_mem[_addr - XIO_Reserved_High_First];

		case XIO_DCR5: return m_dma.getDCR(5);
		case XIO_DCO5: return m_dma.getDCO(5);
		case XIO_DDR5: return m_dma.getDDR(5);
		case XIO_DSR5: return m_dma.getDSR(5);

		case XIO_DCR4: return m_dma.getDCR(4);
		case XIO_DCO4: return m_dma.getDCO(4);
		case XIO_DDR4: return m_dma.getDDR(4);
		case XIO_DSR4: return m_dma.getDSR(4);

		case XIO_DCR3: return m_dma.getDCR(3);
		case XIO_DCO3: return m_dma.getDCO(3);
		case XIO_DDR3: return m_dma.getDDR(3);
		case XIO_DSR3: return m_dma.getDSR(3);

		case XIO_DCR2: return m_dma.getDCR(2);
		case XIO_DCO2: return m_dma.getDCO(2);
		case XIO_DDR2: return m_dma.getDDR(2);
		case XIO_DSR2: return m_dma.getDSR(2);

		case XIO_DCR1: return m_dma.getDCR(1);
		case XIO_DCO1: return m_dma.getDCO(1);
		case XIO_DDR1: return m_dma.getDDR(1);
		case XIO_DSR1: return m_dma.getDSR(1);

		case XIO_DCR0: return m_dma.getDCR(0);
		case XIO_DCO0: return m_dma.getDCO(0);
		case XIO_DDR0: return m_dma.getDDR(0);
		case XIO_DSR0: return m_dma.getDSR(0);

		case XIO_DOR3: return m_dma.getDOR(3);
		case XIO_DOR2: return m_dma.getDOR(2);
		case XIO_DOR1: return m_dma.getDOR(1);
		case XIO_DOR0: return m_dma.getDOR(0);

		case XIO_DSTR: return m_dma.getDSTR();
		}

		return m_mem[_addr - XIO_Reserved_High_First];
	}

	const TWord* Peripherals56311::readAsPtr(const TWord _addr, Instruction _inst)
	{
		switch (_addr)
		{
		case Esai::M_PCRC:		return &m_portC.getControl();
		case Esai::M_PRRC:		return &m_portC.getDirection();
		case Esai::M_SAISR_1:	return &m_esai.readStatusRegister();

		case XIO_DCR5:			return &m_dma.getDCR(5);
		case XIO_DCO5:			return &m_dma.getDCO(5);
		case XIO_DDR5:			return &m_dma.getDDR(5);
		case XIO_DSR5:			return &m_dma.getDSR(5);

		case XIO_DCR4:			return &m_dma.getDCR(4);
		case XIO_DCO4:			return &m_dma.getDCO(4);
		case XIO_DDR4:			return &m_dma.getDDR(4);
		case XIO_DSR4:			return &m_dma.getDSR(4);

		case XIO_DCR3:			return &m_dma.getDCR(3);
		case XIO_DCO3:			return &m_dma.getDCO(3);
		case XIO_DDR3:			return &m_dma.getDDR(3);
		case XIO_DSR3:			return &m_dma.getDSR(3);

		case XIO_DCR2:			return &m_dma.getDCR(2);
		case XIO_DCO2:			return &m_dma.getDCO(2);
		case XIO_DDR2:			return &m_dma.getDDR(2);
		case XIO_DSR2:			return &m_dma.getDSR(2);

		case XIO_DCR1:			return &m_dma.getDCR(1);
		case XIO_DCO1:			return &m_dma.getDCO(1);
		case XIO_DDR1:			return &m_dma.getDDR(1);
		case XIO_DSR1:			return &m_dma.getDSR(1);

		case XIO_DCR0:			return &m_dma.getDCR(0);
		case XIO_DCO0:			return &m_dma.getDCO(0);
		case XIO_DDR0:			return &m_dma.getDDR(0);
		case XIO_DSR0:			return &m_dma.getDSR(0);

		case XIO_DSTR:			return &m_dma.getDSTR();
		}

		return nullptr;
	}

	void Peripherals56311::write(const TWord _addr, const TWord _val)
	{
		switch (_addr)
		{
		case HDI08::HSR:	m_hdi08.writeStatusRegister(_val);		return;
		case HDI08::HCR:	m_hdi08.writeControlRegister(_val);		return;
		case HDI08::HPCR:	m_hdi08.writePortControlRegister(_val);	return;
		case HDI08::HOTX:	m_hdi08.writeTX(_val);					return;
		case HDI08::HDR:	m_hdi08.writeHDR(_val);					return;
		case HDI08::HDDR:	m_hdi08.writeHDDR(_val);				return;

		// Y-space ESAI registers
		case Esai::M_SAISR_1:	m_esai.writestatusRegister(_val);				return;
		case Esai::M_SAICR_1:	m_esai.writeControlRegister(_val);				return;
		case Esai::M_RCR_1:		m_esai.writeReceiveControlRegister(_val);		return;
		case Esai::M_RCCR_1:	m_esai.writeReceiveClockControlRegister(_val);	return;
		case Esai::M_TCR_1:		m_esai.writeTransmitControlRegister(_val);		return;
		case Esai::M_TCCR_1:	m_esai.writeTransmitClockControlRegister(_val);	return;
		case Esai::M_TX0_1:
		case Esai::M_TX1_1:
		case Esai::M_TX2_1:
		case Esai::M_TX3_1:
		case Esai::M_TX4_1:
		case Esai::M_TX5_1:		m_esai.writeTX(_addr - Esai::M_TX0_1, _val);	return;
		case Esai::M_TSMA_1:	m_esai.writeTSMA(_val);							return;
		case Esai::M_TSMB_1:	m_esai.writeTSMB(_val);							return;
		case Esai::M_RSMA_1:	m_esai.writeRSMA(_val);							return;
		case Esai::M_RSMB_1:	m_esai.writeRSMB(_val);							return;
		case Esai::M_PCRC:		m_portC.setControl(_val);						return;
		case Esai::M_PDRC:		m_portC.dspWrite(_val);							return;
		case Esai::M_PRRC:		m_portC.setDirection(_val);						return;

		case XIO_PCTL:			m_esaiClock.setPCTL(_val);						return;

		case XIO_DCR5: m_dma.setDCR(5, _val); return;
		case XIO_DCO5: m_dma.setDCO(5, _val); return;
		case XIO_DDR5: m_dma.setDDR(5, _val); return;
		case XIO_DSR5: m_dma.setDSR(5, _val); return;

		case XIO_DCR4: m_dma.setDCR(4, _val); return;
		case XIO_DCO4: m_dma.setDCO(4, _val); return;
		case XIO_DDR4: m_dma.setDDR(4, _val); return;
		case XIO_DSR4: m_dma.setDSR(4, _val); return;

		case XIO_DCR3: m_dma.setDCR(3, _val); return;
		case XIO_DCO3: m_dma.setDCO(3, _val); return;
		case XIO_DDR3: m_dma.setDDR(3, _val); return;
		case XIO_DSR3: m_dma.setDSR(3, _val); return;

		case XIO_DCR2: m_dma.setDCR(2, _val); return;
		case XIO_DCO2: m_dma.setDCO(2, _val); return;
		case XIO_DDR2: m_dma.setDDR(2, _val); return;
		case XIO_DSR2: m_dma.setDSR(2, _val); return;

		case XIO_DCR1: m_dma.setDCR(1, _val); return;
		case XIO_DCO1: m_dma.setDCO(1, _val); return;
		case XIO_DDR1: m_dma.setDDR(1, _val); return;
		case XIO_DSR1: m_dma.setDSR(1, _val); return;

		case XIO_DCR0: m_dma.setDCR(0, _val); return;
		case XIO_DCO0: m_dma.setDCO(0, _val); return;
		case XIO_DDR0: m_dma.setDDR(0, _val); return;
		case XIO_DSR0: m_dma.setDSR(0, _val); return;

		case XIO_DOR3: m_dma.setDOR(3, _val); return;
		case XIO_DOR2: m_dma.setDOR(2, _val); return;
		case XIO_DOR1: m_dma.setDOR(1, _val); return;
		case XIO_DOR0: m_dma.setDOR(0, _val); return;

		default:
			break;
		}
		m_mem[_addr - XIO_Reserved_High_First] = _val;
	}

	uint32_t Peripherals56311::exec() noexcept
	{
		auto delay = m_esaiClock.exec();
		delay = std::min(delay, m_hdi08.exec());
		delay = std::min(delay, m_timers.exec());
		delay = std::min(delay, m_dma.exec());
		return delay;
	}

	void Peripherals56311::reset()
	{
		m_esai.reset();
		m_hdi08.reset();
	}

	void Peripherals56311::setSymbols(Disassembler& _disasm) const
	{
		auto addIR = [&](TWord _addr, const std::string& _symbol)
		{
			_disasm.addSymbol(Disassembler::MemP, _addr, "int_" + _symbol);
		};

		addIR(Vba_HardwareRESET, "reset");
		addIR(Vba_Stackerror, "stackerror");
		addIR(Vba_Illegalinstruction, "illegal");
		addIR(Vba_DebugRequest, "debug");
		addIR(Vba_Trap, "trap");
		addIR(Vba_NMI, "nmi");

		addIR(Vba_IRQA, "irqA");
		addIR(Vba_IRQB, "irqB");
		addIR(Vba_IRQC, "irqC");
		addIR(Vba_IRQD, "irqD");

		addIR(Vba_DMAchannel0, "dma0");
		addIR(Vba_DMAchannel1, "dma1");
		addIR(Vba_DMAchannel2, "dma2");
		addIR(Vba_DMAchannel3, "dma3");
		addIR(Vba_DMAchannel4, "dma4");
		addIR(Vba_DMAchannel5, "dma5");

		Esai::setSymbols(_disasm, MemArea_Y);
		HDI08::setSymbols(_disasm);
		m_timers.setSymbols(_disasm);
	}

	void Peripherals56311::terminate()
	{
		m_hdi08.terminate();
		m_esai.terminate();
	}

	void Peripherals56311::setDSP(DSP* _dsp)
	{
		IPeripherals::setDSP(_dsp);
		m_esaiClock.setDSP(_dsp);
		m_esai.setDSP(_dsp);
		m_timers.setDSP(_dsp);
	}
}
