module enderecoVGA (
	input [9:0] vga_x, vga_y,
	output [16:0] enderecoL
);

	wire [8:0] log_vga_x = (vga_x + 10'd1) >> 1;
	wire [7:0] log_vga_y = vga_y >> 1;
	assign enderecoL = (log_vga_y * 17'd320) + log_vga_x;
	
endmodule