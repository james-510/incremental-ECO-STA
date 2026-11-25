# OpenSTA reference setup: time in ns, capacitance in pF.
# c17 is combinational, so use a virtual clock to define I/O budgets.
# These constraints are separate from the synthetic ECO RefTimer model.
create_clock -name vclk -period 1.0
set_input_delay -clock vclk 0.0 [all_inputs]
set_output_delay -clock vclk 0.0 [all_outputs]
set_input_transition 0.05 [all_inputs]
set_load 0.005 [all_outputs]
