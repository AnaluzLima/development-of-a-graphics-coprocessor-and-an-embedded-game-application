module MotorBG (
	input clk, rst, vsync,
	input [8:0] bg_x, // coordenada X (0 a 319)
	input [7:0] bg_y, // coordenada Y (0 a 239)

	input [1:0] qual_op,
	input [26:0] dados,
	
	output [7:0] draw
);

	reg wr_en;
	reg [16:0] wr_addr; // 17 bits para cobrir 76.800 pixels (320x240)
	reg [7:0] wr_data;

	reg [5:0] scroll_x; // deslocamento horizontal em tiles (0 a 39)
	reg [4:0] scroll_y; // deslocamento vertical em tiles (0 a 29)

	always @(posedge clk or posedge rst) begin
		if (rst) begin
			wr_en <= 1'b0;
			wr_addr <= 17'b0;
			wr_data <= 8'b0;
			scroll_x <= 6'b0;
			scroll_y <= 5'b0;
		end else begin
			wr_en <= qual_op[1];                 
			if (qual_op[1]) begin
				wr_addr <= dados[24:8]; // 17 bits de endereço do pixel
				wr_data <= dados[7:0];  // 8 bits de cor
			end
			if (qual_op[0]) begin                
				scroll_x <= dados[10:5];
				scroll_y <= dados[4:0];
			end
		end
	end

	reg [5:0] scroll_x_atrasado;
	reg [4:0] scroll_y_atrasado;

	always @(negedge vsync or posedge rst) begin
		if (rst) begin
			scroll_x_atrasado <= 6'd0;
			scroll_y_atrasado <= 5'd0;
		end else begin
			scroll_x_atrasado <= scroll_x;
			scroll_y_atrasado <= scroll_y;
		end
	end

	wire [5:0] col_tile = bg_x[8:3]; // 0 a 39
	wire [4:0] lin_tile = bg_y[7:3]; // 0 a 29

	wire [6:0] soma_col  = col_tile + scroll_x_atrasado;
	wire [5:0] col_final = (soma_col >= 7'd40) ? (soma_col - 7'd40) : soma_col[5:0];

	wire [5:0] soma_lin  = lin_tile + scroll_y_atrasado;
	wire [4:0] lin_final = (soma_lin >= 6'd30) ? (soma_lin - 6'd30) : soma_lin[4:0];

	wire [8:0] x_pixel_final = {col_final, bg_x[2:0]}; // 0 a 319
	wire [7:0] y_pixel_final = {lin_final, bg_y[2:0]}; // 0 a 239

	wire [16:0] tilemap_addr = (y_pixel_final * 17'd320) + x_pixel_final;

	ram_map tm (
		.clock (clk),
		.data (wr_data),
		.wraddress (wr_addr),
		.wren (wr_en),
		.rdaddress (tilemap_addr),
		.q (draw)
	);

endmodule