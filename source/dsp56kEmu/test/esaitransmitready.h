#pragma once

#include "dsp56kEmu/esai.h"

namespace dsp56k {
// Raise TDE by running one transmit slot in network mode (DSP56362UM Section 8.3.6).
inline void raiseTransmitDataEmpty(Esai &_esai) {
  _esai.writeTransmitClockControlRegister(1 << Esai::M_TDC0);
  _esai.writeTransmitControlRegister((1 << Esai::M_TMOD0) | (1 << Esai::M_TE0));
  _esai.execTX();
}
} // namespace dsp56k
