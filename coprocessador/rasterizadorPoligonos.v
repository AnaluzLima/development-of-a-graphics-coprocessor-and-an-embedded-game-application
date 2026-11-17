module rasterizadorPoligonos (
	input clk,

	input [8:0] next_x,
	input [7:0] next_y,

	input [799:0] poligono,

	output reg [7:0] draw
);

	//S0
	//pixel adiantado em 2 posições para compensar a latência do pipeline (5 estágios, 20 ns cada)
	wire [9:0] xs = {1'b0, next_x} + 10'd2;
	wire quebra = (xs >= 10'd320);
	wire [8:0] la_x = quebra ? (xs - 10'd320) : xs[8:0];
	wire [7:0] la_y = quebra ? ((next_y == 8'd239) ? 8'd0 : (next_y + 8'd1)) : next_y;

	reg [8:0] s0_x;
	reg [7:0] s0_y;
	
	//registrador do S0
	always @(posedge clk) begin
		s0_x <= la_x; // registrando o pixel adiantado em 2 posições
		s0_y <= la_y;
	end

	wire signed [11:0] px = {3'b000, s0_x};
	wire signed [11:0] py = {4'b0000, s0_y};

	wire [15:0] dentro; //bit i vale 1 se o pixel caiu dentro do polígono i
	wire [127:0] cores_flat; //cor de cada polígono

	genvar i;
	generate
	  for (i = 0; i < 16; i = i + 1) begin : check_poly

			wire [49:0] poly = poligono [i*50 +: 50]; // polígono i

			wire [8:0] AxD = poly[8:0];
			wire [7:0] AyD = poly[16:9];
			wire [8:0] largura = poly[25:17];
			wire [7:0] altura = poly[33:26];
			wire [7:0] cor_p = poly[41:34];
			wire show = poly[42];
			wire [2:0] tipo = poly[45:43];
			wire [1:0] flip = poly[47:46];
			
			//guarda a cor desse polígono na posição i do barramento de cores
			assign cores_flat[i*8 +: 8] = cor_p;

			wire signed [11:0] ax = {3'b000, AxD};
			wire signed [11:0] ay = {4'b0000, AyD};
			wire signed [11:0] w = {3'b000, largura};
			wire signed [11:0] h = {4'b0000, altura};

			reg signed [11:0] vx_a, vy_a, bx, by, cx, cy;
			always @(*) begin
				vx_a = ax; vy_a = ay;
				bx = ax; by = ay;
				cx = ax; cy = ay;

				case (tipo)
					3'd2: begin // Equilátero
						case (flip)
						2'd0: begin //CIMA
							vx_a = ax; vy_a = ay;
							bx = ax - (w>>>1); by = ay + h;
							cx = ax + (w>>>1); cy = ay + h;
						end
						2'd1: begin // BAIXO
							vx_a = ax; vy_a = ay + h;
							bx = ax - (w>>>1); by = ay;
							cx = ax + (w>>>1); cy = ay;
						end
						2'd2: begin // DIREITA
							vx_a = ax + (w>>>1); vy_a = ay + (h>>>1);
							bx = ax - (w>>>1); by = ay;
							cx = ax - (w>>>1); cy = ay + h;
						end
						2'd3: begin //ESQUERDA
							vx_a = ax - (w>>>1); vy_a = ay + (h>>>1);
							bx = ax + (w>>>1); by = ay;
							cx = ax + (w>>>1); cy = ay + h;
						end
						endcase
					end
					3'd3: begin // Isósceles
						case (flip)
						2'd0: begin // CIMA
							vx_a = ax; vy_a = ay;
							bx = ax - (w>>>2); by = ay + h;
							cx = ax + (w>>>2); cy = ay + h;
						end
						2'd1: begin // BAIXO
							vx_a = ax; vy_a = ay + h;
							bx = ax - (w>>>2); by = ay;
							cx = ax + (w>>>2); cy = ay;
						end
						2'd2: begin // DIREITA
							vx_a = ax + (w>>>2); vy_a = ay + (h>>>1);
							bx = ax - (w>>>2); by = ay;
							cx = ax - (w>>>2); cy = ay + h;
						end
						2'd3: begin // ESQUERDA
							vx_a = ax - (w>>>2); vy_a = ay + (h>>>1);
							bx = ax + (w>>>2); by = ay;
							cx = ax + (w>>>2); cy = ay + h;
						end
						endcase
					end
					3'd4: begin // Escaleno
						case (flip)
						2'd0: begin //CIMA
							vx_a = ax; vy_a = ay;
							bx = ax; by = ay + h;
							cx = ax + w; cy = ay + h;
						end
						2'd1: begin // BAIXO
							vx_a = ax; vy_a = ay + h;
							bx = ax; by = ay;
							cx = ax + w; cy = ay;
						end
						2'd2: begin //DIREITA
							vx_a = ax + w; vy_a = ay;
							bx = ax; by = ay;
							cx = ax; cy = ay + h;
						end
						2'd3: begin //ESQUERDA
							vx_a = ax; vy_a = ay;
							bx = ax + w; by = ay;
							cx = ax + w; cy = ay + h;
						end
						endcase
					end
					default: begin
						vx_a = ax; vy_a = ay;
						bx = ax; by = ay;
						cx = ax; cy = ay;
					end
				endcase
			end

			//REGISTRADORES ESTÁTICOS: guardam os vértices, os vetores das 3 arestas e limites

			reg signed [11:0] r_vx, r_vy, r_bx, r_by, r_cx, r_cy; //vértices A, B, C
			reg signed [11:0] r_a1, r_b1, r_a2, r_b2, r_a3, r_b3; //coeficientes
			reg signed [11:0] r_ax, r_ay, r_axw, r_ayh, r_ayw; //retângulo/quadrado
			reg r_show;
			reg [2:0] r_tipo;

			//registra vértices e coeficientes das arestas, retângulo/quadrado, show/tipo
			always @(posedge clk) begin
				r_vx <= vx_a; r_vy <= vy_a;
				r_bx <= bx; r_by <= by;
				r_cx <= cx; r_cy <= cy;
				
				//vetores das 3 arestas já subtraídos e salvos:
				r_a1 <= bx - vx_a; r_b1 <= by - vy_a; // aresta A->B
				r_a2 <= cx - bx; r_b2 <= cy - by; // aresta B->C
				r_a3 <= vx_a - cx; r_b3 <= vy_a - cy; // aresta C->A
				
				//limites para retângulo e quadrado
				r_ax <= ax;
				r_ay <= ay;
				r_axw <= ax + w;
				r_ayh <= ay + h;
				r_ayw <= ay + w; // quadrado usa a largura como altura
				r_show <= show;
				r_tipo <= tipo;
			end

			//S1: multiplica e testa limites de Retângulo/Quadrado
			reg signed [23:0] m1a, m1b, m2a, m2b, m3a, m3b;
			reg ret1, qua1;
			
			always @(posedge clk) begin
				//calcula os 6 produtos das 3 arestas
				m1a <= r_a1 * (py - r_vy); m1b <= r_b1 * (px - r_vx);
				m2a <= r_a2 * (py - r_by); m2b <= r_b2 * (px - r_bx);
				m3a <= r_a3 * (py - r_cy); m3b <= r_b3 * (px - r_cx);
				
				//testa se px e py está dentro da caixa do retângulo ou quadrado
				ret1 <= (px >= r_ax) && (px <= r_axw) && (py >= r_ay) && (py <= r_ayh);
				qua1 <= (px >= r_ax) && (px <= r_axw) && (py >= r_ay) && (py <= r_ayw);
			end
			

			//S2: subtrai os produtos (Função de Aresta) e verifica o sinal
			wire signed [23:0] e1 = m1a - m1b;
			wire signed [23:0] e2 = m2a - m2b;
			wire signed [23:0] e3 = m3a - m3b;

			reg tri2, ret2, qua2;
			always @(posedge clk) begin
				//o pixel está dentro do triângulo se estiver do mesmo lado das 3 arestas (todos <= 0 para sentido horário, ou todos >= 0 para anti-horário)
				tri2 <= ((e1 <= 0) && (e2 <= 0) && (e3 <= 0)) || ((e1 >= 0) && (e2 >= 0) && (e3 >= 0));
				//repassa ret1 e qua1 para o próximo registrador para manter todos sincronizados
				ret2 <= ret1;
				qua2 <= qua1;
			end
			//O retangulo/quadrado foi calculado no estagio S1, mas o valor precisou ser repassado no S2 para não chegar adiantado em relação ao triângulo

			//S3: está visível e caiu dentro da forma?
			reg dentro_q;
			always @(posedge clk) begin
				dentro_q <= r_show && ((r_tipo == 3'd0 && ret2) || (r_tipo == 3'd1 && qua2) || (r_tipo >= 3'd2 && r_tipo <= 3'd4 && tri2));
			end
			assign dentro[i] = dentro_q; //conecta o resultado registrado no vetor
		end
	endgenerate

	// S4: codificador de prioridade e cor
	reg [3:0] poly_sel;
	reg hit;
	integer j;

	//como vai descendo até 0, se o polígono 0 e o polígono 5 estiverem no mesmo pixel, o índice menor sobrescreve por último e vence
	always @(*) begin
		hit = 1'b0;
		poly_sel = 4'd0;
		for (j = 15; j >= 0; j = j - 1) begin
			if (dentro[j]) begin
				poly_sel = j[3:0];
				hit = 1'b1;
			end
		end
	end

	reg hit_reg;
	reg [7:0] cor_reg;
	
	//registrador do S4: salva se houve colisão e busca a cor no mux
	always @(posedge clk) begin
		hit_reg <= hit;
		cor_reg <= cores_flat[poly_sel*8 +: 8]; // mux 16:1
	end
	
	//se acertou algum polígono e a cor não for 0x00, desenha
	always @(*) begin
		if (hit_reg && (cor_reg != 8'd0)) draw = cor_reg;
		else draw = 8'd0;
	end

endmodule

module RegPoligonos (
	input clk, rst, 
	input [2:0] pol_en, 
	input [26:0] dados,
	
	output [799:0] poligono
);

	reg [49:0] banco [0:15];
	reg [49:0] temp_poly;
	reg [3:0] id;

	integer i;
	always @(posedge clk) begin
		if (rst) begin
			banco[0] <= {2'd0, 2'd0, 3'd0, 1'b0, 8'hE0, 8'd15, 9'd40, 8'd30,  9'd30}; // Retângulo
         banco[1] <= {2'd0, 2'd0, 3'd1, 1'b0, 8'h1C, 8'd30, 9'd30, 8'd30,  9'd100}; // Quadrado
         banco[2] <= {2'd0, 2'd1, 3'd2, 1'b1, 8'hE0, 8'd60, 9'd60, 8'd100, 9'd160}; // Equilátero
         banco[3] <= {2'd0, 2'd0, 3'd3, 1'b0, 8'hFC, 8'd45, 9'd20, 8'd30,  9'd240}; // Isósceles
         banco[4] <= {2'd0, 2'd0, 3'd4, 1'b0, 8'hE3, 8'd30, 9'd40, 8'd120, 9'd50}; // Escaleno
			
			for (i = 5; i<=15; i = i+1) begin
				banco[i] <= 50'd0;
			end

		end
		else begin
			case (pol_en)
                3'b001: begin //POSPO
						  id = dados[20:17];
						  temp_poly = banco[id];
                    temp_poly[16:9] = dados[7:0]; // Y
                    temp_poly[8:0] = dados[16:8]; // X
						  banco[id] <= temp_poly;
                end
                3'b010: begin //ATPO
						  id = dados[7:4];
						  temp_poly = banco[id];
                    temp_poly[45:43] = dados[2:0]; //tipo
						  temp_poly[47:46] = dados[9:8]; //flip
                    temp_poly[42] = dados[3]; //show
						  banco[id] <= temp_poly;
                end
                3'b011: begin //TAMPO
						id = dados[20:17];
						temp_poly = banco[id];
						temp_poly[33:26] = dados[7:0]; //altura
						temp_poly[25:17] = dados[16:8]; //largura
						banco[id] <= temp_poly;
                end
					 3'b100: begin //CORPO
						id = dados[11:8];
						temp_poly = banco[id];
						temp_poly[41:34] = dados[7:0]; //cor
						banco[id] <= temp_poly;
                end
            endcase
		end
	end

	genvar k;
	generate
		for (k=0; k<16; k=k+1) begin: empacotar
			assign poligono[k*50 +: 50] = banco[k];
		end
	endgenerate

endmodule