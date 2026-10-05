// Host Digital Interface (HDI08) host-side register model unit tests.
// Exercises register access, 24-bit word assembly, endianness, interrupt generation,
// and callbacks per Motorola DSP56367 User's Manual Rev. 2.1, Section 8.6 Table 8-8 (folio 8-19).

#include "dsp56kEmu/hdi08HostPort.h"
#include "dsp56kEmu/unittests.h"

#include <iostream>
#include <vector>

namespace
{
	using namespace dsp56k;

	void defaultStateMatchesSpecification()
	{
		Hdi08HostPort port;

		verify((port.read8(Hdi08HostPort::HdiISR) & Hdi08HostPort::Txde) != 0);
		verify((port.read8(Hdi08HostPort::HdiISR) & Hdi08HostPort::Rxdf) == 0);
		verify(port.read8(Hdi08HostPort::HdiIVR) == 0x0f);
		verify(port.read8(Hdi08HostPort::HdiICR) == 0x00);
		verify(port.read8(Hdi08HostPort::HdiUnused4) == 0x00);
		verify(port.canReceiveData());
	}

	void wordAssemblyBigEndian()
	{
		Hdi08HostPort port;
		std::vector<uint32_t> words;

		port.setWriteTxCallback([&words](const uint32_t w)
		{
			words.push_back(w);
		});

		port.write8(Hdi08HostPort::HdiTXH, 0x12);
		verify(words.empty());

		port.write8(Hdi08HostPort::HdiTXM, 0x34);
		verify(words.empty());

		port.write8(Hdi08HostPort::HdiTXL, 0x56);
		verify(words.size() == 1);
		verify(words[0] == 0x123456);
	}

	void wordAssemblyLittleEndian()
	{
		Hdi08HostPort port;
		std::vector<uint32_t> words;

		port.write8(Hdi08HostPort::HdiICR, Hdi08HostPort::Hlend);

		port.setWriteTxCallback([&words](const uint32_t w)
		{
			words.push_back(w);
		});

		// In little-endian mode: TXH is low byte (bits 0..7), TXM is mid byte (bits 8..15), TXL is high byte (bits 16..23)
		port.write8(Hdi08HostPort::HdiTXH, 0x12);
		port.write8(Hdi08HostPort::HdiTXM, 0x34);
		port.write8(Hdi08HostPort::HdiTXL, 0x56);

		verify(words.size() == 1);
		verify(words[0] == 0x563412);
	}

	void hostCommandInterruptGeneration()
	{
		Hdi08HostPort port;
		uint8_t receivedVector = 0;
		bool irqFired = false;

		port.setWriteIrqCallback([&](const uint8_t vec)
		{
			irqFired = true;
			receivedVector = vec;
		});

		// Write CVR with HC=1 and vector index 0x12 (offset address is (vec & 0x7f) << 1)
		port.write8(Hdi08HostPort::HdiCVR, Hdi08HostPort::Hc | 0x12);

		verify(irqFired);
		verify(receivedVector == (0x12 << 1));
		verify((port.read8(Hdi08HostPort::HdiCVR) & Hdi08HostPort::Hc) == 0);
	}

	void receiveDataAndFifoRead()
	{
		Hdi08HostPort port;

		verify(port.canReceiveData());

		port.writeRx(0xabcdef);
		verify(!port.canReceiveData());
		verify((port.isr() & Hdi08HostPort::Rxdf) != 0);

		const uint8_t h = port.read8(Hdi08HostPort::HdiTXH);
		verify((port.isr() & Hdi08HostPort::Rxdf) != 0);
		verify(!port.canReceiveData());

		const uint8_t m = port.read8(Hdi08HostPort::HdiTXM);
		verify((port.isr() & Hdi08HostPort::Rxdf) != 0);
		verify(!port.canReceiveData());

		const uint8_t l = port.read8(Hdi08HostPort::HdiTXL);

		verify(h == 0xab);
		verify(m == 0xcd);
		verify(l == 0xef);

		// After reading lowest byte (+7), RXDF clears
		verify((port.isr() & Hdi08HostPort::Rxdf) == 0);
		verify(port.canReceiveData());
	}

	void receiveDataLittleEndian()
	{
		Hdi08HostPort port;

		port.write8(Hdi08HostPort::HdiICR, Hdi08HostPort::Hlend);

		verify(port.canReceiveData());

		port.writeRx(0xabcdef);
		verify(!port.canReceiveData());
		verify((port.isr() & Hdi08HostPort::Rxdf) != 0);

		// In HLEND=1 mode per Table 8-8:
		// +5 (HdiTXH) is RXL: low byte (0xef)
		// +6 (HdiTXM) is RXM: mid byte (0xcd)
		// +7 (HdiTXL) is RXH: high byte (0xab)
		const uint8_t b5 = port.read8(Hdi08HostPort::HdiTXH);
		verify((port.isr() & Hdi08HostPort::Rxdf) != 0);
		verify(!port.canReceiveData());
		verify(b5 == 0xef);

		const uint8_t b6 = port.read8(Hdi08HostPort::HdiTXM);
		verify((port.isr() & Hdi08HostPort::Rxdf) != 0);
		verify(!port.canReceiveData());
		verify(b6 == 0xcd);

		const uint8_t b7 = port.read8(Hdi08HostPort::HdiTXL);
		verify(b7 == 0xab);

		// Rxdf clears only after reading the last byte (+7)
		verify((port.isr() & Hdi08HostPort::Rxdf) == 0);
		verify(port.canReceiveData());
	}

	void unusedOffset4ReadsZero()
	{
		Hdi08HostPort port;
		verify(port.read8(Hdi08HostPort::HdiUnused4) == 0x00);
	}

	void initSoftwareResetClearsInit()
	{
		Hdi08HostPort port;
		bool initFired = false;

		port.setInitHdi08Callback([&initFired]
		{
			initFired = true;
		});

		port.write8(Hdi08HostPort::HdiICR, Hdi08HostPort::Init | Hdi08HostPort::Hf0);
		verify(initFired);
	}
}

int main()
{
	try
	{
		defaultStateMatchesSpecification();
		unusedOffset4ReadsZero();
		wordAssemblyBigEndian();
		wordAssemblyLittleEndian();
		hostCommandInterruptGeneration();
		receiveDataAndFifoRead();
		receiveDataLittleEndian();
		initSoftwareResetClearsInit();
	}
	catch(const std::string& _err)
	{
		std::cout << "dsp56k_hdi08_host_port FAILED: " << _err << std::endl;
		return -1;
	}

	std::cout << "dsp56k_hdi08_host_port passed" << std::endl;
	return 0;
}
