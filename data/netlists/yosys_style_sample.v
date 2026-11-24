// Hand-written sample in the style Yosys emits: bus ports, escaped identifiers, attributes,
// a constant tie, flops. Exercises the loader paths that real mapped netlists hit.
(* top = 1 *)
module top (clk, a, b, y);
  input clk;
  input [1:0] a;
  input [1:0] b;
  output [1:0] y;
  wire _00_;
  wire _01_;
  wire _02_;
  wire [1:0] q;
  sky130_fd_sc_hd__xor2_1 _03_ ( .A(a[0]), .B(b[0]), .X(_00_) );
  sky130_fd_sc_hd__and2_1 _04_ ( .A(\a[0] ), .B(b[0]), .X(_01_) );
  sky130_fd_sc_hd__xor2_1 _05_ ( .A(a[1]), .B(b[1]), .X(_02_) );
  sky130_fd_sc_hd__dfxtp_1 \q_reg[0]  ( .CLK(clk), .D(_00_), .Q(q[0]) );
  sky130_fd_sc_hd__dfxtp_1 \q_reg[1]  ( .CLK(clk), .D(_02_), .Q(q[1]) );
  sky130_fd_sc_hd__or2_1 _06_ ( .A(q[0]), .B(_01_), .X(y[0]) );
  sky130_fd_sc_hd__and2_1 _07_ ( .A(q[1]), .B(1'b1), .X(y[1]) );
endmodule
