#include "peripherals56311.h"

#include "aar.h"
#include "disasm.h"
#include "dsp.h"
#include "interrupts.h"

#include <algorithm>

namespace dsp56k
{
	namespace
	{
		// The fall-through of every face subtracts XIO_Reserved_High_First. An
		// address below that window would underflow the subtraction and index
		// out of bounds, so every fall-through goes through this test first.
		constexpr bool isPeripheralWindow(const TWord _addr)
		{
			return _addr >= XIO_Reserved_High_First && _addr <= XIO_Reserved_High_Last;
		}
	}

	// _________________________________________________________________________
	// Peripherals56311Y - the Y-space face
	//
	Peripherals56311Y::Peripherals56311Y(Esai& _esai, const uint32_t _frameRateHz)
		: IPeripherals(PeripheralType::Peripherals56311)
		, m_esai(_esai)
		, m_frameRateHz(_frameRateHz)
		, m_mem{}
	{
		m_mem.fill(0);
	}

	TWord Peripherals56311Y::read(const TWord _addr, Instruction /*_inst*/)
	{
		switch (_addr)
		{
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
		default:
			break;
		}

		if(!isPeripheralWindow(_addr))
			return 0;

		return m_mem[_addr - XIO_Reserved_High_First];
	}

	const TWord* Peripherals56311Y::readAsPtr(const TWord _addr, Instruction /*_inst*/)
	{
		switch (_addr)
		{
		case Esai::M_SAISR_1:	return &m_esai.readStatusRegister();
		default:
			return nullptr;
		}
	}

	void Peripherals56311Y::write(const TWord _addr, const TWord _val)
	{
		switch (_addr)
		{
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
		default:
			break;
		}

		if(!isPeripheralWindow(_addr))
			return;

		m_mem[_addr - XIO_Reserved_High_First] = _val;
	}

	void Peripherals56311Y::reset()
	{
		// The ESAI_1 instance belongs to the X-space face, which resets it.
		m_mem.fill(0);
	}

	void Peripherals56311Y::setSymbols(Disassembler& _disasm) const
	{
		Esai::setSymbols(_disasm, MemArea_Y);
	}

	void Peripherals56311Y::terminate()
	{
		// The X-space face owns every block and terminates all of them.
	}

	// _________________________________________________________________________
	// Peripherals56311 - the X-space face
	//
	Peripherals56311::Peripherals56311(const uint32_t _secondBusFrameRateHz)
		: IPeripherals(PeripheralType::Peripherals56311)
		, m_mem{}
		, m_dma(*this)
		, m_esaiX(*this, MemArea_X, &m_dma)
		, m_esaiY(*this, MemArea_Y)
		, m_hdi08(*this)
		, m_timers(*this, Vba_TIMER0_Compare)
		, m_ySpace(m_esaiY, _secondBusFrameRateHz)
	{
		m_mem.fill(0);
	}

	TWord Peripherals56311::read(const TWord _addr, const Instruction _inst)
	{
		switch (_addr)
		{
		case HDI08::HSR:		return m_hdi08.readStatusRegister();
		case HDI08::HCR:		return m_hdi08.readControlRegister();
		case HDI08::HPCR:		return m_hdi08.readPortControlRegister();
		case HDI08::HORX:		return m_hdi08.readRX(_inst);
		case HDI08::HDR:		return m_hdi08.readHDR();
		case HDI08::HDDR:		return m_hdi08.readHDDR();

		case Esai::M_RCR:		return m_esaiX.readReceiveControlRegister();
		case Esai::M_RCCR:		return m_esaiX.readReceiveClockControlRegister();
		case Esai::M_SAISR:		return m_esaiX.readStatusRegister();
		case Esai::M_TCR:		return m_esaiX.readTransmitControlRegister();
		case Esai::M_TCCR:		return m_esaiX.readTransmitClockControlRegister();
		case Esai::M_RX0:
		case Esai::M_RX1:
		case Esai::M_RX2:
		case Esai::M_RX3:		return m_esaiX.readRX(_addr - Esai::M_RX0);
		case Esai::M_TSMA:		return m_esaiX.readTSMA();
		case Esai::M_TSMB:		return m_esaiX.readTSMB();
		case Esai::M_RSMA:		return m_esaiX.readRSMA();
		case Esai::M_RSMB:		return m_esaiX.readRSMB();

		case Timers::M_TCSR0:	return m_timers.readTCSR(0);
		case Timers::M_TCSR1:	return m_timers.readTCSR(1);
		case Timers::M_TCSR2:	return m_timers.readTCSR(2);
		case Timers::M_TLR0:	return m_timers.readTLR(0);
		case Timers::M_TLR1:	return m_timers.readTLR(1);
		case Timers::M_TLR2:	return m_timers.readTLR(2);
		case Timers::M_TCPR0:	return m_timers.readTCPR(0);
		case Timers::M_TCPR1:	return m_timers.readTCPR(1);
		case Timers::M_TCPR2:	return m_timers.readTCPR(2);
		case Timers::M_TCR0:	return m_timers.readTCR(0);
		case Timers::M_TCR1:	return m_timers.readTCR(1);
		case Timers::M_TCR2:	return m_timers.readTCR(2);
		case Timers::M_TPLR:	return m_timers.readTPLR();
		case Timers::M_TPCR:	return m_timers.readTPCR();

		case XIO_IDR:			return 0x56311;

		case XIO_DCR5:			return m_dma.getDCR(5);
		case XIO_DCO5:			return m_dma.getDCO(5);
		case XIO_DDR5:			return m_dma.getDDR(5);
		case XIO_DSR5:			return m_dma.getDSR(5);

		case XIO_DCR4:			return m_dma.getDCR(4);
		case XIO_DCO4:			return m_dma.getDCO(4);
		case XIO_DDR4:			return m_dma.getDDR(4);
		case XIO_DSR4:			return m_dma.getDSR(4);

		case XIO_DCR3:			return m_dma.getDCR(3);
		case XIO_DCO3:			return m_dma.getDCO(3);
		case XIO_DDR3:			return m_dma.getDDR(3);
		case XIO_DSR3:			return m_dma.getDSR(3);

		case XIO_DCR2:			return m_dma.getDCR(2);
		case XIO_DCO2:			return m_dma.getDCO(2);
		case XIO_DDR2:			return m_dma.getDDR(2);
		case XIO_DSR2:			return m_dma.getDSR(2);

		case XIO_DCR1:			return m_dma.getDCR(1);
		case XIO_DCO1:			return m_dma.getDCO(1);
		case XIO_DDR1:			return m_dma.getDDR(1);
		case XIO_DSR1:			return m_dma.getDSR(1);

		case XIO_DCR0:			return m_dma.getDCR(0);
		case XIO_DCO0:			return m_dma.getDCO(0);
		case XIO_DDR0:			return m_dma.getDDR(0);
		case XIO_DSR0:			return m_dma.getDSR(0);

		case XIO_DOR3:			return m_dma.getDOR(3);
		case XIO_DOR2:			return m_dma.getDOR(2);
		case XIO_DOR1:			return m_dma.getDOR(1);
		case XIO_DOR0:			return m_dma.getDOR(0);

		case XIO_DSTR:			return m_dma.getDSTR();

		default:
			break;
		}

		if(!isPeripheralWindow(_addr))
			return 0;

		return m_mem[_addr - XIO_Reserved_High_First];
	}

	const TWord* Peripherals56311::readAsPtr(const TWord _addr, Instruction /*_inst*/)
	{
		switch (_addr)
		{
		case Esai::M_SAISR:		return &m_esaiX.readStatusRegister();

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

		default:
			return nullptr;
		}
	}

	void Peripherals56311::write(const TWord _addr, const TWord _val)
	{
		switch (_addr)
		{
		case HDI08::HSR:		m_hdi08.writeStatusRegister(_val);					return;
		case HDI08::HCR:		m_hdi08.writeControlRegister(_val);					return;
		case HDI08::HPCR:		m_hdi08.writePortControlRegister(_val);				return;
		case HDI08::HOTX:		m_hdi08.writeTX(_val);								return;
		case HDI08::HDR:		m_hdi08.writeHDR(_val);								return;
		case HDI08::HDDR:		m_hdi08.writeHDDR(_val);							return;

		case Esai::M_SAISR:		m_esaiX.writestatusRegister(_val);					return;
		case Esai::M_SAICR:		m_esaiX.writeControlRegister(_val);					return;
		case Esai::M_RCR:		m_esaiX.writeReceiveControlRegister(_val);			return;
		case Esai::M_RCCR:		m_esaiX.writeReceiveClockControlRegister(_val);		return;
		case Esai::M_TCR:		m_esaiX.writeTransmitControlRegister(_val);			return;
		case Esai::M_TCCR:		m_esaiX.writeTransmitClockControlRegister(_val);		return;
		case Esai::M_TX0:
		case Esai::M_TX1:
		case Esai::M_TX2:
		case Esai::M_TX3:
		case Esai::M_TX4:
		case Esai::M_TX5:		m_esaiX.writeTX(_addr - Esai::M_TX0, _val);			return;
		case Esai::M_TSMA:		m_esaiX.writeTSMA(_val);							return;
		case Esai::M_TSMB:		m_esaiX.writeTSMB(_val);							return;
		case Esai::M_RSMA:		m_esaiX.writeRSMA(_val);							return;
		case Esai::M_RSMB:		m_esaiX.writeRSMB(_val);							return;

		case Timers::M_TCSR0:	m_timers.writeTCSR(0, _val);						return;
		case Timers::M_TCSR1:	m_timers.writeTCSR(1, _val);						return;
		case Timers::M_TCSR2:	m_timers.writeTCSR(2, _val);						return;
		case Timers::M_TLR0:	m_timers.writeTLR(0, _val);							return;
		case Timers::M_TLR1:	m_timers.writeTLR(1, _val);							return;
		case Timers::M_TLR2:	m_timers.writeTLR(2, _val);							return;
		case Timers::M_TCPR0:	m_timers.writeTCPR(0, _val);						return;
		case Timers::M_TCPR1:	m_timers.writeTCPR(1, _val);						return;
		case Timers::M_TCPR2:	m_timers.writeTCPR(2, _val);						return;
		case Timers::M_TCR0:	m_timers.writeTCR(0, _val);							return;
		case Timers::M_TCR1:	m_timers.writeTCR(1, _val);							return;
		case Timers::M_TCR2:	m_timers.writeTCR(2, _val);							return;
		case Timers::M_TPLR:	m_timers.writeTPLR(_val);							return;
		case Timers::M_TPCR:	m_timers.writeTPCR(_val);							return;

		case XIO_DCR5:			m_dma.setDCR(5, _val);								return;
		case XIO_DCO5:			m_dma.setDCO(5, _val);								return;
		case XIO_DDR5:			m_dma.setDDR(5, _val);								return;
		case XIO_DSR5:			m_dma.setDSR(5, _val);								return;

		case XIO_DCR4:			m_dma.setDCR(4, _val);								return;
		case XIO_DCO4:			m_dma.setDCO(4, _val);								return;
		case XIO_DDR4:			m_dma.setDDR(4, _val);								return;
		case XIO_DSR4:			m_dma.setDSR(4, _val);								return;

		case XIO_DCR3:			m_dma.setDCR(3, _val);								return;
		case XIO_DCO3:			m_dma.setDCO(3, _val);								return;
		case XIO_DDR3:			m_dma.setDDR(3, _val);								return;
		case XIO_DSR3:			m_dma.setDSR(3, _val);								return;

		case XIO_DCR2:			m_dma.setDCR(2, _val);								return;
		case XIO_DCO2:			m_dma.setDCO(2, _val);								return;
		case XIO_DDR2:			m_dma.setDDR(2, _val);								return;
		case XIO_DSR2:			m_dma.setDSR(2, _val);								return;

		case XIO_DCR1:			m_dma.setDCR(1, _val);								return;
		case XIO_DCO1:			m_dma.setDCO(1, _val);								return;
		case XIO_DDR1:			m_dma.setDDR(1, _val);								return;
		case XIO_DSR1:			m_dma.setDSR(1, _val);								return;

		case XIO_DCR0:			m_dma.setDCR(0, _val);								return;
		case XIO_DCO0:			m_dma.setDCO(0, _val);								return;
		case XIO_DDR0:			m_dma.setDDR(0, _val);								return;
		case XIO_DSR0:			m_dma.setDSR(0, _val);								return;

		case XIO_DOR3:			m_dma.setDOR(3, _val);								return;
		case XIO_DOR2:			m_dma.setDOR(2, _val);								return;
		case XIO_DOR1:			m_dma.setDOR(1, _val);								return;
		case XIO_DOR0:			m_dma.setDOR(0, _val);								return;

		default:
			break;
		}

		if(!isPeripheralWindow(_addr))
			return;

		m_mem[_addr - XIO_Reserved_High_First] = _val;
	}

	// No EsaiClock and no ESAI here. The scheduler drives Esai::execTX and
	// Esai::execRX for both ports.
	uint32_t Peripherals56311::exec() noexcept
	{
		auto delay = m_hdi08.exec();
		delay = std::min(delay, m_timers.exec());
		delay = std::min(delay, m_dma.exec());
		return delay;
	}

	void Peripherals56311::reset()
	{
		m_esaiX.reset();
		m_esaiY.reset();
		m_hdi08.reset();
	}

	void Peripherals56311::setDSP(DSP* _dsp)
	{
		IPeripherals::setDSP(_dsp);

		m_esaiX.setDSP(_dsp);
		m_esaiY.setDSP(_dsp);
		m_timers.setDSP(_dsp);
	}

	void Peripherals56311::setSymbols(Disassembler& _disasm) const
	{
		auto addIR = [&](const TWord _addr, const std::string& _symbol)
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

		Esai::setSymbols(_disasm, MemArea_X);
		HDI08::setSymbols(_disasm);
		m_timers.setSymbols(_disasm);
	}

	void Peripherals56311::terminate()
	{
		m_hdi08.terminate();
		m_esaiX.terminate();
		m_esaiY.terminate();
	}
}
