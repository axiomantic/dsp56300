#pragma once

#include "dsp56kEmu/esai.h"

namespace dsp56k {
/*	Raise TDE the way the chip does.

        Reset clears TDE and TFS, and the first transmit slot sets them (56362
   UM 8.3.6.10 and 8.3.6.12). The 56300 simulator reads SAISR $000000 after
   reset and still reads it after TE0 is set, so a transmit request is not
   pending until a slot has run. A fixture that wants the transmit condition has
   to run one.

        Two slots per frame and network mode with transmitter 0 enabled is the
   smallest programming that produces a slot. The transmitter is left on,
   because a DMA channel armed on the transmit source is served while the
   condition stands.
*/
inline void raiseTransmitDataEmpty(Esai &_esai) {
  _esai.writeTransmitClockControlRegister(1 << Esai::M_TDC0);
  _esai.writeTransmitControlRegister((1 << Esai::M_TMOD0) | (1 << Esai::M_TE0));
  _esai.execTX();
}
} // namespace dsp56k
