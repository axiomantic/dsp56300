// An unsupported DMA address generation mode must be REPORTED, in
// every build type, and must not claim the block completed.
//
// The unsupported-mode limb of execTransfer was `assert(false && "DMA
// transfer mode not supported yet"); return true;`. NDEBUG deletes the
// assert, so a Release build dropped the transfer in complete silence and
// then told its caller the block had finished - which clears DE and fires
// the transfer-done interrupt for a transfer that never happened. A
// mechanism that fails silently fails exactly like a missing one.
//
// The report goes through LOG, the same facility esai.cpp uses for its
// transmit underrun. LOG has no NDEBUG guard: it formats into a stringstream
// and hands the result to Logging::g_logToConsole, whose target is
// replaceable through Logging::setLogFunc. That replacement is what this
// test reads the message out of, and it is also why the message is asserted
// character for character rather than searched for.
//
// The mode under test is source AGM 100 and destination AGM 100, both "no
// update". It is deliberately NOT the two-dimensional source mode this
// branch implements: a test that
// reported on an implemented mode would go green the moment support
// arrived and stop guarding anything.

#include "dsp56kEmu/dsp.h"
#include "dsp56kEmu/memory.h"
#include "dsp56kEmu/peripherals56311.h"
#include "dsp56kEmu/unittests.h"

#include "dsp56kBase/logging.h"

#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

namespace
{
	using namespace dsp56k;
	using AddressGenMode = DmaChannel::AddressGenMode;

	constexpr uint32_t g_frameRate96k = 96000;

	constexpr TWord g_channel = 2;
	constexpr TWord g_source = 0x001000;
	constexpr TWord g_destination = 0x002000;
	constexpr TWord g_payload = 0x123456;

	constexpr TWord g_hwEsaiTransmitData = 12;

	DefaultMemoryValidator g_memoryValidator;

	std::vector<std::string> g_captured;

	void captureLog(const std::string& _line)
	{
		g_captured.push_back(_line);
	}

	// logging.h does not declare the library's own default sink, so the
	// restore path installs an equivalent one. Installing g_logToConsole
	// itself would recurse: that function is what dispatches to g_logFunc.
	void logToStdout(const std::string& _line)
	{
		std::cout << _line << std::endl;
	}

	// Source AGM 4 and destination AGM 4, both SingleCounterAnoUpdate. Line
	// transfer triggered by request, both spaces X, DE set.
	constexpr TWord dcrUnsupported()
	{
		return (1u << DmaChannel::De)
			| (static_cast<TWord>(DmaChannel::TransferMode::LineTriggerRequestClearDE) << 19)
			| (g_hwEsaiTransmitData << 11)
			| (static_cast<TWord>(AddressGenMode::SingleCounterAnoUpdate) << 7)
			| (static_cast<TWord>(AddressGenMode::SingleCounterAnoUpdate) << 4);
	}

	// LOG prefixes every line with the emitting function and its line number.
	// The line number moves whenever dma.cpp is edited above the report, so it
	// is read out of the captured text - but it is read as a complete field,
	// checked to be a non-empty run of digits, and then put back. The final
	// comparison pins every other character of the line, including the
	// function name.
	void verifyLogLine(const std::string& _line, const std::string& _expectedBody)
	{
		const std::string prefix = "execTransfer@";

		verify(_line.size() > prefix.size());
		verify(_line.compare(0, prefix.size(), prefix) == 0);

		const auto separator = _line.find(": ", prefix.size());
		verify(separator != std::string::npos);

		const std::string lineNumber = _line.substr(prefix.size(), separator - prefix.size());

		verify(!lineNumber.empty());
		verify(lineNumber.find_first_not_of("0123456789") == std::string::npos);

		verify(_line == prefix + lineNumber + ": " + _expectedBody);
	}

	void unsupportedModeIsReportedAndDoesNotClaimCompletion()
	{
		verify(dcrUnsupported() == 0x906240);
		verify(((dcrUnsupported() >> 4) & 0x3f) == 0x24);

		Peripherals56311 p(g_frameRate96k);
		Memory mem(g_memoryValidator, 0x080000, 0x800000, 0x200000);
		DSP dsp(mem, &p, &p.ySpace());

		// ESAI sets M_TDE in its constructor, so arm() triggers immediately.
		verify(p.getEsai().readStatusRegister() & (1 << Esai::M_TDE));

		mem.set(MemArea_X, g_source, g_payload);
		mem.set(MemArea_X, g_destination, 0);

		auto& dma = p.getDMA();

		dma.setDOR(0, 0);
		dma.setDSR(g_channel, g_source);
		dma.setDDR(g_channel, g_destination);
		dma.setDCO(g_channel, 0);

		g_captured.clear();
		Logging::setLogFunc(&captureLog);
		dma.setDCR(g_channel, dcrUnsupported());
		Logging::setLogFunc(&logToStdout);

		verify(g_captured.size() == 1);

		verifyLogLine(g_captured[0],
			"DMA channel 2 unsupported address generation mode, DCR is 906240, DAM is 24, no transfer performed");

		// Nothing was transferred, so nothing may have been written.
		verify(mem.get(MemArea_X, g_destination) == 0);
		verify(mem.get(MemArea_X, g_source) == g_payload);

		// DE is still set. This transfer mode clears DE when a block completes,
		// so an unchanged DCR is the observable proof that execTransfer did NOT
		// answer "finished" and finishTransfer never ran - no cleared enable
		// bit and no transfer-done interrupt for a transfer that did not happen.
		verify(dma.getDCR(g_channel) == 0x906240);

		// The addresses did not move either.
		verify(dma.getDSR(g_channel) == g_source);
		verify(dma.getDDR(g_channel) == g_destination);
	}
}

int main()
{
	try
	{
		unsupportedModeIsReportedAndDoesNotClaimCompletion();
	}
	catch(const std::string& _err)
	{
		Logging::setLogFunc(&logToStdout);
		std::cout << "dsp56k_dma_unsupported_mode_report FAILED: " << _err << std::endl;
		return -1;
	}

	std::cout << "dsp56k_dma_unsupported_mode_report passed" << std::endl;
	return 0;
}
