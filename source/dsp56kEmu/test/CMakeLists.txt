cmake_minimum_required(VERSION 3.10)

# Test list for the Nord Modular G2 emulator dsp track.
#
# Each entry below builds ONE executable from ONE source file and registers it
# under ONE CTest name. A name registered here must be a distinct, real test.
# Do not point two names at one program. See README.md.

function(dsp56k_add_test _name)
	add_executable(${_name} ${_name}.cpp)
	target_link_libraries(${_name} PRIVATE dsp56kEmu)
	set_property(TARGET ${_name} PROPERTY FOLDER "dsp56300")
	add_test(NAME ${_name} COMMAND ${_name})
	set_tests_properties(${_name} PROPERTIES LABELS "UnitTest;dsp56300;nmg2")
endfunction()

dsp56k_add_test(dsp56k_peripheral_type)
dsp56k_add_test(dsp56k_peripherals56311_surface)
dsp56k_add_test(dsp56k_find_exec_peripherals)
dsp56k_add_test(dsp56k_check_trigger_overload)
dsp56k_add_test(dsp56k_peripheral_type_dispatch)
dsp56k_add_test(dsp56k_dma_request_source)
dsp56k_add_test(dsp56k_second_esai_dma)
dsp56k_add_test(dsp56k_dma_dor_generalization)
dsp56k_add_test(dsp56k_do_forever)
dsp56k_add_test(dsp56k_esai_receive_last_slot)
