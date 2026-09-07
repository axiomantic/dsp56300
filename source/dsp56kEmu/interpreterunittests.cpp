#include "interpreterunittests.h"

#include "agu.h"
#include "dsp.h"
#include "memory.h"

namespace dsp56k
{
	InterpreterUnitTests::InterpreterUnitTests()
	{
		testCCCC();
		testSubr();
		testLslLsrOversizedShift();
		
		runAllTests();
	}

	void InterpreterUnitTests::execOpcode(uint32_t _op0, uint32_t _op1, const bool _reset, TWord _pc)
	{
		if(_reset)
			dsp.resetHW();
		dsp.clearOpcodeCache();
		dsp.mem.set(MemArea_P, _pc, _op0);
		dsp.mem.set(MemArea_P, _pc + 1, _op1);
		dsp.setPC(_pc);

		// Execute only the instruction, bypassing interrupt handling which
		// is designed for a running DSP, not single-step unit tests.
		dsp.pcCurrentInstruction = _pc;
		const auto op = dsp.fetchPC();
		dsp.execOp(op);
	}

	void InterpreterUnitTests::testSubr()
	{
		dsp.setALU(false, TReg56(static_cast<TReg56::MyType>(0x00600000000000)));
		dsp.setALU(true , TReg56(static_cast<TReg56::MyType>(0x00020000000000)));

		emit("subr b,a");
		verify(dsp.aluA().var == 0x002e0000000000);
		verify(!dsp.sr_test(CCR_C));
		verify(!dsp.sr_test(CCR_V));
	}

	void InterpreterUnitTests::testCCCC()
	{
		constexpr auto T=true;
		constexpr auto F=false;

		//                            <  <= =  >= >  != 
		testCCCC(0xff000000000000, 0, T, T, F, F, F, T);
		testCCCC(0x00ff0000000000, 0, F, F, F, T, T, T);
		testCCCC(0x00000000000000, 0, F, T, T, T ,F ,F);
	}

	void InterpreterUnitTests::testCCCC(const int64_t _value, const int64_t _compareValue, const bool _lt, bool _le, bool _eq, bool _ge, bool _gt, bool _neq)
	{
		dsp.resetHW();
		dsp.setALU(false, TReg56(static_cast<TReg56::MyType>(_value)));
		dsp.alu_cmp(false, TReg56(_compareValue), false);
		char sr[16]{};
		dsp.sr_debug(sr);
		verify(_lt == (dsp.decode_cccc(CCCC_LessThan) != 0));
		verify(_le == (dsp.decode_cccc(CCCC_LessEqual) != 0));
		verify(_eq == (dsp.decode_cccc(CCCC_Equal) != 0));
		verify(_ge == (dsp.decode_cccc(CCCC_GreaterEqual) != 0));
		verify(_gt == (dsp.decode_cccc(CCCC_GreaterThan) != 0));
		verify(_neq == (dsp.decode_cccc(CCCC_NotEqual) != 0));	
	}

	// The register form of LSL and LSR takes the low six bits of the source register,
	// so a count of 32 or more reaches the ALU. Any count from 24 upwards shifts every
	// bit of the 24 bit operand out and leaves zero, the rule the count of 28 in
	// UnitTests::lsl states. A plain machine shift cannot express that: both x86 and
	// arm64 truncate the count of a 32 bit shift to five bits, so a count of 32 shifts
	// by nothing and returns the operand unchanged.
	void InterpreterUnitTests::testLslLsrOversizedShift()
	{
		for(const TWord shiftAmount : {32u, 40u, 63u})
		{
			dsp.x1(shiftAmount);
			dsp.setALU(false, TReg56(static_cast<TReg56::MyType>(0xab112233445566)));
			emit("lsl x1,a");
			verify(dsp.aluA().var == 0xab000000445566);
			verify(!dsp.sr_test(CCR_C));

			dsp.x1(shiftAmount);
			dsp.setALU(false, TReg56(static_cast<TReg56::MyType>(0xab112233445566)));
			emit("lsr x1,a");
			verify(dsp.aluA().var == 0xab000000445566);
			verify(!dsp.sr_test(CCR_C));
		}
	}

	void InterpreterUnitTests::runTest(const std::function<void()>& _build, const std::function<void()>& _verify)
	{
		_build();
		_verify();
	}

	void InterpreterUnitTests::emit(TWord _opA, TWord _opB, TWord _pc)
	{
		execOpcode(_opA, _opB, false, _pc);
	}

}
