add_test(NAME dsp56k_core COMMAND dsp56kTestRunner dsp56k_core)
add_test(NAME dsp56k_alu COMMAND dsp56kTestRunner dsp56k_alu)
add_test(NAME dsp56k_agud COMMAND dsp56kTestRunner dsp56k_agud)
add_test(NAME dsp56k_dma COMMAND dsp56kTestRunner dsp56k_dma)
add_test(NAME dsp56k_esai COMMAND dsp56kTestRunner dsp56k_esai)
add_test(NAME dsp56k_hi08 COMMAND dsp56kTestRunner dsp56k_hi08)
add_test(NAME dsp56k_timer COMMAND dsp56kTestRunner dsp56k_timer)
add_test(NAME dsp56k_bitops COMMAND dsp56kTestRunner dsp56k_bitops)
add_test(NAME dsp56k_mac COMMAND dsp56kTestRunner dsp56k_mac)

set_tests_properties(
	dsp56k_core
	dsp56k_alu
	dsp56k_agud
	dsp56k_dma
	dsp56k_esai
	dsp56k_hi08
	dsp56k_timer
	dsp56k_bitops
	dsp56k_mac
	PROPERTIES SKIP_RETURN_CODE 77
)
