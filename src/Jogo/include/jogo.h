#ifndef JOGO_H
#define JOGO_H

#include <stdint.h>
#include <pthread.h>
#include <stdbool.h>

#define SCREEN_W 320
#define SCREEN_H 240
#define TOTAL_PIXELS_BG (SCREEN_W * SCREEN_H)
#define TILES_X 40
#define VIEW_TILES_Y 30
#define MAP_TILES_Y 60
#define MAP_PIXEL_H 480

#define RGB332(r, g, b) ((uint8_t)(((r) & 0xE0) | (((g) & 0xE0) >> 3) | ((b) >> 6)))
#define COR_TELA_INIT 0x66

#define POL_RETANGULO 0
#define POL_QUADRADO 1
#define POL_TRI_EQUI 2
#define POL_TRI_ISOS 3
#define POL_TRI_ESCA 4

#define POL_TRI_CIMA 0
#define POL_TRI_BAIXO 1
#define POL_TRI_DIR 2
#define POL_TRI_ESQ 3

#ifndef SKIN_GATO_PRETO
#define SKIN_GATO_PRETO 0
#define SKIN_GATO_CINZA 1
#define SKIN_GATO_BRANCO 2
#endif
#define TOTAL_SKINS_GATO 3

typedef enum {
    ESTADO_TELA_INICIAL = 0,
    ESTADO_MENU_INICIAL,
    ESTADO_TELA_THE_CAT,
    ESTADO_TELA_INICIO_GAME,
    ESTADO_TELA_INTRO_FASE,
    ESTADO_JOGO_RODANDO,
    ESTADO_TELA_PAUSE,
    ESTADO_TELA_VITORIA,
    ESTADO_TELA_DERROTA,
    ESTADO_TELA_RANK,
    ESTADO_TELA_CREDITOS
} EstadoJogo;

typedef struct {
    int tecla_enter;
    int tecla_esc;
    int tecla_esq;
    int tecla_dir;
    int tecla_s;
    int tecla_p;
    int espaco_segurado;
    int dir_x;
    int dir_y;
} EstadoTeclado;

#define MAX_RANKING 3
typedef struct {
    int pontos_totais;
    int skin_id;
    int valido;
} RegistroRanking;

typedef struct {
    int px, py;
    int tipo_flip;
    uint8_t cor;
    int poly_id;
} EspinhoFase;

typedef struct {
    int marcador_x, marcador_y;
    int tipo_flip;
    uint8_t cor_base;
    uint8_t cor_espinho;
    int poly_id;
    int armado;
    int timer_armar;
    int ativo;
    int timer_ativo;
} EspinhoTemporario;

typedef struct {
    int x, y;
    int dir;
    int etapa;
    int tiro_x0, tiro_y0;
    int ativo;
    int sprite_id;
    int poly_id;
    int timer;
    int anim_frames;
    int tiro_ativo;
    int tiro_x, tiro_y;
    int explosao_frames;
    int explosao_x, explosao_y;
} AveRobo;

typedef struct {
    int y_gatilho;
    int y_pos;
    int dir;
    int disparado;
    int ativo;
} BotDogConfig;

#define TILE_VAZIO  0
#define TILE_PAREDE 1

#define MAX_ESPINHOS_FIXOS 16
#define MAX_TEMPORARIOS 16
#define MAX_BUGS_FASE 4
#define NUM_BUGS_FASE 3
#define MAX_PATRULHAS_FASE 6

#define SPRITE_JOGADOR 0
#define SPRITE_BUG_BASE 5
#define SPRITE_BOTDOG_BASE 10
#define SPRITE_BRILHO 16
#define SPRITE_PATRULHA_BASE 18
#define SPRITE_AVE_BASE 24

#ifndef SPRITE_DOG
#define SPRITE_DOG 27
#endif
#define POLY_DOG_TIRO 14
#define POLY_DOG_EXPLOSAO 15
#define DOG_INTERVALO_TIRO 120
#define DOG_VEL_TIRO 5
#define DOG_FLIP_P_DIREITA 1

int  ave_robo_config(AveRobo *c);

#define POLY_PORTAL 0
#define POLY_ESPINHO_BASE 1
#define POLY_ESPINHO_TEMP 7
#define POLY_PROJETIL_DOG 14
#define POLY_EXPLOSAO_DOG 15
#define MAX_AVES_FASE 4
#define MAX_BOTDOGS_FASE 4
#define POLY_AVE_TIRO_BASE 10

extern int mortes_fase;
extern AveRobo aves[MAX_AVES_FASE];
extern int num_aves;
extern BotDogConfig botdogs[MAX_BOTDOGS_FASE];
extern int num_botdogs;

typedef struct {
    int px, py;
    int vivo;
    int frame_morte;
    int sprite_id;
} BugFase;

typedef struct {
    int px, py;
    int x1, y1;
    int x2, y2;
    int vx, vy;
    int sprite_id;
    int ativo;
    int circular;
} InimigoPatrulha;

extern volatile EstadoJogo estado_atual;
extern volatile int jogo_rodando;
extern int skin_selecionada;
extern int fase_atual;
extern int pontos_fase;
extern int pontos_totais;
extern int bugs_coletados;
extern int jogador_px;
extern int jogador_py;
extern int etapa_fase;
extern int vel_tx;
extern int vel_ty;
extern int flip_jogador;
extern uint8_t mapa_atual[MAP_TILES_Y][TILES_X];
extern EspinhoFase espinhos[MAX_ESPINHOS_FIXOS];
extern int num_espinhos;
extern EspinhoTemporario espinhos_temp[MAX_TEMPORARIOS];
extern int num_espinhos_temp;
extern BugFase bugs[MAX_BUGS_FASE];
extern int num_bugs;
extern InimigoPatrulha patrulhas[MAX_PATRULHAS_FASE];
extern int num_patrulhas;
extern int portal_px;
extern int portal_py;
extern int portal_fugiu;
extern uint8_t portal_cor;
extern RegistroRanking ranking[MAX_RANKING];

extern int botdog_ativa;
extern int botdog_disparada;
extern int botdog_dir;
extern int botdog_x;
extern int botdog_y;
extern int botdog_frames;
extern int tremor_tela;


extern int dog_tiro_ativo;
extern int dog_tiro_x;
extern int dog_tiro_y;
extern int dog_timer;
extern int dog_anim_frames;
extern int dog_explosao_frames;
extern int dog_explosao_x;
extern int dog_explosao_y;

//driver
int gpu_init(void);
void gpu_close(void);
int clear_status(void);
int noop(void);
int bscroll(int x, int y);
int btile(int end, int cor);
int possp(int id, int x, int y);
int atsp(int id, int flip, int show);
int wrsp(int end, int cor);
int rgsp(int id, int slot);
int atpo(int flip, int id, int show, int tipo);
int pospo(int id, int x, int y);
int tampo(int id, int larg, int alt);
int corpo(int id, int cor);
int prior(int p);

//funções de status
int status(void);
int busy(void);
int fifo_full(void);
int fifo_empty(void);
int error(void);
int warning(void);
int frame_counter(void);
uint32_t obter_e_zerar_instrucoes(void);
uint32_t obter_mascara_sprites_ativos(void);
uint32_t obter_mascara_poligonos_ativos(void);

//teclado e threads
void *thread_teclado_usb(void *arg);
void ler_botoes_estado(EstadoTeclado *copia);
void limpar_direcoes_teclado(void);

//funções de métricas e ranking
double obter_tempo_atual_segundos(void);
void carregar_ranking_arquivo(void);
void salvar_ranking_arquivo(void);
void registrar_no_ranking(int pontos_totais, int skin_id);
void metricas_registrar_frame(double duracao_segundos);

//telas
void ocultar_todos_sprites_e_poligonos(void);
void preencher_background_pixel(uint8_t cor);
void pintar_bloco_8x8_solido(int tx, int ty, uint8_t cor);
void carregar_recursos_tela_inicial(void);
int atualizar_tela_inicial(int frame_atual);
void carregar_menu(void);
void atualizar_animacao_menu(int frame_menu);
void carregar_tela_the_cat(void);
void atualizar_tela_the_cat(int frame_atual, int skin_sel);
void carregar_tela_inicio_game(void);
void carregar_tela_intro_fase(int indice_fase);
void atualizar_tela_intro_fase(int frame_atual);
void carregar_tela_pause(void);
void atualizar_tela_pause(int frame_atual, int opcao_sel);
void carregar_tela_vitoria(int pontos_fase);
void carregar_tela_derrota(int pontos_fase);
void carregar_tela_rank(void);
void atualizar_tela_rank(int frame_atual);
void animar_morte_gato(int px, int py);
void animar_entrada_porta(int px, int py);
void carregar_tela_creditos(void);

//mapa e Lógica
void carregar_fase(int indice_fase, int reinicializar_bugs);
void carregar_etapa_fundo_ram(void);
void restaurar_cena_fase_atual(void);
void atualizar_etapa_camera(int jogador_py);
void atualizar_visuais_objetos_fase(int frame_atual);
void executar_estado_jogando(int espaco_segurado, int frame_atual);
int posicao_jogador_no_endgame(void);
int checar_colisoes(int espaco_segurado);
int ponto_bate_em_parede(int px, int py, int w, int h);
void atualizar_ave_robo(void);
void desenhar_ave_robo(int offset_y, int frame_atual);
int mapa_parede_pixel(int x, int y);
void apagar_porta_topo_fase3(void);

#endif