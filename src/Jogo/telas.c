#include <stdint.h>
#include <unistd.h>
#include "jogo.h"
#include "sprites_menu.h"
#include "telas_background.h"
#include "caracteres.h"
#include "mainCat.h"
#include "botoes.h"
#include "catDead.h"
#include "brilhos.h"

//envia os 76.800 pixels de uma tela de fundo para a ram
static void desenhar_background_dados(const uint8_t *dados) {
    for (int i = 0; i < BG_TAMANHO_RAM; i++) {
        btile(i, dados[i]);
    }
}

static void desenhar_bg_derrota(void) { desenhar_background_dados(bg_derrota_dados); }
static void desenhar_bg_fase01tela(void) { desenhar_background_dados(bg_fase01tela_dados); }
static void desenhar_bg_fase02tela(void) { desenhar_background_dados(bg_fase02tela_dados); }
static void desenhar_bg_fase03tela(void) { desenhar_background_dados(bg_fase03tela_dados); }
static void desenhar_bg_iniciogame(void) { desenhar_background_dados(bg_iniciogame_dados); }
static void desenhar_bg_pause(void) { desenhar_background_dados(bg_pause_dados); }
static void desenhar_bg_rank(void) { desenhar_background_dados(bg_rank_dados); }
static void desenhar_bg_tela_menu(void) { desenhar_background_dados(bg_tela_menu_dados); }
static void desenhar_bg_thecat(void) { desenhar_background_dados(bg_thecat_dados); }
static void desenhar_bg_vitoria(void) { desenhar_background_dados(bg_vitoria_dados); }
static void desenhar_bg_creditos(void) { desenhar_background_dados(bg_creditos_dados); }

//converte letra/numero ASCII para o indice no array
static int obter_indice_char(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a';
    if (c >= '0' && c <= '9') return 26 + (c - '0');
    return -1;
}

//grava 4 blocos 16x16 consecutivos na ram de sprites para formar um sprite 32x32
static void carregar_sprite32_na_ram(const uint8_t pixels[4][256], int offset_slot) {
    for (int s = 0; s < 4; s++) {
        int slot_ram = offset_slot + s;
        for (int p = 0; p < 256; p++) {
            wrsp((slot_ram * 256) + p, pixels[s][p]);
        }
    }
}

//posiciona e exibe um sprite 32x32 montado a partir de 4 sprites 16x16
static void exibir_sprite32_na_tela(int offset_slot, int x, int y, int id_sprite_base) {
    int idx = 0;
    for (int r = 0; r < 2; r++) {
        for (int c = 0; c < 2; c++) {
            int sp_id = id_sprite_base + idx;
            rgsp(sp_id, offset_slot + idx);
            possp(sp_id, x + (c * 16), y + (r * 16));
            atsp(sp_id, 0, 1);
            idx++;
        }
    }
}

//oculta os 4 sub-sprites que compoem um sprite 32x32
static void esconder_sprite32_da_tela(int id_sprite_base) {
    for (int i = 0; i < 4; i++) {
        atsp(id_sprite_base + i, 0, 0);
    }
}

static void carregar_cat0inicio_na_ram(void) { carregar_sprite32_na_ram(cat0inicio_pixels, CAT0INICIO_OFFSET); }
static void carregar_cat1inicio_na_ram(void) { carregar_sprite32_na_ram(cat1inicio_pixels, CAT1INICIO_OFFSET); }
static void carregar_cat2inicio_na_ram(void) { carregar_sprite32_na_ram(cat2inicio_pixels, CAT2INICIO_OFFSET); }
static void carregar_openFile_na_ram(void) { carregar_sprite32_na_ram(openFile_pixels, OPENFILE_OFFSET);   }
static void carregar_cat3inicio_na_ram(void) { carregar_sprite32_na_ram(cat3inicio_pixels, CAT3INICIO_OFFSET); }
static void carregar_cat4inicio_na_ram(void) { carregar_sprite32_na_ram(cat4inicio_pixels, CAT4INICIO_OFFSET); }

static void exibir_cat0inicio_na_tela(int x, int y, int id_base) { exibir_sprite32_na_tela(CAT0INICIO_OFFSET, x, y, id_base); }
static void exibir_cat1inicio_na_tela(int x, int y, int id_base) { exibir_sprite32_na_tela(CAT1INICIO_OFFSET, x, y, id_base); }
static void exibir_cat2inicio_na_tela(int x, int y, int id_base) { exibir_sprite32_na_tela(CAT2INICIO_OFFSET, x, y, id_base); }
static void exibir_openFile_na_tela(int x, int y, int id_base) { exibir_sprite32_na_tela(OPENFILE_OFFSET, x, y, id_base); }
static void exibir_cat3inicio_na_tela(int x, int y, int id_base) { exibir_sprite32_na_tela(CAT3INICIO_OFFSET, x, y, id_base); }
static void exibir_cat4inicio_na_tela(int x, int y, int id_base) { exibir_sprite32_na_tela(CAT4INICIO_OFFSET, x, y, id_base); }

#define carregar_cat3menu_na_ram carregar_cat3inicio_na_ram
#define carregar_cat4menu_na_ram carregar_cat4inicio_na_ram
#define exibir_cat3menu_na_tela exibir_cat3inicio_na_tela
#define exibir_cat4menu_na_tela exibir_cat4inicio_na_tela

//carrega os icones dos botoes da tela de pause na ram
static void carregar_todos_botoes_na_ram(void) {
    for (int p = 0; p < 256; p++) {
        wrsp((CLOSE_OFFSET * 256) + p, close_pixels[0][p]);
        wrsp((PLAY_OFFSET * 256) + p, play_pixels[0][p]);
        wrsp((REPLAY_OFFSET * 256) + p, replay_pixels[0][p]);
    }
}

static void exibir_close_na_tela(int x, int y, int sp_id) { rgsp(sp_id, CLOSE_OFFSET);  possp(sp_id, x, y); atsp(sp_id, 0, 1); }
static void exibir_play_na_tela(int x, int y, int sp_id) { rgsp(sp_id, PLAY_OFFSET); possp(sp_id, x, y); atsp(sp_id, 0, 1); }
static void exibir_replay_na_tela(int x, int y, int sp_id) { rgsp(sp_id, REPLAY_OFFSET); possp(sp_id, x, y); atsp(sp_id, 0, 1); }

//carrega os 7 frames de fumaca usados na animacao de morte do gato
static void carregar_todos_smokes_na_ram(void) {
    for (int p = 0; p < 256; p++) {
        wrsp((SMOKE0_OFFSET * 256) + p, smoke0_pixels[0][p]);
        wrsp((SMOKE1_OFFSET * 256) + p, smoke1_pixels[0][p]);
        wrsp((SMOKE2_OFFSET * 256) + p, smoke2_pixels[0][p]);
        wrsp((SMOKE3_OFFSET * 256) + p, smoke3_pixels[0][p]);
        wrsp((SMOKE4_OFFSET * 256) + p, smoke4_pixels[0][p]);
        wrsp((SMOKE5_OFFSET * 256) + p, smoke5_pixels[0][p]);
        wrsp((SMOKE6_OFFSET * 256) + p, smoke6_pixels[0][p]);
    }
}

static void exibir_smoke_frame(int frame, int x, int y, int sp_id) {
    if (frame < 0 || frame > 6) return;
    rgsp(sp_id, SMOKE0_OFFSET + frame);
    possp(sp_id, x, y);
    atsp(sp_id, 0, 1);
} 

//carrega os sprites de brilho usados no portal e na tela de ranking
static void carregar_todos_brilhos_na_ram(void) {
    for (int p = 0; p < 256; p++) {
        wrsp((BRILHO0_OFFSET * 256) + p, brilho0_pixels[0][p]);
        wrsp((BRILHO1_OFFSET * 256) + p, brilho1_pixels[0][p]);
        wrsp((BRILHO2_OFFSET * 256) + p, brilho2_pixels[0][p]);
        wrsp((BRILHOSDIREITA_OFFSET  * 256) + p, brilhosDireita_pixels[0][p]);
        wrsp((BRILHOSESQUERDA_OFFSET * 256) + p, brilhosEsquerda_pixels[0][p]);
    }
}

static void exibir_brilho_por_indice(int tipo_brilho, int x, int y, int sp_id) {
    if (tipo_brilho < 0 || tipo_brilho > 4) return;
    rgsp(sp_id, tipo_brilho);
    possp(sp_id, x, y);
    atsp(sp_id, 0, 1);
}

//desativa todos os 32 sprites e 16 poligonos antes de trocar de tela
void ocultar_todos_sprites_e_poligonos(void) {
    for (int i = 0; i < 32; i++) atsp(i, 0, 0);
    for (int p = 0; p < 16; p++) atpo(0, p, 0, POL_RETANGULO);
}

//pinta todo o background com uma unica cor
void preencher_background_pixel(uint8_t cor) {
    bscroll(0, 0);
    for (int i = 0; i < TOTAL_PIXELS_BG; i++) btile(i, cor);
}

//grava um padrao 16x16 em um slot especifico da ram
static void gravar_padrao_16x16(int slot, const uint8_t *pixels) {
    int end_base = slot * 256;
    for (int i = 0; i < 256; i++) wrsp(end_base + i, pixels[i]);
}

//carrega os digitos 0 a 9 nos slots 0 a 9 da ram
static void carregar_digitos_0_a_9_na_ram(void) {
    for (int d = 0; d < 10; d++) {
        gravar_padrao_16x16(d, fonte_pixels[26 + d]);
    }
}

//exibe um numero formatado com 4 digitos
static void exibir_numero_4_digitos(int valor, int x_inicial, int y, int sp_id_base) {
    if (valor < 0) valor = 0;
    if (valor > 9999) valor = 9999;

    int d[4];
    d[0] = (valor / 1000) % 10;
    d[1] = (valor / 100) % 10;
    d[2] = (valor / 10) % 10;
    d[3] =  valor % 10;

    for (int i = 0; i < 4; i++) {
        int id = sp_id_base + i;
        rgsp(id, d[i]);
        possp(id, x_inicial + i * 16, y);
        atsp(id, 0, 1);
    }
}

//carrega o primeiro frame das 3 skins de gato para exibicao nas telas de selecao e ranking
static void carregar_3_skins_preview(int s0, int s1, int s2) {
    gravar_padrao_16x16(s0, gato_skins_pixels[0][0]);
    gravar_padrao_16x16(s1, gato_skins_pixels[1][0]);
    gravar_padrao_16x16(s2, gato_skins_pixels[2][0]);
}

//tela inicial
void carregar_recursos_tela_inicial(void) {
    ocultar_todos_sprites_e_poligonos();
    preencher_background_pixel(COR_TELA_INIT);

    carregar_cat0inicio_na_ram();
    carregar_cat1inicio_na_ram();
    carregar_cat2inicio_na_ram();
    carregar_openFile_na_ram();
    prior(0);
}

int atualizar_tela_inicial(int frame_atual) {
    const int PASTA_X = 144;
    const int PASTA_Y = 120;

    if (frame_atual <= 50) {
        int gato_y = 10 + frame_atual * 2;
        exibir_openFile_na_tela(PASTA_X, PASTA_Y, 4);
        exibir_cat2inicio_na_tela(PASTA_X, gato_y, 0);
        return 0;
    }
    if (frame_atual <= 140) {
        if (frame_atual == 51) {
            esconder_sprite32_da_tela(0);
            esconder_sprite32_da_tela(4);
            exibir_cat1inicio_na_tela(PASTA_X, PASTA_Y, 0);
        }
        return 0;
    }
    if (frame_atual <= 230) {
        if (frame_atual == 141) {
            esconder_sprite32_da_tela(0);
            exibir_cat0inicio_na_tela(PASTA_X, PASTA_Y, 0);
        }
        return 0;
    }
    return 1;
}

//menu
void carregar_menu(void) {
    ocultar_todos_sprites_e_poligonos();
    bscroll(0, 0);
    desenhar_bg_tela_menu();

    carregar_cat3menu_na_ram();
    carregar_cat4menu_na_ram();

    gravar_padrao_16x16(8, fonte_pixels[15]);
    gravar_padrao_16x16(10, fonte_pixels[0]);
    gravar_padrao_16x16(9, fonte_pixels[11]);
    gravar_padrao_16x16(11, fonte_pixels[24]);

    const int PLAY_X = 39;
    const int PLAY_Y = 117;
    for (int i = 0; i < 4; i++) {
        rgsp(10 + i, 8 + i);
        possp(10 + i, PLAY_X + i * 14, PLAY_Y);
        atsp(10 + i, 0, 1);
    }
    prior(0);
}

//Animacao do gato da tela de play
void atualizar_animacao_menu(int frame_menu) {
    static const uint8_t CORES_PISCA[4] = { 0x4A, 0x6E, 0x93, 0x6E };
    uint8_t cor_pisca = CORES_PISCA[(frame_menu / 12) % 4];

    pospo(0, 31, 114);
    tampo(0, 74, 22);
    corpo(0, cor_pisca);
    atpo(0, 0, 1, POL_RETANGULO);

    if (((frame_menu / 20) % 2) == 0) {
        exibir_cat3menu_na_tela(226, 105, 0);
    } else {
        exibir_cat4menu_na_tela(226, 105, 0);
    }
}

static const int SKIN_POS_X[3] = {29, 59, 89};
static const int SKIN_POS_Y    = 120;

//selecionar skin
void carregar_tela_cat(void) {
    ocultar_todos_sprites_e_poligonos();
    bscroll(0, 0);
    desenhar_bg_thecat();

    carregar_3_skins_preview(0, 1, 2);

    for (int i = 0; i < 3; i++) {
        rgsp(i, i);
        possp(i, SKIN_POS_X[i], SKIN_POS_Y);
        atsp(i, 0, 1);
    }
    prior(0);
}

//Mudanca de cores dos poligonos na tela de escolha de skin
void atualizar_tela_cat(int frame_atual, int skin_sel) {
    static const uint8_t CORES_SEL[4] = { 0xFC, 0x93, 0x6E, 0x93 };
    uint8_t cor_ativa = CORES_SEL[(frame_atual / 10) % 4];

    for (int i = 0; i < 3; i++) {
        pospo(i, SKIN_POS_X[i] - 6, SKIN_POS_Y - 6);
        tampo(i, 28, 28);
        if (i == skin_sel) {
            corpo(i, cor_ativa);
            atpo(0, i, 1, POL_RETANGULO);
        } else {
            corpo(i, 0x24);
            atpo(0, i, 0, POL_RETANGULO);
        }
    }
}

//introducao do jogo
void carregar_tela_inicio_game(void) {
    ocultar_todos_sprites_e_poligonos();
    bscroll(0, 0);
    desenhar_bg_iniciogame();

    carregar_cat3menu_na_ram();
    carregar_cat4menu_na_ram();

    prior(0);
}

//intro das fases
void carregar_tela_intro_fase(int indice_fase) {
    ocultar_todos_sprites_e_poligonos();
    bscroll(0, 0);

    if (indice_fase == 0) {
        desenhar_bg_fase01tela();
    } else if (indice_fase == 1) {
        desenhar_bg_fase02tela();
    } else {
        desenhar_bg_fase03tela();
    }

    carregar_cat3menu_na_ram();
    carregar_cat4menu_na_ram();
    prior(0);
}

//Animacao da tela de introducao
void atualizar_tela_intro_fase(int frame_atual) {
    if (((frame_atual / 20) % 2) == 0) {
        exibir_cat3menu_na_tela(205, 37, 0);
    } else {
        exibir_cat4menu_na_tela(205, 37, 0);
    }
}

//tela de pause

static const int PAUSE_POLY_X[3] = {24, 54, 84};
static const int PAUSE_POLY_Y = 80;
static const int PAUSE_POLY_W = 20;
static const int PAUSE_POLY_H = 20;

void carregar_tela_pause(void) {
    ocultar_todos_sprites_e_poligonos();
    bscroll(0, 0);
    desenhar_bg_pause();

    carregar_todos_botoes_na_ram();

    exibir_close_na_tela(PAUSE_POLY_X[0] + 2, PAUSE_POLY_Y + 2, 0);
    exibir_replay_na_tela(PAUSE_POLY_X[1] + 2, PAUSE_POLY_Y + 2, 1);
    exibir_play_na_tela(PAUSE_POLY_X[2] + 2, PAUSE_POLY_Y + 2, 2);

    prior(0);
}

//Animacao dos pligonos da tela de pause
void atualizar_tela_pause(int frame_atual, int opcao_sel) {
    static const uint8_t CORES_PISCA[4] = { 0xFC, 0x93, 0x4A, 0x93 };
    const uint8_t COR_PARADO = 0x49;

    for (int i = 0; i < 3; i++) {
        pospo(i, PAUSE_POLY_X[i], PAUSE_POLY_Y);
        tampo(i, PAUSE_POLY_W, PAUSE_POLY_H);
        corpo(i, (i == opcao_sel) ? CORES_PISCA[(frame_atual / 10) % 4] : COR_PARADO);
        atpo(0, i, 1, POL_RETANGULO);
    }
}

//tela de vitoria
void carregar_tela_vitoria(int pontos_fase) {
    ocultar_todos_sprites_e_poligonos();
    bscroll(0, 0);
    desenhar_bg_vitoria();

    carregar_digitos_0_a_9_na_ram();
    exibir_numero_4_digitos(pontos_fase, 15, 100, 0);
    prior(0);
}

//tela de derrota
void carregar_tela_derrota(int pontos_fase) {
    ocultar_todos_sprites_e_poligonos();
    bscroll(0, 0);
    desenhar_bg_derrota();

    carregar_digitos_0_a_9_na_ram();
    exibir_numero_4_digitos(pontos_fase, 15, 100, 0);
    prior(0);
}

//tela de ranking
#define SLOT_RANK_BRILHO_DIR 18
#define SLOT_RANK_BRILHO_ESQ 19

void carregar_tela_rank(void) {
    ocultar_todos_sprites_e_poligonos();
    bscroll(0, 0);
    desenhar_bg_rank();

    carregar_digitos_0_a_9_na_ram();
    carregar_3_skins_preview(10, 11, 12);

    static const char PALAVRA_SCORE[5] = { 'S', 'C', 'O', 'R', 'E' };
    for (int i = 0; i < 5; i++) {
        gravar_padrao_16x16(13 + i, fonte_pixels[obter_indice_char(PALAVRA_SCORE[i])]);
    }

    gravar_padrao_16x16(SLOT_RANK_BRILHO_DIR, brilhosDireita_pixels[0]);
    gravar_padrao_16x16(SLOT_RANK_BRILHO_ESQ, brilhosEsquerda_pixels[0]);

    const int Y_INICIAL = 75;
    const int PASSO_Y = 30;
    const int X_COLOC = 14;
    const int X_GATO = 42;
    const int X_PONTOS = 74;

    int sp_id = 0;
    for (int r = 0; r < MAX_RANKING; r++) {
        if (!ranking[r].valido) continue;

        int y = Y_INICIAL + r * PASSO_Y;

        rgsp(sp_id, r + 1);
        possp(sp_id, X_COLOC, y);
        atsp(sp_id, 0, 1);
        sp_id++;

        int skin = ranking[r].skin_id;
        if (skin < 0 || skin > 2) skin = 0;
        rgsp(sp_id, 10 + skin);
        possp(sp_id, X_GATO, y);
        atsp(sp_id, 0, 1);
        sp_id++;

        exibir_numero_4_digitos(ranking[r].pontos_totais, X_PONTOS, y, sp_id);
        sp_id += 4;
    }

    const int SCORE_X = 5;
    const int SCORE_Y = 198;
    const int PONTUACAO_Y = 215;
    const int SP_SCORE_BASE = 24;
    const int SP_TOTAL_BASE = 18;

    for (int i = 0; i < 5; i++) {
        rgsp(SP_SCORE_BASE + i, 13 + i);
        possp(SP_SCORE_BASE + i, SCORE_X + i * 16, SCORE_Y);
        atsp(SP_SCORE_BASE + i, 0, 1);
    }

    exibir_numero_4_digitos(pontos_totais, SCORE_X, PONTUACAO_Y, SP_TOTAL_BASE);

    prior(0);
}

void atualizar_tela_rank(int frame_atual) {
    int slot_brilho = (((frame_atual / 15) % 2) == 0) ? SLOT_RANK_BRILHO_DIR : SLOT_RANK_BRILHO_ESQ;

    rgsp(29, slot_brilho);
    possp(29, 290, 38);
    atsp(29, 0, 1);

    rgsp(30, slot_brilho);
    possp(30, 156, 32);
    atsp(30, 0, 1);

    rgsp(31, slot_brilho);
    possp(31, 278, 119);
    atsp(31, 0, 1);
}

//tela de creditos

void carregar_tela_creditos(void) {
    ocultar_todos_sprites_e_poligonos();
    bscroll(0, 0);
    desenhar_bg_creditos();
    prior(0);
}

//animacao no joguinho

void animar_morte_gato(int px, int py) {
    carregar_todos_smokes_na_ram();
    for (int f = 0; f <= 6; f++) {
        exibir_smoke_frame(f, px, py, 0);
        noop();
        usleep(45000);
    }
    atsp(0, 0, 0);
}

//Animacao no portal de saida das fases
void animar_entrada_porta(int px, int py) {
    carregar_todos_brilhos_na_ram();
    const int seq[5] = { 0, 1, 2, 1, 0 };
    for (int i = 0; i < 5; i++) {
        exibir_brilho_por_indice(seq[i], px, py, SPRITE_BRILHO);
        noop();
        usleep(45000);
    }
    atsp(SPRITE_BRILHO, 0, 0);
}