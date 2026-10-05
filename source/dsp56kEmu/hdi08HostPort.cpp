// Host Digital Interface (HDI08) host-side register model
// per Motorola DSP56367 User's Manual Rev. 2.1, Section 8.6 Table 8-8 (folio 8-19).

#include "hdi08HostPort.h"

namespace dsp56k
{
	Hdi08HostPort::Hdi08HostPort()
	{
		setRxEmptyCallback(nullptr);
		setWriteTxCallback(nullptr);
		setWriteIrqCallback(nullptr);
		setReadIsrCallback(nullptr);
		setInitHdi08Callback(nullptr);
		setWriteIcrCallback(nullptr);
	}

	uint8_t Hdi08HostPort::read8(const PeriphAddress _addr)
	{
		switch(_addr)
		{
		case HdiICR:
			return icr();
		case HdiISR:
			return isr();
		case HdiTXH:
			return readRxByte(WordByte::H);
		case HdiTXM:
			return readRxByte(WordByte::M);
		case HdiTXL:
			return readRxByte(WordByte::L);
		case HdiCVR:
			return m_cvr;
		case HdiIVR:
			return m_ivr;
		case HdiUnused4:
			return 0;
		}
		return 0;
	}

	uint16_t Hdi08HostPort::read16(const PeriphAddress _addr)
	{
		switch(_addr)
		{
		case HdiUnused4:
			return read8(HdiTXH);
		case HdiTXM:
			{
				uint16_t r = static_cast<uint16_t>(read8(HdiTXM)) << 8;
				r |= static_cast<uint16_t>(read8(HdiTXL));
				return r;
			}
		default:
			break;
		}
		return (static_cast<uint16_t>(read8(_addr)) << 8)
			| static_cast<uint16_t>(read8(static_cast<PeriphAddress>((_addr + 1) & 7u)));
	}

	void Hdi08HostPort::write8(const PeriphAddress _addr, const uint8_t _val)
	{
		switch(_addr)
		{
		case HdiISR:
			m_isr = _val;
			return;
		case HdiICR:
			m_icr = _val;
			m_writeIcrCallback(_val);
			if(_val & Init)
				m_initHdi08Callback();
			return;
		case HdiCVR:
			m_cvr = _val;
			if(_val & Hc)
			{
				const uint8_t addr = static_cast<uint8_t>((_val & Hv) << 1);
				m_writeIrqCallback(addr);
				m_cvr &= ~Hc;
			}
			return;
		case HdiIVR:
			m_ivr = _val;
			return;
		case HdiTXH:
			writeTxByte(WordByte::H, _val);
			return;
		case HdiTXM:
			writeTxByte(WordByte::M, _val);
			return;
		case HdiTXL:
			writeTxByte(WordByte::L, _val);
			return;
		case HdiUnused4:
			return;
		}
	}

	void Hdi08HostPort::write16(const PeriphAddress _addr, const uint16_t _val)
	{
		switch(_addr)
		{
		case HdiUnused4:
			write8(HdiTXH, static_cast<uint8_t>(_val & 0xff));
			return;
		case HdiTXM:
			write8(HdiTXM, static_cast<uint8_t>(_val >> 8));
			write8(HdiTXL, static_cast<uint8_t>(_val & 0xff));
			return;
		default:
			break;
		}
		write8(_addr, static_cast<uint8_t>(_val >> 8));
		write8(static_cast<PeriphAddress>((_addr + 1) & 7u), static_cast<uint8_t>(_val & 0xff));
	}

	void Hdi08HostPort::pollTx(std::deque<uint32_t>& _dst)
	{
		std::swap(_dst, m_txData);
	}

	bool Hdi08HostPort::pollInterruptRequest(uint8_t& _addr)
	{
		if(m_pendingInterruptRequests.empty())
			return false;

		_addr = m_pendingInterruptRequests.front();
		m_pendingInterruptRequests.pop_front();
		return true;
	}

	void Hdi08HostPort::writeRx(const uint32_t _word)
	{
		m_rxData.push_back(_word);
		if(!(isr() & Rxdf))
			pollRx();
	}

	void Hdi08HostPort::clearRx()
	{
		m_rxData.clear();
		m_currentRxWord = 0;
		m_isr &= ~Rxdf;
	}

	void Hdi08HostPort::exec(const uint32_t _deltaCycles)
	{
		if(!(m_isr & Rxdf) || m_rxData.empty())
			return;

		m_readTimeoutCycles += _deltaCycles;
		if(m_readTimeoutCycles >= 50)
		{
			m_isr &= ~Rxdf;
			pollRx();
		}
	}

	uint8_t Hdi08HostPort::isr() const
	{
		uint8_t isrVal = m_isr;
		isrVal |= Txde;
		return m_readIsrCallback(isrVal);
	}

	void Hdi08HostPort::setRxEmptyCallback(const CallbackRxEmpty& _cb)
	{
		m_rxEmptyCallback = _cb ? _cb : [](bool) {};
	}

	void Hdi08HostPort::setWriteTxCallback(const CallbackWriteTx& _cb)
	{
		m_writeTxCallback = _cb ? _cb : [this](const uint32_t _word) { m_txData.push_back(_word); };
	}

	void Hdi08HostPort::setWriteIrqCallback(const CallbackWriteIrq& _cb)
	{
		m_writeIrqCallback = _cb ? _cb : [this](const uint8_t _irq) { m_pendingInterruptRequests.push_back(_irq); };
	}

	void Hdi08HostPort::setReadIsrCallback(const CallbackReadIsr& _cb)
	{
		m_readIsrCallback = _cb ? _cb : [](const uint8_t _isr) { return _isr; };
	}

	void Hdi08HostPort::setInitHdi08Callback(const CallbackInitHdi08& _cb)
	{
		m_initHdi08Callback = _cb ? _cb : [] {};
	}

	void Hdi08HostPort::setWriteIcrCallback(const CallbackWriteIcr& _cb)
	{
		m_writeIcrCallback = _cb ? _cb : [](uint8_t) {};
	}

	uint8_t Hdi08HostPort::readRxByte(const WordByte _byte)
	{
		const bool hasRX = (isr() & Rxdf) != 0;

		if(!hasRX)
		{
			const auto firstByte = WordByte::H;
			if(_byte == firstByte)
				m_rxEmptyCallback(true);

			if(!(isr() & Rxdf))
				return 0;
		}

		const auto word = m_currentRxWord;
		std::array<uint8_t, 3> bytes{};

		if(littleEndian())
		{
			bytes[0] = static_cast<uint8_t>(word & 0xff);
			bytes[1] = static_cast<uint8_t>((word >> 8) & 0xff);
			bytes[2] = static_cast<uint8_t>((word >> 16) & 0xff);
		}
		else
		{
			bytes[0] = static_cast<uint8_t>((word >> 16) & 0xff);
			bytes[1] = static_cast<uint8_t>((word >> 8) & 0xff);
			bytes[2] = static_cast<uint8_t>(word & 0xff);
		}

		if(_byte == WordByte::L)
		{
			m_isr &= ~Rxdf;
			m_rxEmptyCallback(false);
			if(!(m_isr & Rxdf))
				pollRx();
		}

		return bytes[static_cast<size_t>(_byte)];
	}

	void Hdi08HostPort::writeTxByte(const WordByte _byte, const uint8_t _val)
	{
		m_txBytes[static_cast<size_t>(_byte)] = _val;
		if(_byte != WordByte::L)
			return;

		const uint32_t h = m_txBytes[0];
		const uint32_t m = m_txBytes[1];
		const uint32_t l = m_txBytes[2];

		const uint32_t word = littleEndian()
			? ((l << 16) | (m << 8) | h)
			: ((h << 16) | (m << 8) | l);

		m_writeTxCallback(word);
		m_txBytes.fill(0);
	}

	void Hdi08HostPort::pollRx()
	{
		if(m_rxData.empty())
			return;

		m_readTimeoutCycles = 0;
		m_currentRxWord = m_rxData.front();
		m_rxData.pop_front();
		m_isr |= Rxdf;
	}
}
