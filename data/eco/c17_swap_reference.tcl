# OpenSTA equivalent of: swap_cell g16 sky130_fd_sc_hd__nand2_2
# Start with a single edit before buffer/reconnect integration.
set replacement [get_lib_cells */sky130_fd_sc_hd__nand2_2]
if {[llength $replacement] != 1} {
    error "Expected exactly one sky130_fd_sc_hd__nand2_2 cell in the loaded library"
}
replace_cell g16 [lindex $replacement 0]
