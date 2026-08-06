cmake_minimum_required(VERSION 3.10)

# Per-task test list for the Nord Modular G2 emulator dsp track.
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

# The list is empty. Each task adds its own entry.
