// Verify 2D source addressing mode transfers (SingleCounterApostInc destination,
// DualCounterDOR0-3 source) with line triggering per DSP56300FM Section 10.3.3.2.

#include "dsp56kEmu/dsp.h"
#include "dsp56kEmu/memory.h"
#include "dsp56kEmu/peripherals.h"
#include "dsp56kEmu/unittests.h"

#include "esaitransmitready.h"

#include <cstdint>
#include <iostream>

namespace {
using namespace dsp56k;
using AddressGenMode = DmaChannel::AddressGenMode;

constexpr uint32_t g_frameRate96k = 96000;

constexpr TWord g_channel = 2;
constexpr TWord g_source = 0x001000;
constexpr TWord g_destination = 0x002000;

constexpr TWord g_wordA = 0x0a1b2c;
constexpr TWord g_wordB = 0x3d4e5f;

constexpr TWord g_dorSlot = 2;
constexpr TWord g_dorOffset =
    0xffffff; // -1, the manual's wrap-to-first-register offset

// DCOH = 1 -> two lines, DCOL = 1 -> two words per line, four words total.
constexpr TWord g_dco = 0x001001;

constexpr TWord g_hwEsaiTransmitData = 12;

DefaultMemoryValidator g_memoryValidator;

// Source AGM 2 (DualCounterDOR2), destination AGM 5 (SingleCounterApostInc),
// line transfer triggered by request with DE cleared afterwards, both
// spaces X, DE set. This is the G2's DAM $2A with a request source whose
// flag is already raised at construction.
constexpr TWord dcr2dSource() {
  return (1u << DmaChannel::De) |
         (static_cast<TWord>(
              DmaChannel::TransferMode::LineTriggerRequestClearDE)
          << 19) |
         (g_hwEsaiTransmitData << 11) |
         (static_cast<TWord>(AddressGenMode::SingleCounterApostInc) << 7) |
         (static_cast<TWord>(AddressGenMode::DualCounterDOR2) << 4);
}

void twoDimensionalSourceWalksTheSourceAndAdvancesTheDestination() {
  // The literal pins the encoding: a builder compared only against
  // itself would agree with any bit layout. This word is NOT the G2's
  // $965AA0 - that one also carries DRS=01011 (ESAI receive) and DPR=11,
  // and this test drives ESAI transmit at the default priority. DAM is
  // the field under test and it is $2A in both.
  verify(dcr2dSource() == 0x9062a0);
  verify(((dcr2dSource() >> 4) & 0x3f) == 0x2a);

  Peripherals56362 p;
  PeripheralsNop pNop;
  Memory mem(g_memoryValidator, 0x080000, 0x800000, 0x200000);
  DSP dsp(mem, &p, &pNop);

  // A slot has run, so the transmit condition stands and arm() is served.
  raiseTransmitDataEmpty(p.getEsai());
  verify(p.getEsai().readStatusRegister() & (1 << Esai::M_TDE));

  mem.set(MemArea_X, g_source, g_wordA);
  mem.set(MemArea_X, g_source + 1, g_wordB);

  for (TWord i = 0; i < 5; ++i)
    mem.set(MemArea_X, g_destination + i, 0);

  auto &dma = p.getDMA();

  dma.setDOR(g_dorSlot, g_dorOffset);
  dma.setDSR(g_channel, g_source);
  dma.setDDR(g_channel, g_destination);
  dma.setDCO(g_channel, g_dco);
  dma.setDCR(g_channel, dcr2dSource() & ~(1u << DmaChannel::De));
  dma.setDCR(g_channel, dcr2dSource());

  // First line: two words, then the source returns to its first word via
  // DOR2 and the destination has advanced by two. The block is not done,
  // so DE is still set and DCR is unchanged.
  verify(mem.get(MemArea_X, g_destination + 0) == g_wordA);
  verify(mem.get(MemArea_X, g_destination + 1) == g_wordB);
  verify(mem.get(MemArea_X, g_destination + 2) == 0);
  verify(mem.get(MemArea_X, g_destination + 3) == 0);
  verify(mem.get(MemArea_X, g_destination + 4) == 0);

  verify(dma.getDSR(g_channel) == g_source);
  verify(dma.getDDR(g_channel) == g_destination + 2);
  verify(dma.getDCR(g_channel) == 0x9062a0);

  // Second line ends the block: two more words, DE cleared.
  verify(dma.trigger(DmaChannel::RequestSource::EsaiTransmitData));

  verify(mem.get(MemArea_X, g_destination + 0) == g_wordA);
  verify(mem.get(MemArea_X, g_destination + 1) == g_wordB);
  verify(mem.get(MemArea_X, g_destination + 2) == g_wordA);
  verify(mem.get(MemArea_X, g_destination + 3) == g_wordB);
  verify(mem.get(MemArea_X, g_destination + 4) == 0);

  verify(dma.getDSR(g_channel) == g_source);
  verify(dma.getDDR(g_channel) == g_destination + 4);
  verify(dma.getDCR(g_channel) == (0x9062a0u & ~(1u << DmaChannel::De)));
}
} // namespace

int main() {
  try {
    twoDimensionalSourceWalksTheSourceAndAdvancesTheDestination();
  } catch (const std::string &_err) {
    std::cout << "dsp56k_dma_2d_source FAILED: " << _err << std::endl;
    return -1;
  }

  std::cout << "dsp56k_dma_2d_source passed" << std::endl;
  return 0;
}
