#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>

namespace dsp56k
{
	// Host Digital Interface (HDI08) host-side register model
	// per Motorola DSP56367 User's Manual Rev. 2.1, Section 8.6 Table 8-8 (folio 8-19).
	class Hdi08HostPort
	{
	public:
		enum PeriphAddress : uint32_t
		{
			HdiICR     = 0,
			HdiCVR     = 1,
			HdiISR     = 2,
			HdiIVR     = 3,
			HdiUnused4 = 4,
			HdiTXH     = 5,
			HdiTXM     = 6,
			HdiTXL     = 7
		};

		enum IsrBits : uint8_t
		{
			Rxdf         = 1 << 0,
			Txde         = 1 << 1,
			Trdy         = 1 << 2,
			Hf2          = 1 << 3,
			Hf3          = 1 << 4,
			IsrReserved5 = 1 << 5,
			IsrReserved6 = 1 << 6,
			Hreq         = 1 << 7
		};

		enum IcrBits : uint8_t
		{
			Rreq  = 1 << 0,
			Treq  = 1 << 1,
			Hdrq  = 1 << 2,
			Hf0   = 1 << 3,
			Hf1   = 1 << 4,
			Hlend = 1 << 5,
			Hm0   = Hlend,
			Hm1   = 1 << 6,
			Init  = 1 << 7
		};

		enum CvrBits : uint8_t
		{
			Hv = 0x7f,
			Hc = 1 << 7
		};

		using CallbackRxEmpty  = std::function<void(bool)>;
		using CallbackWriteTx  = std::function<void(uint32_t)>;
		using CallbackWriteIrq = std::function<void(uint8_t)>;
		using CallbackReadIsr  = std::function<uint8_t(uint8_t)>;
		using CallbackInitHdi08 = std::function<void()>;
		using CallbackWriteIcr = std::function<void(uint8_t)>;

		Hdi08HostPort();
		virtual ~Hdi08HostPort() = default;

		uint8_t read8(PeriphAddress _addr);
		uint8_t read8(uint32_t _addr) { return read8(static_cast<PeriphAddress>(_addr & 7u)); }

		uint16_t read16(PeriphAddress _addr);
		uint16_t read16(uint32_t _addr) { return read16(static_cast<PeriphAddress>(_addr & 7u)); }

		void write8(PeriphAddress _addr, uint8_t _val);
		void write8(uint32_t _addr, uint8_t _val) { write8(static_cast<PeriphAddress>(_addr & 7u), _val); }

		void write16(PeriphAddress _addr, uint16_t _val);
		void write16(uint32_t _addr, uint16_t _val) { write16(static_cast<PeriphAddress>(_addr & 7u), _val); }

		void pollTx(std::deque<uint32_t>& _dst);
		bool pollInterruptRequest(uint8_t& _addr);

		void writeRx(uint32_t _word);
		void clearRx();

		void exec(uint32_t _deltaCycles);

		uint8_t isr() const;
		uint8_t icr() const { return m_icr; }
		void isr(uint8_t _isr) { m_isr = _isr; }
		void icr(uint8_t _icr) { m_icr = _icr; }

		bool canReceiveData() const { return (m_isr & Rxdf) == 0; }

		void setRxEmptyCallback(const CallbackRxEmpty& _cb);
		void setWriteTxCallback(const CallbackWriteTx& _cb);
		void setWriteIrqCallback(const CallbackWriteIrq& _cb);
		void setReadIsrCallback(const CallbackReadIsr& _cb);
		void setInitHdi08Callback(const CallbackInitHdi08& _cb);
		void setWriteIcrCallback(const CallbackWriteIcr& _cb);

	private:
		enum class WordByte : uint8_t
		{
			H = 0,
			M = 1,
			L = 2
		};

		uint8_t readRxByte(WordByte _byte);
		void writeTxByte(WordByte _byte, uint8_t _val);
		void pollRx();
		bool littleEndian() const { return (m_icr & Hlend) != 0; }

		uint8_t m_icr = 0;
		uint8_t m_cvr = 0;
		uint8_t m_isr = 0;
		uint8_t m_ivr = 0x0f;

		std::array<uint8_t, 3> m_txBytes{};
		uint32_t m_currentRxWord = 0;

		std::deque<uint32_t> m_txData;
		std::deque<uint32_t> m_rxData;
		std::deque<uint8_t> m_pendingInterruptRequests;

		CallbackRxEmpty m_rxEmptyCallback;
		CallbackWriteTx m_writeTxCallback;
		CallbackWriteIrq m_writeIrqCallback;
		CallbackReadIsr m_readIsrCallback;
		CallbackInitHdi08 m_initHdi08Callback;
		CallbackWriteIcr m_writeIcrCallback;

		uint32_t m_readTimeoutCycles = 0;
	};
}
