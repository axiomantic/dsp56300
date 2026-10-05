cmake_minimum_required(VERSION 3.10)

function(dsp56k_add_test _name)
	add_executable(${_name} ${_name}.cpp)
	target_link_libraries(${_name} PRIVATE dsp56kEmu)
	set_property(TARGET ${_name} PROPERTY FOLDER "dsp56300")
	add_test(NAME ${_name} COMMAND ${_name})
	set_tests_properties(${_name} PROPERTIES LABELS "UnitTest;dsp56300" TIMEOUT 60)
endfunction()

dsp56k_add_test(dsp56k_esai_tx_underrun_latch)
