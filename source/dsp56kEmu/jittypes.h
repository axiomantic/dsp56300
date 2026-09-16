#pragma once

#include "dsp56kBase/buildconfig.h"
#include "types.h"

// set to 1 to compile for ARM even if on an x64 system
#if 0
#define HAVE_ARM64
#undef HAVE_X86_64
#endif

namespace dsp56k
{
	struct DspRegs;

	using JitDspPtr = DspRegs;

	typedef void (*TJitFunc)(JitDspPtr*, TWord) noexcept;

	// What an entry table slot holds when no JIT block has been made for its address
	// yet. It is also what every read of the table resolves to when the index is not
	// one the table has: the two cases mean the same thing to the caller, and giving
	// them the same answer is what lets the read be bounded without a second outcome
	// to define. Declared here rather than in jit.h so the read sites in DSP can name
	// it without pulling the JIT in.
	void funcCreate(JitDspPtr* _jit, TWord _pc) noexcept;
}

#if defined(HAVE_ARM64)
#include "asmjit/arm/a64operand.h"

namespace asmjit
{
	inline namespace ASMJIT_ABI_NAMESPACE
	{
		namespace a64
		{
			class Builder;
		}
	}
}

namespace dsp56k
{
	using JitBuilder = asmjit::a64::Builder;

	using JitMemPtr = asmjit::arm::Mem;

	using JitReg = asmjit::arm::Reg;
	using JitRegGP = asmjit::a64::Gp;
	using JitReg32 = asmjit::a64::GpW;
	using JitReg64 = asmjit::a64::GpX;
	using JitReg128 = asmjit::a64::VecV;
	using JitCondCode = asmjit::arm::CondCode;
}
#else
#include "asmjit/x86/x86operand.h"

namespace asmjit
{
	inline namespace ASMJIT_ABI_NAMESPACE
	{
		namespace x86
		{
			class Builder;
		}
	}
}

namespace dsp56k
{
	using JitBuilder = asmjit::x86::Builder;

	using JitMemPtr = asmjit::x86::Mem;

	using JitReg = asmjit::x86::Reg;
	using JitRegGP = asmjit::x86::Gp;
	using JitReg32 = asmjit::x86::Gpd;
	using JitReg64 = asmjit::x86::Gpq;
	using JitReg128 = asmjit::x86::Xmm;
	using JitCondCode = asmjit::x86::CondCode;
}
#endif
