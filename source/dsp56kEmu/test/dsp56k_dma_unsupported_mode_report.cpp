// Verify that unsupported DMA address generation modes report an error via LOG
// and do not complete the transfer or clear DE.

#include "dsp56kEmu/dsp.h"
#include "dsp56kEmu/memory.h"
#include "dsp56kEmu/peripherals.h"
#include "dsp56kEmu/unittests.h"

#include "esaitransmitready.h"

#include "dsp56kBase/logging.h"

#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

namespace {
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

void captureLog(const std::string &_line) { g_captured.push_back(_line); }

// logging.h does not declare the library's own default sink, so the
// restore path installs an equivalent one. Installing g_logToConsole
// itself would recurse: that function is what dispatches to g_logFunc.
void logToStdout(const std::string &_line) { std::cout << _line << std::endl; }

// Source AGM 6 and destination AGM 6, both reserved encodings of the field.
// Line transfer triggered by request, both spaces X, DE set.
constexpr TWord dcrUnsupported() {
  return (1u << DmaChannel::De) |
         (static_cast<TWord>(
              DmaChannel::TransferMode::LineTriggerRequestClearDE)
          << 19) |
         (g_hwEsaiTransmitData << 11) |
         (static_cast<TWord>(AddressGenMode::reserved110) << 7) |
         (static_cast<TWord>(AddressGenMode::reserved110) << 4);
}

// LOG prefixes every line with the emitting function and its line number.
// The line number moves whenever dma.cpp is edited above the report, so it
// is read out of the captured text - but it is read as a complete field,
// checked to be a non-empty run of digits, and then put back. The final
// comparison pins every other character of the line, including the
// function name.
void verifyLogLine(const std::string &_line, const std::string &_expectedBody) {
  const std::string prefix = "execTransfer@";

  verify(_line.size() > prefix.size());
  verify(_line.compare(0, prefix.size(), prefix) == 0);

  const auto separator = _line.find(": ", prefix.size());
  verify(separator != std::string::npos);

  const std::string lineNumber =
      _line.substr(prefix.size(), separator - prefix.size());

  verify(!lineNumber.empty());
  verify(lineNumber.find_first_not_of("0123456789") == std::string::npos);

  verify(_line == prefix + lineNumber + ": " + _expectedBody);
}

void unsupportedModeIsReportedAndDoesNotClaimCompletion() {
  verify(dcrUnsupported() == 0x906360);
  verify(((dcrUnsupported() >> 4) & 0x3f) == 0x36);

  Peripherals56362 p;
  PeripheralsNop pNop;
  Memory mem(g_memoryValidator, 0x080000, 0x800000, 0x200000);
  DSP dsp(mem, &p, &pNop);

  // A slot has run, so the transmit condition stands and arm() is served.
  raiseTransmitDataEmpty(p.getEsai());
  verify(p.getEsai().readStatusRegister() & (1 << Esai::M_TDE));

  mem.set(MemArea_X, g_source, g_payload);
  mem.set(MemArea_X, g_destination, 0);

  auto &dma = p.getDMA();

  dma.setDOR(0, 0);
  dma.setDSR(g_channel, g_source);
  dma.setDDR(g_channel, g_destination);
  dma.setDCO(g_channel, 0);
  dma.setDCR(g_channel, dcrUnsupported() & ~(1u << DmaChannel::De));

  g_captured.clear();
  Logging::setLogFunc(&captureLog);
  dma.setDCR(g_channel, dcrUnsupported());
  Logging::setLogFunc(&logToStdout);

  verify(g_captured.size() == 1);

  verifyLogLine(g_captured[0],
                "DMA channel 2 unsupported address generation mode, DCR is "
                "906360, DAM is 36, no transfer performed");

  // Nothing was transferred, so nothing may have been written.
  verify(mem.get(MemArea_X, g_destination) == 0);
  verify(mem.get(MemArea_X, g_source) == g_payload);

  // DE is still set. This transfer mode clears DE when a block completes,
  // so an unchanged DCR is the observable proof that execTransfer did NOT
  // answer "finished" and finishTransfer never ran - no cleared enable
  // bit and no transfer-done interrupt for a transfer that did not happen.
  verify(dma.getDCR(g_channel) == 0x906360);

  // The addresses did not move either.
  verify(dma.getDSR(g_channel) == g_source);
  verify(dma.getDDR(g_channel) == g_destination);
}
} // namespace

int main() {
  try {
    unsupportedModeIsReportedAndDoesNotClaimCompletion();
  } catch (const std::string &_err) {
    Logging::setLogFunc(&logToStdout);
    std::cout << "dsp56k_dma_unsupported_mode_report FAILED: " << _err
              << std::endl;
    return -1;
  }

  std::cout << "dsp56k_dma_unsupported_mode_report passed" << std::endl;
  return 0;
}
