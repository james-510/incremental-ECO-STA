// ISCAS85 c17 mapped to sky130 NAND2 cells (6 gates, 5 inputs, 2 outputs).
module c17 (N1, N2, N3, N6, N7, N22, N23);
  input N1, N2, N3, N6, N7;
  output N22, N23;
  wire N10, N11, N16, N19;
  sky130_fd_sc_hd__nand2_1 g10 (.A(N1),  .B(N3),  .Y(N10));
  sky130_fd_sc_hd__nand2_1 g11 (.A(N3),  .B(N6),  .Y(N11));
  sky130_fd_sc_hd__nand2_1 g16 (.A(N2),  .B(N11), .Y(N16));
  sky130_fd_sc_hd__nand2_1 g19 (.A(N11), .B(N7),  .Y(N19));
  sky130_fd_sc_hd__nand2_1 g22 (.A(N10), .B(N16), .Y(N22));
  sky130_fd_sc_hd__nand2_1 g23 (.A(N16), .B(N19), .Y(N23));
endmodule
