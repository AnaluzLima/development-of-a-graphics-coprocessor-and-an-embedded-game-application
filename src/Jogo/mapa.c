#include "jogo.h"
#include "fase1.h"
#include "fase2.h"
#include "fase3.h"
#include "mainCat.h"
#include "animacao_bug.h"
#include "botdog.h"
#include "inimigos.h"

//mapeamento dos slots de 16x16 na ram de sprites
enum {
    SLOT_PATRULHA = 2,
    SLOT_AVE0 = 3,
    SLOT_AVE1 = 4,
    SLOT_AVE2 = 5,
    SLOT_BUG_BASE = 14,
    SLOT_BOTDOG0_BASE = 21,
    SLOT_BOTDOG1_BASE = 26
};

//definição das entidades e estado do mapa ativo
BugFase bugs[MAX_BUGS_FASE];
int num_bugs = 0;

uint8_t mapa_atual[MAP_TILES_Y][TILES_X];
EspinhoFase espinhos[MAX_ESPINHOS_FIXOS];
int num_espinhos = 0;
EspinhoTemporario espinhos_temp[MAX_TEMPORARIOS];
int num_espinhos_temp = 0;

InimigoPatrulha   patrulhas[MAX_PATRULHAS_FASE];
int num_patrulhas = 0;

AveRobo aves[MAX_AVES_FASE];
int num_aves = 0;

BotDogConfig botdogs[MAX_BOTDOGS_FASE];
int num_botdogs = 0;

int portal_px  = 91;
int portal_py  = 3;
uint8_t portal_cor = 0x00;
int etapa_fase = 0;
int portal_fugiu = 0;

//estado da animacao ativa
int botdog_ativa = 0;
int botdog_disparada = 0;
int botdog_dir = 1;
int botdog_x = -80;
int botdog_y = 0;
int botdog_frames = 0;
int tremor_tela = 0;

//retorna o vetor de pixels do background da fase selecionada
static const uint8_t *obter_pixels_fase(int fase) {
    if (fase == 1) return mapa_fase2_pixels;
    if (fase == 2) return portal_fugiu ? mapa_fase3_2_pixels : mapa_fase3_1_pixels;
    return mapa_fase1_pixels;
}

//retorna a matriz de colisao da fase selecionada
static const uint8_t (*obter_paredes_fase(int fase))[FASE1_LARGURA] {
    if (fase == 1) return paredesF2;
    if (fase == 2) return paredesF3;
    return paredesF1;
}

//carrega a metade que o jogador ta no mapa
void carregar_fundo_etapa(void) {
    const uint8_t *origem = obter_pixels_fase(fase_atual);
    int offset_y = (etapa_fase == 0) ? 240 : 0;
    int base_origem = offset_y * SCREEN_W;

    bscroll(0, 0);
    for (int i = 0; i < TOTAL_PIXELS_BG; i++) {
        btile(i, origem[base_origem + i]);
    }
}

//alterna entre a metade inferior e superior do mapa conforme a altura do jogador
void atualizar_etapa_camera(int jogador_py) {
    if (etapa_fase == 0 && jogador_py < 240) {
        etapa_fase = 1;
        for (int i = 0; i < num_aves; i++) aves[i].tiro_ativo = 0;
        carregar_fundo_etapa();
    }
    else if (etapa_fase == 1 && jogador_py >= 240) {
        etapa_fase = 0;
        for (int i = 0; i < num_aves; i++) aves[i].tiro_ativo = 0;
        carregar_fundo_etapa();
    }
}

//converte a mascara de paredes em pixels para a grade de tiles 8x8
static void copiar_mascara_colisoes(void) {
    const uint8_t (*paredes)[FASE1_LARGURA] = obter_paredes_fase(fase_atual);
    for (int ty = 0; ty < MAP_TILES_Y; ty++) {
        for (int tx = 0; tx < TILES_X; tx++) {
            int px = tx * 8 + 4;
            int py = ty * 8 + 4;
            if (paredes[py][px] != 0 || ty == 0 || ty == MAP_TILES_Y - 1 || tx == 0 || tx == TILES_X - 1) {
                mapa_atual[ty][tx] = TILE_PAREDE;
            } else {
                mapa_atual[ty][tx] = TILE_VAZIO;
            }
        }
    }
}

//grava um sprite 16x16 em um slot da ram
static void gravar_slot_sprite(int slot, const uint8_t pixels[256]) {
    for (int p = 0; p < 256; p++) {
        wrsp(slot * 256 + p, pixels[p]);
    }
}

//carrega os 2 frames da skin escolhida do gato nos slots 0 e 1
static void carregar_gato_skin_na_ram(int skin_id) {
    if (skin_id < 0 || skin_id >= GATO_NUM_SKINS) skin_id = 0;
    for (int f = 0; f < GATO_NUM_FRAMES; f++) {
        int slot_ram = GATO_SLOT_BASE + f;
        int end_base = slot_ram * 256;
        for (int p = 0; p < 256; p++) {
            wrsp(end_base + p, gato_skins_pixels[skin_id][f][p]);
        }
    }
}

//carrega na ram apenas os recursos necessarios para a fase atual
static void carregar_slots_objetos_jogo(void) {
    carregar_gato_skin_na_ram(skin_selecionada);

    if (fase_atual == 0) {
        gravar_slot_sprite(SLOT_PATRULHA, virus_pixels[0]);
    } else if (fase_atual == 1) {
        gravar_slot_sprite(SLOT_PATRULHA, warning_pixels[0]);
    } else {
        gravar_slot_sprite(SLOT_PATRULHA, nuvem_pixels[0]);
    }

    const uint8_t *fb[7] = {
        bug0_pixels[0], bug1_pixels[0], bug2_pixels[0], bug3_pixels[0],
        bug4_pixels[0], bug5_pixels[0], bug6_pixels[0]
    };
    for (int i = 0; i < 7; i++) gravar_slot_sprite(SLOT_BUG_BASE + i, fb[i]);

    if (num_botdogs > 0) {
        for (int i = 0; i < 5; i++) {
            gravar_slot_sprite(SLOT_BOTDOG0_BASE + i, botdog0_pixels[i]);
            gravar_slot_sprite(SLOT_BOTDOG1_BASE + i, botdog1_pixels[i]);
        }
    }

    if (num_aves > 0) {
        gravar_slot_sprite(SLOT_AVE0, ave0_pixels[0]);
        gravar_slot_sprite(SLOT_AVE1, ave1_pixels[0]);
        gravar_slot_sprite(SLOT_AVE2, ave2_pixels[0]);
    }
}

//funcao para criar um espinho normal fixo na fase
static void adicionar_espinho_fixo(int px, int py, int tipo_flip, uint8_t cor) {
    if (num_espinhos >= MAX_ESPINHOS_FIXOS) return;
    espinhos[num_espinhos] = (EspinhoFase){
        .px = px,
        .py = py,
        .tipo_flip = tipo_flip,
        .cor = cor,
        .poly_id = 0
    };
    num_espinhos++;
}

//funcao para criar um espinho retratil que dispara quando encosta
static void adicionar_espinho_temporario(int mx, int my, int tipo_flip, uint8_t cor_base, uint8_t cor_espinho) {
    if (num_espinhos_temp >= MAX_TEMPORARIOS) return;
    espinhos_temp[num_espinhos_temp] = (EspinhoTemporario){
        .marcador_x = mx,
        .marcador_y = my,
        .tipo_flip = tipo_flip,
        .cor_base = cor_base,
        .cor_espinho = cor_espinho,
        .poly_id = 0,
        .armado = 0,
        .timer_armar = 0,
        .ativo = 0,
        .timer_ativo = 0
    };
    num_espinhos_temp++;
}

//funcao para colocar um bug na fase
static void adicionar_bug_px(int px, int py, int sp_id) {
    if (num_bugs >= MAX_BUGS_FASE) return;
    bugs[num_bugs] = (BugFase){ px, py, 1, 0, sp_id };
    num_bugs++;
}

//patrulha em circuito fechado
static void adicionar_patrulha_circular(int x1, int y1, int x2, int y2, int vel) {
    if (num_patrulhas >= MAX_PATRULHAS_FASE) return;
    int v_abs   = (vel >= 0) ? vel : -vel;
    int horario = (vel < 0) ? 1 : 0;

    patrulhas[num_patrulhas] = (InimigoPatrulha){
        .px = x1,
        .py = y1,
        .x1 = x1,
        .y1 = y1,
        .x2 = x2,
        .y2 = y2,
        .vx = horario ? v_abs : 0,
        .vy = horario ? 0 : v_abs,
        .sprite_id = SPRITE_PATRULHA_BASE + num_patrulhas,
        .ativo = 1,
        .circular = horario ? 2 : 1
    };
    num_patrulhas++;
}

//adiciona ave que atira
static void adicionar_ave_robo(int x, int y, int dir) {
    if (num_aves >= MAX_AVES_FASE) return;
    static const int POLY_TIROS_AVES[4] = { 14, 15, 10, 11 };
    int d = (dir >= 0) ? 1 : -1;

    aves[num_aves] = (AveRobo){
        .x = x,
        .y = y,
        .dir = d,
        .etapa = (y >= 240) ? 0 : 1,
        .tiro_x0 = (d > 0) ? (x + 16) : (x - 4),
        .tiro_y0 = y + 6,
        .ativo = 1,
        .sprite_id = SPRITE_AVE_BASE + num_aves,
        .poly_id = POLY_TIROS_AVES[num_aves],
        .timer = 0,
        .anim_frames = 0,
        .tiro_ativo = 0,
        .tiro_x = 0,
        .tiro_y = 0,
        .explosao_frames = 0,
        .explosao_x = 0,
        .explosao_y = 0
    };
    num_aves++;
}

//adiciona botdog atravessa a tela
static void adicionar_botdog(int y_gatilho, int y_pos, int dir) {
    if (num_botdogs >= MAX_BOTDOGS_FASE) return;
    botdogs[num_botdogs] = (BotDogConfig){
        .y_gatilho = y_gatilho,
        .y_pos = y_pos,
        .dir = (dir >= 0) ? 1 : -1,
        .disparado = 0,
        .ativo = 1
    };
    num_botdogs++;
}

//apaga apenas a regiao da porta falsa no topo da Fase 3 sem recarregar toda a tela
void apagar_porta_topo_fase3(void) {
    int porta_x_inicio = 148;
    int porta_x_fim = 172;
    int porta_y_inicio = 1;
    int porta_y_fim = 23;

    for (int y = porta_y_inicio; y < porta_y_fim; y++) {
        for (int x = porta_x_inicio; x < porta_x_fim; x++) {
            int endereco_pixel = (y * SCREEN_W) + x;
            
            btile(endereco_pixel, mapa_fase3_2_pixels[endereco_pixel]);
        }
    }
}

//posiciona jogador, portal, armadilhas, inimigos e bugs de cada fase
static void configurar_layout_fase(int fase, int reinicializar_bugs) {
    num_espinhos = 0;
    num_espinhos_temp = 0;
    num_patrulhas = 0;
    num_aves = 0;
    num_botdogs = 0;

    if (fase == 0) {
        //FASE 1
        adicionar_espinho_temporario(56, 382, POL_TRI_BAIXO, 0x61, 0xFC);
        adicionar_espinho_temporario(272, 390, POL_TRI_ESQ, 0x61, 0xFC);
        adicionar_espinho_temporario(211, 253, POL_TRI_BAIXO, 0x61, 0xFC);
        adicionar_espinho_temporario(104, 352, POL_TRI_CIMA, 0x61, 0xFC);
        adicionar_espinho_temporario(30, 195, POL_TRI_DIR, 0x61, 0xFC);

        adicionar_espinho_fixo(94, 144, POL_TRI_CIMA, 0xFC);

        adicionar_patrulha_circular(88, 56, 176, 136, 2);

        portal_px = 89;
        portal_py = 0;
        portal_cor = 0x00;

        jogador_px = 136;
        jogador_py = 463;

        if (reinicializar_bugs) {
            num_bugs = 0;
            adicionar_bug_px(208, 273, SPRITE_BUG_BASE + 0);
            adicionar_bug_px(51, 192, SPRITE_BUG_BASE + 1);
            adicionar_bug_px(64, 73, SPRITE_BUG_BASE + 2);
        }
    } else if (fase == 1) {
        //FASE 2
        portal_px = 16;
        portal_py = 21;
        portal_cor = 0x00;

        jogador_px = 152;
        jogador_py = 463;

        adicionar_espinho_temporario(182, 358, POL_TRI_BAIXO, 0x03, 0xE3);
        adicionar_espinho_temporario(246, 358, POL_TRI_BAIXO, 0x03, 0xE3);
        adicionar_espinho_temporario(304, 380, POL_TRI_ESQ,0x03, 0xE3);
        adicionar_espinho_temporario(104, 318, POL_TRI_BAIXO, 0x03, 0xE3);
        adicionar_espinho_temporario(16, 464, POL_TRI_CIMA, 0x03, 0xE3);
        adicionar_espinho_temporario(71, 246, POL_TRI_BAIXO, 0x03, 0xE3);

        adicionar_espinho_temporario(184, 214, POL_TRI_BAIXO, 0x03, 0xE3);
        adicionar_espinho_temporario(158, 198, POL_TRI_BAIXO, 0x03, 0xE3);
        adicionar_espinho_temporario(168, 158, POL_TRI_BAIXO, 0x03, 0xE3);
        adicionar_espinho_temporario(158, 118, POL_TRI_BAIXO, 0x03, 0xE3);
        adicionar_espinho_temporario(168, 46, POL_TRI_BAIXO, 0x03, 0xE3);
        adicionar_espinho_temporario(288, 14, POL_TRI_BAIXO, 0x03, 0xE3);

        adicionar_patrulha_circular(128, 240, 176, 272, 2);
        adicionar_patrulha_circular(152, 48, 288, 64, 2);

        adicionar_botdog(340, 330, +1);
        adicionar_botdog(169, 161, -1);

        if (reinicializar_bugs) {
            num_bugs = 0;
            adicionar_bug_px(156, 320, SPRITE_BUG_BASE + 0);
            adicionar_bug_px(16, 362, SPRITE_BUG_BASE + 1);
            adicionar_bug_px(78, 19,  SPRITE_BUG_BASE + 2);
        }
    } else {
        //FASE 3
        portal_px = 154;
        portal_py = 0;
        portal_cor = 0x00;

        jogador_px = 13;
        jogador_py = 463;

        adicionar_espinho_temporario(40, 382, POL_TRI_BAIXO, 0xB5, 0x1C);
        adicionar_espinho_temporario(214, 350, POL_TRI_BAIXO, 0xB5, 0x1C);
        adicionar_espinho_temporario(232, 352, POL_TRI_CIMA,  0xB5, 0x1C);
        adicionar_espinho_temporario(254, 278, POL_TRI_BAIXO, 0xB5, 0x1C);
        adicionar_espinho_temporario(8, 336, POL_TRI_CIMA, 0xB5, 0x1C);
        adicionar_espinho_temporario(144, 272, POL_TRI_CIMA, 0xB5, 0x1C);

        
        adicionar_espinho_temporario(128, 206, POL_TRI_BAIXO, 0xB5, 0x1C);
        adicionar_espinho_temporario(8, 200, POL_TRI_CIMA, 0xB5, 0x1C);
        adicionar_espinho_temporario(32, 166, POL_TRI_BAIXO, 0xB5, 0x1C);
        adicionar_espinho_temporario(8, 38, POL_TRI_BAIXO, 0xB5, 0x1C);
        adicionar_espinho_temporario(232, 62, POL_TRI_BAIXO, 0xB5, 0x1C);
        adicionar_espinho_temporario(184, 62, POL_TRI_BAIXO, 0xB5, 0x1C);

        adicionar_patrulha_circular(144, 352, 208, 440, 2);
        adicionar_patrulha_circular(56, 208, 104, 280, -2);

        adicionar_ave_robo(110, 400, -1);
        adicionar_ave_robo(160, 184, -1);
        adicionar_ave_robo(128, 40, 1);

        adicionar_botdog(320, 312, +1);
        adicionar_botdog(140, 132, -1);

        if (reinicializar_bugs) {
            num_bugs = 0;
            adicionar_bug_px(296, 440, SPRITE_BUG_BASE + 0);
            adicionar_bug_px(63, 40, SPRITE_BUG_BASE + 1);
            adicionar_bug_px(236, 161, SPRITE_BUG_BASE + 2);
        }
    }
}

//atualiza poligonos e sprites das entidades visiveis na metade ativa da tela
void atualizar_visuais_objetos_fase(int frame_atual) {
    int offset_y = (etapa_fase == 0) ? 240 : 0;

    //atualiza o poligono de colisao/marcacao do portal de saida
    int portal_sy = portal_py - offset_y;
    if (portal_sy >= -16 && portal_sy < SCREEN_H) {
        pospo(POLY_PORTAL, portal_px, portal_sy);
        tampo(POLY_PORTAL, 13, 13);
        corpo(POLY_PORTAL, portal_cor);
        atpo(0, POLY_PORTAL, 1, POL_QUADRADO);
    } else {
        atpo(0, POLY_PORTAL, 0, POL_QUADRADO);
    }

    int poly_slot = 1;

    //desenha espinhos fixos da etapa ativa
    for (int i = 0; i < num_espinhos; i++) {
        EspinhoFase *e = &espinhos[i];
        int etapa_do_espinho = (e->py >= 240) ? 0 : 1;
        if (etapa_do_espinho != etapa_fase || poly_slot > 9) continue;

        int pid = poly_slot++;
        int w = (e->tipo_flip == POL_TRI_DIR || e->tipo_flip == POL_TRI_ESQ) ? 7 : 9;
        int h = (w == 7) ? 9 : 7;
        int sy = e->py - offset_y;

        if (sy >= 0 && sy + h <= SCREEN_H) {
            pospo(pid, e->px, sy);
            tampo(pid, w, h);
            corpo(pid, e->cor);
            atpo(e->tipo_flip, pid, 1, POL_TRI_EQUI);
        } else {
            atpo(0, pid, 0, POL_RETANGULO);
        }
    }

    //desenha marcadores ou espinhos ativos das armadilhas
    for (int i = 0; i < num_espinhos_temp; i++) {
        EspinhoTemporario *e = &espinhos_temp[i];
        int etapa_do_espinho = (e->marcador_y >= 240) ? 0 : 1;
        if (etapa_do_espinho != etapa_fase || poly_slot > 9) continue;

        int pid = poly_slot++;
        int horiz = (e->tipo_flip == POL_TRI_CIMA || e->tipo_flip == POL_TRI_BAIXO);
        int mw = horiz ? 9 : 1;
        int mh = horiz ? 1 : 9;
        int sy_m = e->marcador_y - offset_y;

        int sw = horiz ? 9 : 7;
        int sh = horiz ? 7 : 9;

        int sx = e->marcador_x;
        int sy_s = sy_m;

        if (e->tipo_flip == POL_TRI_CIMA) {
            sx   = e->marcador_x + 4;
            sy_s = sy_m - 8;
        } else if (e->tipo_flip == POL_TRI_BAIXO) {
            sx   = e->marcador_x + 4;
            sy_s = sy_m + 2;
        } else if (e->tipo_flip == POL_TRI_ESQ) {
            sx   = e->marcador_x - 4;
            sy_s = sy_m;
        } else if (e->tipo_flip == POL_TRI_DIR) {
            sx   = e->marcador_x + 5;
            sy_s = sy_m;
        }

        if (!e->ativo) {
          if (sy_m >= 0 && sy_m + mh <= SCREEN_H) {
              pospo(pid, e->marcador_x, sy_m);
              tampo(pid, mw, mh);
              uint8_t cor_marcador = e->armado ? ((frame_atual & 2) ? 0xE0 : 0xFC) : e->cor_base;
              corpo(pid, cor_marcador);
              atpo(0, pid, 1, POL_RETANGULO);
          } else {
              atpo(0, pid, 0, POL_RETANGULO);
          }
      } else {
          if (sy_s >= 0 && sy_s + sh <= SCREEN_H) {
              pospo(pid, sx, sy_s);
              tampo(pid, sw, sh);
              corpo(pid, e->cor_espinho);
              atpo(e->tipo_flip, pid, 1, POL_TRI_EQUI);
          } else {
              atpo(0, pid, 0, POL_RETANGULO);
          }
      }
    }

    //desativa os slots de poligonos de espinhos nao utilizados nesta metade
    for (int p = poly_slot; p <= 9; p++) {
        atpo(0, p, 0, POL_RETANGULO);
    }

    desenhar_ave_robo(offset_y, frame_atual);

    //atualiza sprites dos inimigos patrulheiros
    for (int i = 0; i < MAX_PATRULHAS_FASE; i++) {
        if (i >= num_patrulhas || !patrulhas[i].ativo) {
            atsp(SPRITE_PATRULHA_BASE + i, 0, 0);
            continue;
        }
        InimigoPatrulha *p = &patrulhas[i];
        int etapa_p = (p->py >= 240) ? 0 : 1;
        int sy = p->py - offset_y;

        if (etapa_p != etapa_fase || sy < -16 || sy >= SCREEN_H) {
            atsp(p->sprite_id, 0, 0);
        } else {
            int flip_p = (p->vx < 0) ? 1 : 0;
            rgsp(p->sprite_id, SLOT_PATRULHA);
            possp(p->sprite_id, p->px, sy);
            atsp(p->sprite_id, flip_p, 1);
        }
    }

    //atualiza sprites dos bugs coletaveis e sua animacao de coleta
    for (int i = 0; i < num_bugs; i++) {
        BugFase *b = &bugs[i];
        int sy = b->py - offset_y;

        if (sy < -16 || sy >= SCREEN_H) {
            atsp(b->sprite_id, 0, 0);
            continue;
        }

        if (b->vivo) {
            rgsp(b->sprite_id, SLOT_BUG_BASE);
            possp(b->sprite_id, b->px, sy);
            atsp(b->sprite_id, 0, 1);
        } else if (b->frame_morte >= 1 && b->frame_morte <= 6) {
            rgsp(b->sprite_id, SLOT_BUG_BASE + b->frame_morte);
            possp(b->sprite_id, b->px, sy);
            atsp(b->sprite_id, 0, 1);
            if ((frame_atual % 3) == 0) {
                b->frame_morte++;
                if (b->frame_morte > 6) {
                    b->frame_morte = 0;
                    atsp(b->sprite_id, 0, 0);
                }
            }
        } else {
            atsp(b->sprite_id, 0, 0);
        }
    }

    //atualiza os 5 sprites que formam o BotDog em movimento
    if (botdog_ativa) {
        int sy = botdog_y - offset_y;
        int slot_base = ((botdog_frames / 6) & 1) ? SLOT_BOTDOG1_BASE : SLOT_BOTDOG0_BASE;
        int flip_h = (botdog_dir > 0) ? 1 : 0;

        for (int i = 0; i < 5; i++) {
            int pos_idx = (botdog_dir > 0) ? (4 - i) : i;
            int sx = botdog_x + pos_idx * 16;

            if (sx <= -16 || sx >= SCREEN_W || sy <= -16 || sy >= SCREEN_H) {
                atsp(SPRITE_BOTDOG_BASE + i, flip_h, 0);
            } else {
                rgsp(SPRITE_BOTDOG_BASE + i, slot_base + i);
                possp(SPRITE_BOTDOG_BASE + i, sx, sy);
                atsp(SPRITE_BOTDOG_BASE + i, flip_h, 1);
            }
        }
    } else {
        for (int i = 0; i < 5; i++) {
            atsp(SPRITE_BOTDOG_BASE + i, 0, 0);
        }
    }

}

int mapa_parede_pixel(int x, int y) {
    if (x < 0 || x >= FASE1_LARGURA || y < 0 || y >= FASE1_ALTURA_TOTAL) return 1;
    return obter_paredes_fase(fase_atual)[y][x] != 0;
}

//atualiza os sprites das aves atiradoras e os poligonos de seus tiros
void desenhar_ave_robo(int offset_y, int frame_atual) {
    for (int i = 0; i < MAX_AVES_FASE; i++) {
        if (i >= num_aves || !aves[i].ativo || aves[i].etapa != etapa_fase) {
            atsp(SPRITE_AVE_BASE + i, 0, 0);
            if (i < num_aves) {
                atpo(0, aves[i].poly_id, 0, POL_QUADRADO);
            }
            continue;
        }

        AveRobo *c = &aves[i];

        int slot_ave = SLOT_AVE0;
        if (c->anim_frames > 0) {
            slot_ave = SLOT_AVE2;
        } else if (c->timer >= (DOG_INTERVALO_TIRO - 30)) {
            slot_ave = SLOT_AVE1;
        }

        int sy = c->y - offset_y;
        int recuo = (c->anim_frames > 10) ? -c->dir * 2 : 0;
        int flip_ave = (c->dir > 0) ? 0 : 1;

        rgsp(c->sprite_id, slot_ave);
        possp(c->sprite_id, c->x + recuo, sy);
        atsp(c->sprite_id, flip_ave, 1);

        //desenha explosao ao atingir parede/escudo ou o projetil em voo
        if (c->explosao_frames > 0) {
            int t  = 8 - c->explosao_frames;
            int s  = 4 + t * 2;
            int ex = c->explosao_x + 1 - s / 2;
            int ey = c->explosao_y - offset_y + 1 - s / 2;
            if (ex < 0) ex = 0;
            if (ey < 0) ey = 0;
            pospo(c->poly_id, ex, ey);
            tampo(c->poly_id, s, s);
            corpo(c->poly_id, (frame_atual & 1) ? 0xFC : 0xE0);
            atpo(0, c->poly_id, 1, POL_QUADRADO);
        } else if (c->tiro_ativo) {
            int ty = c->tiro_y - offset_y;
            if (ty >= 0 && ty < SCREEN_H) {
                pospo(c->poly_id, c->tiro_x, ty);
                tampo(c->poly_id, 3, 3);
                corpo(c->poly_id, 0xE0);
                atpo(0, c->poly_id, 1, POL_QUADRADO);
            } else {
                atpo(0, c->poly_id, 0, POL_QUADRADO);
            }
        } else {
            atpo(0, c->poly_id, 0, POL_QUADRADO);
        }
    }
}

//reconstroi toda a cena grafica da fase
void restaurar_cena_fase_atual(void) {
    ocultar_todos_sprites_e_poligonos();
    carregar_slots_objetos_jogo();
    carregar_fundo_etapa();

    int sy = jogador_py - ((etapa_fase == 0) ? 240 : 0);
    rgsp(SPRITE_JOGADOR, 0);
    possp(SPRITE_JOGADOR, jogador_px, sy);
    atsp(SPRITE_JOGADOR, flip_jogador, 1);

    atualizar_visuais_objetos_fase(0);
    prior(0);
}

//inicializa o estado completo de uma fase
void carregar_fase(int indice_fase, int reinicializar_bugs) {
    if (indice_fase < 0) indice_fase = 0;
    if (indice_fase > 2) indice_fase = 2;

    fase_atual = indice_fase;
    etapa_fase = 0;
    vel_tx = 0;
    vel_ty = 0;
    flip_jogador = 0;

    copiar_mascara_colisoes();
    configurar_layout_fase(indice_fase, reinicializar_bugs);

    botdog_ativa = 0;
    botdog_dir = 1;
    botdog_x = -80;
    tremor_tela = 0;

    portal_fugiu = 0;

    restaurar_cena_fase_atual();
}
