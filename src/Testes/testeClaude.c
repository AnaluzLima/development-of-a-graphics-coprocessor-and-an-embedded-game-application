/* ============================================================================
 *  Bateria Completa de Testes, Diagnostico e Telemetria - Coprocessador Grafico
 *  DE1-SoC (HPS ARM + FPGA)  |  Driver: libAssembly.S
 *
 *  Compilar na DE1-SoC (C + driver Assembly juntos):
 *      gcc -O2 -Wall teste_coprocessador.c libAssembly.S -o teste -lrt
 *
 *  Executar (root, por causa do mmap de /dev/mem):
 *      sudo ./teste                  modo interativo (ENTER avanca, q+ENTER sai)
 *      sudo ./teste -t 1500          modo automatico (avanca a cada 1500 ms)
 *      sudo ./teste -t 0 -s 20       "smoke test" rapido: sem pausas, 20% das amostras
 *      sudo ./teste -c 17            somente a cena 17 (flip dos poligonos)
 *      sudo ./teste -c 1-4,8         lista/intervalo de cenas
 *      sudo ./teste -o resultados    exporta CSVs (resultados_*.csv)
 *      sudo ./teste -l               lista as cenas
 *      sudo ./teste -Q               silencia as linhas [ OK ] (mostra so falhas)
 *
 *  Codigo de saida: 0 = todas as verificacoes passaram | 2 = houve falhas.
 *
 *  DRIVER NOVO (libAssembly_1.S) - o que esta bateria cobre alem da ISA:
 *    - atpo(flip, id, show, tipo)   <-- ORDEM NOVA dos argumentos (r0=flip, r1=id, r2=show, r3=tipo)
 *    - busy()                       le PIO_BUSY (0x30000); enviar_instrucao espera busy==0 antes de enviar
 *    - obter_e_zerar_instrucoes()   contador de envios bem-sucedidos (le e zera)
 *    - obter_mascara_sprites_ativos() / obter_mascara_poligonos_ativos()   mascaras de software
 *      (bit=1 -> show=1 pedido), atualizadas por atsp()/atpo() ANTES do envio.
 *    Toda etapa visual reconcilia o contador e as mascaras do driver com um modelo do teste.
 *
 *  ARQUITETURA (conforme o RTL atual do projeto)
 *    - Tela logica 320x240 -> VGA 640x480@60 Hz; cor RGB332 (RRRGGGBB); cor 0x00
 *      e TRANSPARENTE em sprites e poligonos (no fundo, 0x00 e preto normal).
 *    - FUNDO: mapa por PIXEL (320x240 = 76800 posicoes, M10K). BTILE escreve 1 pixel:
 *      END = y*320 + x. BSCROLL continua em unidades de TILE 8x8 (X 0..39, Y 0..29),
 *      com wrap, e e travado no VSYNC (so vale no quadro seguinte).
 *    - Instrucao 32 bits: [31:5] operandos | [4:0] opcode. FIFO de 16 posicoes.
 *
 *  ISA (opcodes 0..12) e checagens de wrn_param no hardware:
 *     0 NOOP    bit5=0 -> UC espera borda de descida do VSYNC | bit5=1 -> NOOP rapido
 *     1 BSCROLL X[15:10] Y[9:5]            wrn se X>=40 ou Y>=30
 *     2 BTILE   END[29:13] COR[12:5]       wrn se END>=76800 (e a escrita e descartada)
 *     3 POSSP   ID[26:22] X[21:13] Y[12:5] wrn se X>=320 ou Y>=240
 *     4 ATSP    ID[12:8] FLIP[7:6] SHOW[5]
 *     5 WRSP    END[25:13] COR[12:5]       (slot*256 + y*16 + x)
 *     6 RGSP    ID[14:10] SLOT[9:5]
 *     7 ATPO    FLIP[14:13] ID[12:9] SHOW[8] TIPO[7:5]   wrn se TIPO>4
 *     8 POSPO   ID[25:22] X[21:13] Y[12:5] wrn se X>=320 ou Y>=240
 *     9 TAMPO   ID[25:22] LARG[21:13] ALT[12:5]  wrn se LARG>=320 ou ALT>=240
 *    10 CORPO   ID[16:13] COR[12:5]
 *    11 PRIOR   bit5: 0 sprites acima | 1 poligonos acima
 *    12 CLST    limpa err_opcode e wrn_param
 *    13..31     invalidos -> err_opcode = 1 (flag retentiva)
 *
 *  FLIP DOS POLIGONOS (novo): so afeta tipos 2 (equilatero), 3 (isosceles), 4 (escaleno).
 *    0=CIMA  1=BAIXO  2=DIREITA  3=ESQUERDA.  Tipos 0 e 1 ignoram o flip.
 *
 *  PIO_STATUS (0x60000):  [0] fifo_full | [1] fifo_empty | [2] err_opcode |
 *    [3] wrn_param | [4] vsync_wait | [15:5] zeros | [31:16] frame_counter
 * ============================================================================ */

#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <signal.h>
#include <stdarg.h>

/* ============================================================================
 *  1. Driver Assembly (libAssembly.S)
 * ========================================================================== */
extern int  gpu_init(void);
extern void gpu_close(void);
extern int  enviar_instrucao(uint32_t instr);

extern int  status(void);
extern int  fifo_full(void);
extern int  fifo_empty(void);
extern int  error(void);
extern int  warning(void);
extern int  frame_counter(void);
extern int  busy(void);                                  /* PIO_BUSY bit 0 */
extern unsigned obter_e_zerar_instrucoes(void);          /* envios OK desde a ultima leitura; zera */
extern unsigned obter_mascara_sprites_ativos(void);      /* 32 bits: bit i = sprite i com show=1 */
extern unsigned obter_mascara_poligonos_ativos(void);    /* 16 bits: bit i = poligono i com show=1 */

extern int  clear_status(void);
extern int  noop(void);
extern int  bscroll(unsigned x, unsigned y);
extern int  btile(unsigned end, unsigned cor);
extern int  possp(unsigned id, unsigned x, unsigned y);
extern int  atsp(unsigned id, unsigned flip, unsigned show);
extern int  wrsp(unsigned end, unsigned cor);
extern int  rgsp(unsigned id, unsigned slot);
extern int  atpo(unsigned flip, unsigned id, unsigned show, unsigned tipo); /* ORDEM do libAssembly_1.S */
extern int  pospo(unsigned id, unsigned x, unsigned y);
extern int  tampo(unsigned id, unsigned larg, unsigned alt);
extern int  corpo(unsigned id, unsigned cor);
extern int  prior(unsigned p);

/* Unico ponto que conhece a ordem de argumentos do driver: (id, show, tipo, flip) -> asm */
static inline int atpo_hw(unsigned id, unsigned show, unsigned tipo, unsigned flip)
{
    return atpo(flip, id, show, tipo);
}

/* ============================================================================
 *  2. Constantes de hardware e paleta
 * ========================================================================== */
#define LARGURA        320
#define ALTURA         240
#define N_PIXELS_BG    (LARGURA * ALTURA)      /* 76800 posicoes do mapa de fundo */
#define TILES_X        40
#define TILES_Y        30
#define N_SPRITES      32
#define N_SPRITE_RAM   32
#define SPR_TAM        16
#define N_POLIGONOS    16

#define ST_BIT_FIFO_FULL   (1u << 0)
#define ST_BIT_FIFO_EMPTY  (1u << 1)
#define ST_BIT_ERR_OPCODE  (1u << 2)
#define ST_BIT_WRN_PARAM   (1u << 3)
#define ST_BIT_VSYNC_WAIT  (1u << 4)
#define ST_MASK_RESERVADO  0x0000FFE0u          /* bits [15:5] devem ser 0 */
#define ST_SHIFT_FC        16
#define FIFO_PROFUNDIDADE  16

#define VSYNC_NOMINAL_NS   16683000ULL          /* 640x480 @ 59.94 Hz */
#define STALL_NS           500000ULL            /* chamada > 0,5 ms = bloqueio/backpressure */

enum {
    OP_NOOP = 0, OP_NOOP_RAPIDO, OP_BSCROLL, OP_BTILE, OP_POSSP, OP_ATSP, OP_WRSP,
    OP_RGSP, OP_ATPO, OP_POSPO, OP_TAMPO, OP_CORPO, OP_PRIOR, OP_CLST, OP_RAW_INV,
    N_OPS
};
static const char *NOMES_OP[N_OPS] = {
    "noop(vsync)", "noop(rapido)", "bscroll", "btile", "possp", "atsp", "wrsp",
    "rgsp", "atpo", "pospo", "tampo", "corpo", "prior", "clst", "raw/invalido"
};

enum { POLY_RETANGULO = 0, POLY_QUADRADO, POLY_EQUILATERO, POLY_ISOSCELES, POLY_ESCALENO };
enum { FLIP_CIMA = 0, FLIP_BAIXO = 1, FLIP_DIREITA = 2, FLIP_ESQUERDA = 3 };
static const char *NOME_TIPO[5] = { "Retangulo", "Quadrado", "Equilatero", "Isosceles", "Escaleno" };

/* Slots do sprites.mif */
#define SLOT_GATO_A      0
#define SLOT_GATO_B      1
#define SLOT_FOGO        6
#define SLOT_DINAMICO   18   /* unico slot sobrescrito por WRSP nos testes */
#define SLOT_LETRA_G    20   /* slots 20..31 = "G A T i N H O S P B L 1"   */

#define COR_TRANSP       0x00
#define COR_FUNDO_PRETO  0x00   /* no fundo, 0x00 e preto comum */
#define COR_PRETO_SOLIDO 0x01
#define COR_AZUL_ESCURO  0x02
#define COR_AZUL         0x03
#define COR_AZUL_CEU     0x5F
#define COR_VERDE_ESCURO 0x08
#define COR_VERDE        0x1C
#define COR_CIANO        0x1F
#define COR_VERMELHO     0xE0
#define COR_VINHO        0x60
#define COR_ROSA_CHOQUE  0xE3
#define COR_LARANJA      0xF0
#define COR_OURO         0xF4
#define COR_AMARELO      0xFC
#define COR_BRANCO       0xFF
#define COR_CINZA_ESCURO 0x49
#define COR_CINZA        0x92
#define COR_ROXO         0x62

/* ============================================================================
 *  3. Estado global, verificacoes (PASS/FAIL) e utilidades
 * ========================================================================== */
#define MAX_CENAS 32
#define HIST_BINS 28

static int  g_tempo_ms   = -1;      /* -1 = ENTER | >=0 = automatico */
static int  g_escala     = 100;     /* % das amostras/quadros (-s) */
static int  g_verbose    = 1;       /* imprime linhas [ OK ] */
static int  g_auto_flags = 1;       /* toda etapa exige err=0 e wrn=0 ao final */
static int  g_cena_atual = 0;
static const char *g_csv_prefixo = NULL;
static volatile sig_atomic_t g_sair = 0;

static uint64_t g_overhead_timer_ns = 0;
static uint64_t g_periodo_ns        = VSYNC_NOMINAL_NS;
static unsigned long g_total_enviadas = 0;

static unsigned long g_ck_ok[MAX_CENAS], g_ck_fail[MAX_CENAS], g_ck_info[MAX_CENAS];
static char g_falhas[64][330];
static int  g_n_falhas = 0;

static uint64_t agora_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

static void espera_ativa_ns(uint64_t ns)
{
    uint64_t t0 = agora_ns();
    while (agora_ns() - t0 < ns) { }
}

static void dormir_ms(int ms)
{
    struct timespec ts;
    if (ms <= 0) return;
    ts.tv_sec  = ms / 1000;
    ts.tv_nsec = (long)(ms % 1000) * 1000000L;
    nanosleep(&ts, NULL);
}

static int esc(int n)
{
    long v = (long)n * g_escala / 100;
    return (v < 1) ? 1 : (int)v;
}

static void checar(int cond, const char *fmt, ...)
{
    char buf[300];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    if (cond) {
        g_ck_ok[g_cena_atual]++;
        if (g_verbose) printf("      [ OK  ] %s\n", buf);
    } else {
        g_ck_fail[g_cena_atual]++;
        printf("      [FALHA] %s\n", buf);
        if (g_n_falhas < 64)
            snprintf(g_falhas[g_n_falhas++], sizeof g_falhas[0], "Cena %02d: %s", g_cena_atual, buf);
    }
}

static void informar(const char *fmt, ...)
{
    char buf[300];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    g_ck_info[g_cena_atual]++;
    printf("      [INFO ] %s\n", buf);
}

/* ---- Estatistica de amostras (ns) ---------------------------------------- */
typedef struct {
    size_t   n;
    double   media, desvio;
    uint64_t min, p50, p90, p99, p999, max;
} Estat;

static int cmp_u64(const void *a, const void *b)
{
    uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b;
    return (x > y) - (x < y);
}

static double raiz(double x)
{
    double r;
    int i;
    if (x <= 0.0) return 0.0;
    r = (x > 1.0) ? x / 2.0 : 1.0;
    for (i = 0; i < 60; i++) r = 0.5 * (r + x / r);
    return r;
}

static Estat estat_calc(uint64_t *v, size_t n)
{
    Estat e;
    size_t i;
    double soma = 0.0, q = 0.0;
    memset(&e, 0, sizeof e);
    if (n == 0) return e;
    qsort(v, n, sizeof v[0], cmp_u64);
    for (i = 0; i < n; i++) soma += (double)v[i];
    e.n = n;
    e.media = soma / (double)n;
    for (i = 0; i < n; i++) { double d = (double)v[i] - e.media; q += d * d; }
    e.desvio = raiz(q / (double)n);
    e.min  = v[0];
    e.max  = v[n - 1];
    e.p50  = v[(size_t)(0.50  * (double)(n - 1) + 0.5)];
    e.p90  = v[(size_t)(0.90  * (double)(n - 1) + 0.5)];
    e.p99  = v[(size_t)(0.99  * (double)(n - 1) + 0.5)];
    e.p999 = v[(size_t)(0.999 * (double)(n - 1) + 0.5)];
    return e;
}

static int bin_log2(uint64_t ns)
{
    int b = 0;
    while (ns > 1 && b < HIST_BINS - 1) { ns >>= 1; b++; }
    return b;
}

static void imprimir_hist(const char *titulo, const unsigned long *h)
{
    unsigned long total = 0, mx = 0;
    int b, lo = -1, hi = -1;
    for (b = 0; b < HIST_BINS; b++) {
        total += h[b];
        if (h[b] > mx) mx = h[b];
        if (h[b]) { if (lo < 0) lo = b; hi = b; }
    }
    if (total == 0) return;
    printf("      Histograma (%s) - classes log2 da latencia:\n", titulo);
    for (b = lo; b <= hi; b++) {
        int k, barras = (int)(40.0 * (double)h[b] / (double)mx);
        double a = (double)(1ULL << b) / 1e3, z = (double)(1ULL << (b + 1)) / 1e3;
        printf("        %9.2f .. %9.2f us | %9lu ", a, (b == HIST_BINS - 1) ? 1e9 : z, h[b]);
        for (k = 0; k < barras; k++) putchar('#');
        putchar('\n');
    }
}

/* ---- Status do hardware ---------------------------------------------------- */
typedef struct {
    uint32_t raw;
    unsigned full, empty, err, wrn, wait, fc;
    int d_full, d_empty, d_err, d_wrn, d_fc;
    int busy;
} Snap;

static uint32_t raw_status(void) { return (uint32_t)status(); }

static Snap ler_snap(void)
{
    Snap s;
    s.raw   = raw_status();
    s.full  = (s.raw & ST_BIT_FIFO_FULL)  ? 1u : 0u;
    s.empty = (s.raw & ST_BIT_FIFO_EMPTY) ? 1u : 0u;
    s.err   = (s.raw & ST_BIT_ERR_OPCODE) ? 1u : 0u;
    s.wrn   = (s.raw & ST_BIT_WRN_PARAM)  ? 1u : 0u;
    s.wait  = (s.raw & ST_BIT_VSYNC_WAIT) ? 1u : 0u;
    s.fc    = (s.raw >> ST_SHIFT_FC) & 0xFFFFu;
    s.d_full  = fifo_full();
    s.d_empty = fifo_empty();
    s.d_err   = error();
    s.d_wrn   = warning();
    s.d_fc    = frame_counter();
    s.busy    = busy();
    return s;
}

/* Espera a UC ficar ociosa: FIFO vazia e fora de vsync_wait, estavel em 3 leituras
 * (a UC passa 2-3 ciclos em GET_INST/EXEC com a FIFO ja vazia). -1 = timeout. */
static int64_t uc_drenar(uint64_t timeout_ns)
{
    uint64_t t0 = agora_ns();
    int estavel = 0;
    for (;;) {
        uint32_t r = raw_status();
        if ((r & ST_BIT_FIFO_EMPTY) && !(r & ST_BIT_VSYNC_WAIT)) {
            if (++estavel >= 3) return (int64_t)(agora_ns() - t0);
        } else {
            estavel = 0;
        }
        if (agora_ns() - t0 > timeout_ns) return -1;
    }
}

/* Espera o frame_counter sair de fc_ref. Retorna o instante da deteccao (0 = timeout). */
static uint64_t aguardar_fc_mudar(uint32_t fc_ref, uint32_t *fc_novo, uint64_t timeout_ns)
{
    uint64_t t0 = agora_ns();
    for (;;) {
        uint32_t fc = raw_status() >> ST_SHIFT_FC;
        uint64_t t  = agora_ns();
        if (fc != fc_ref) { if (fc_novo) *fc_novo = fc; return t; }
        if (t - t0 > timeout_ns) return 0;
    }
}

static uint64_t sincronizar_inicio_de_quadro(uint32_t *fc_novo)
{
    uint32_t fc0 = raw_status() >> ST_SHIFT_FC;
    return aguardar_fc_mudar(fc0, fc_novo, 200000000ULL);
}

/* ============================================================================
 *  4. Telemetria por instrucao, janela por etapa e wrappers instrumentados
 * ========================================================================== */
typedef struct {
    unsigned long n;
    uint64_t soma, min, max;
    double   soma_q;
} Acum;

static Acum g_op[N_OPS];
static unsigned long g_hist_rap[HIST_BINS], g_hist_noop[HIST_BINS];

typedef struct {
    unsigned long n_rap, n_stall, n_noop;
    uint64_t ns_rap, ns_min, ns_max, ns_stall, ns_noop;
    double   soma_q;
    uint64_t t_ini;
    uint32_t fc_ini;
} Janela;

static Janela g_jan;

/* Modelo do teste para o que o driver novo guarda em software */
static unsigned long g_ctr_ref   = 0;      /* g_total_enviadas na ultima leitura/zeragem do contador */
static uint32_t g_mask_sp_mod    = 0;      /* sprites com show=1 pedido */
static uint32_t g_mask_po_mod    = 0;      /* poligonos com show=1 pedido */

static void modelo_mascara_atsp(unsigned id, unsigned show)
{
    uint32_t b = 1u << (id & 31u);
    if (show & 1u) g_mask_sp_mod |= b; else g_mask_sp_mod &= ~b;
}

static void modelo_mascara_atpo(unsigned id, unsigned show)
{
    uint32_t b = 1u << (id & 15u);
    if (show & 1u) g_mask_po_mod |= b; else g_mask_po_mod &= ~b;
}

static void resetar_janela(void)
{
    memset(&g_jan, 0, sizeof g_jan);
    g_jan.ns_min = UINT64_MAX;
    g_jan.t_ini  = agora_ns();
    g_jan.fc_ini = raw_status() >> ST_SHIFT_FC;
}

static void zerar_metricas(void)
{
    int i;
    memset(g_op, 0, sizeof g_op);
    for (i = 0; i < N_OPS; i++) g_op[i].min = UINT64_MAX;
    memset(g_hist_rap, 0, sizeof g_hist_rap);
    memset(g_hist_noop, 0, sizeof g_hist_noop);
}

static void limpar_tela_padrao(void);
static void imprimir_resumo_final(void);

static void abortar_erro_hw(const char *nome_fn)
{
    uint32_t r = raw_status();
    fprintf(stderr,
        "\n[ERRO FATAL] Timeout no handshake de '%s' (instrucao #%lu).\n"
        "             PIO_STATUS = 0x%08X (full=%u empty=%u err=%u wrn=%u vsync_wait=%u fc=%u)\n"
        "             Verifique: bitstream (.sof) carregado, KEY0 (reset) solto, VGA conectado.\n",
        nome_fn, g_total_enviadas + 1, r,
        (r & ST_BIT_FIFO_FULL) ? 1u : 0u, (r & ST_BIT_FIFO_EMPTY) ? 1u : 0u,
        (r & ST_BIT_ERR_OPCODE) ? 1u : 0u, (r & ST_BIT_WRN_PARAM) ? 1u : 0u,
        (r & ST_BIT_VSYNC_WAIT) ? 1u : 0u, r >> ST_SHIFT_FC);
    gpu_close();
    exit(1);
}

static inline void registrar(int op, uint64_t dt, int rc, const char *nome)
{
    Acum *a = &g_op[op];
    if (rc != 0) abortar_erro_hw(nome);
    g_total_enviadas++;
    a->n++;
    a->soma += dt;
    a->soma_q += (double)dt * (double)dt;
    if (dt < a->min) a->min = dt;
    if (dt > a->max) a->max = dt;

    if (op == OP_NOOP) {
        g_jan.n_noop++;
        g_jan.ns_noop += dt;
        g_hist_noop[bin_log2(dt)]++;
    } else {
        g_jan.n_rap++;
        g_jan.ns_rap += dt;
        g_jan.soma_q += (double)dt * (double)dt;
        if (dt < g_jan.ns_min) g_jan.ns_min = dt;
        if (dt > g_jan.ns_max) g_jan.ns_max = dt;
        if (dt > STALL_NS) { g_jan.n_stall++; g_jan.ns_stall += dt; }
        g_hist_rap[bin_log2(dt)]++;
    }
}

#define TIMED(OP, NOME, CHAMADA) do {                    \
        uint64_t t0_ = agora_ns();                       \
        int rc_ = (CHAMADA);                             \
        registrar((OP), agora_ns() - t0_, rc_, (NOME));  \
    } while (0)

static void cmd_noop(void)                        { TIMED(OP_NOOP, "noop", noop()); }
static void cmd_noop_rapido(void)                 { TIMED(OP_NOOP_RAPIDO, "noop_rapido", enviar_instrucao(1u << 5)); }
static void cmd_bscroll(unsigned x, unsigned y)   { TIMED(OP_BSCROLL, "bscroll", bscroll(x, y)); }
static void cmd_btile(unsigned e, unsigned c)     { TIMED(OP_BTILE, "btile", btile(e, c)); }
static void cmd_possp(unsigned i, unsigned x, unsigned y) { TIMED(OP_POSSP, "possp", possp(i, x, y)); }
static void cmd_atsp(unsigned i, unsigned f, unsigned s)  { modelo_mascara_atsp(i, s); TIMED(OP_ATSP, "atsp", atsp(i, f, s)); }
static void cmd_wrsp(unsigned e, unsigned c)      { TIMED(OP_WRSP, "wrsp", wrsp(e, c)); }
static void cmd_rgsp(unsigned i, unsigned s)      { TIMED(OP_RGSP, "rgsp", rgsp(i, s)); }
static void cmd_atpo(unsigned i, unsigned sh, unsigned t, unsigned f) { modelo_mascara_atpo(i, sh); TIMED(OP_ATPO, "atpo", atpo_hw(i, sh, t, f)); }
static void cmd_pospo(unsigned i, unsigned x, unsigned y) { TIMED(OP_POSPO, "pospo", pospo(i, x, y)); }
static void cmd_tampo(unsigned i, unsigned l, unsigned a) { TIMED(OP_TAMPO, "tampo", tampo(i, l, a)); }
static void cmd_corpo(unsigned i, unsigned c)     { TIMED(OP_CORPO, "corpo", corpo(i, c)); }
static void cmd_prior(unsigned p)                 { TIMED(OP_PRIOR, "prior", prior(p)); }
static void cmd_clear_status(void)                { TIMED(OP_CLST, "clear_status", clear_status()); }
static void cmd_raw(uint32_t w)                   { TIMED(OP_RAW_INV, "enviar_instrucao", enviar_instrucao(w)); }

/* Despacho generico (tabelas, fuzz): a,b,c,d = argumentos na ordem do driver */
static void cmd_gen(int op, unsigned a, unsigned b, unsigned c, unsigned d)
{
    switch (op) {
    case OP_NOOP:        cmd_noop(); break;
    case OP_NOOP_RAPIDO: cmd_noop_rapido(); break;
    case OP_BSCROLL:     cmd_bscroll(a, b); break;
    case OP_BTILE:       cmd_btile(a, b); break;
    case OP_POSSP:       cmd_possp(a, b, c); break;
    case OP_ATSP:        cmd_atsp(a, b, c); break;
    case OP_WRSP:        cmd_wrsp(a, b); break;
    case OP_RGSP:        cmd_rgsp(a, b); break;
    case OP_ATPO:        cmd_atpo(a, b, c, d); break;
    case OP_POSPO:       cmd_pospo(a, b, c); break;
    case OP_TAMPO:       cmd_tampo(a, b, c); break;
    case OP_CORPO:       cmd_corpo(a, b); break;
    case OP_PRIOR:       cmd_prior(a); break;
    case OP_CLST:        cmd_clear_status(); break;
    default:             cmd_raw(a); break;
    }
}

/* Envio direto SEM instrumentacao (vazao pura e amostragem propria) com
 * argumentos sempre VALIDOS derivados de i. Retorna o codigo do driver. */
static int enviar_op(int op, uint32_t i)
{
    switch (op) {
    case OP_NOOP:        return noop();
    case OP_NOOP_RAPIDO: return enviar_instrucao(1u << 5);
    case OP_BSCROLL:     return bscroll(i % TILES_X, (i * 7u) % TILES_Y);
    case OP_BTILE:       return btile(i % N_PIXELS_BG, (i * 13u) & 0xFFu);
    case OP_POSSP:       return possp(i & 31u, (i * 5u) % LARGURA, (i * 3u) % ALTURA);
    case OP_ATSP:        modelo_mascara_atsp(i & 31u, (i >> 2) & 1u);
                         return atsp(i & 31u, i & 3u, (i >> 2) & 1u);
    case OP_WRSP:        return wrsp((18u + (i & 1u)) * 256u + ((i >> 1) & 255u), (i * 29u) & 0xFFu);
    case OP_RGSP:        return rgsp(i & 31u, (i * 3u) & 31u);
    case OP_ATPO:        modelo_mascara_atpo(i & 15u, (i >> 4) & 1u);
                         return atpo_hw(i & 15u, (i >> 4) & 1u, i % 5u, (i >> 5) & 3u);
    case OP_POSPO:       return pospo(i & 15u, (i * 5u) % LARGURA, (i * 3u) % ALTURA);
    case OP_TAMPO:       return tampo(i & 15u, 8u + i % 100u, 8u + (i * 3u) % 90u);
    case OP_CORPO:       return corpo(i & 15u, (i * 37u) & 0xFFu);
    case OP_PRIOR:       return prior(i & 1u);
    case OP_CLST:        return clear_status();
    default:             return enviar_instrucao((13u + i % 19u) | ((i & 0x7FFFu) << 5));
    }
}

/* ---- Modelo de referencia (scoreboard) das flags err_opcode / wrn_param ---- */
typedef struct { unsigned err, wrn; } ModeloFlags;

static void modelo_aplicar(ModeloFlags *m, int op, unsigned a, unsigned b, unsigned c)
{
    switch (op) {
    case OP_BSCROLL: if ((a & 63u) >= 40u || (b & 31u) >= 30u) m->wrn = 1; break;
    case OP_BTILE:   if ((a & 0x1FFFFu) >= (unsigned)N_PIXELS_BG) m->wrn = 1; break;
    case OP_POSSP: case OP_POSPO: case OP_TAMPO:
        if ((b & 511u) >= 320u || (c & 255u) >= 240u) m->wrn = 1;
        break;
    case OP_ATPO:    if ((c & 7u) > 4u) m->wrn = 1; break;
    case OP_CLST:    m->err = 0; m->wrn = 0; break;
    case OP_RAW_INV: if ((a & 31u) > 12u) m->err = 1; break;
    default: break;
    }
}

/* Aguarda ociosidade e confere flags do hardware (e do driver) contra o esperado */
static int verificar_flags(const char *desc, unsigned exp_err, unsigned exp_wrn)
{
    Snap s;
    int ok, k;
    if (uc_drenar(1000000000ULL) < 0) { checar(0, "%s: UC nao ficou ociosa (timeout)", desc); return 0; }
    for (k = 0; k < 4; k++) (void)raw_status();   /* assenta a flag (UC leva ~3 ciclos) */
    s  = ler_snap();
    ok = (s.err == exp_err) && (s.wrn == exp_wrn) &&
         (s.d_err == (int)exp_err) && (s.d_wrn == (int)exp_wrn);
    checar(ok, "%-44s raw=0x%08X HW(err=%u wrn=%u) DRV(err=%d wrn=%d) esperado(err=%u wrn=%u)",
           desc, s.raw, s.err, s.wrn, s.d_err, s.d_wrn, exp_err, exp_wrn);
    return ok;
}

/* ============================================================================
 *  5. Controle de fluxo e relatorio por etapa
 * ========================================================================== */
static void sair_limpo(int codigo)
{
    limpar_tela_padrao();
    cmd_clear_status();
    imprimir_resumo_final();
    gpu_close();
    exit(codigo);
}

static void on_sigint(int s) { (void)s; g_sair = 1; }

static void frame(void)
{
    if (g_sair) sair_limpo(130);
    cmd_noop();
}

static void frames(int n) { while (n-- > 0) frame(); }

static void relatorio_janela(const char *etapa, uint64_t t_fim_prod, int64_t drain_ns, const Snap *st)
{
    double wall_prod_ms = (double)(t_fim_prod - g_jan.t_ini) / 1e6;
    double wall_tot_ms  = wall_prod_ms + (drain_ns > 0 ? (double)drain_ns / 1e6 : 0.0);
    unsigned delta_fc   = (st->fc - g_jan.fc_ini) & 0xFFFFu;

    printf("      --- Telemetria: %s ---\n", etapa);
    if (g_jan.n_rap > 0) {
        double med = (double)g_jan.ns_rap / (double)g_jan.n_rap;
        double var = (double)g_jan.soma_q / (double)g_jan.n_rap - med * med;
        printf("      Instrucoes (sem NOOP-VSYNC): %lu | tempo no driver %.2f ms (%.1f%% do tempo da etapa)\n",
               g_jan.n_rap, (double)g_jan.ns_rap / 1e6,
               wall_prod_ms > 0 ? 100.0 * (double)g_jan.ns_rap / 1e6 / wall_prod_ms : 0.0);
        printf("      Latencia handshake: min %.2f | med %.2f | max %.2f us | desvio %.2f us\n",
               (double)g_jan.ns_min / 1e3, med / 1e3, (double)g_jan.ns_max / 1e3, raiz(var > 0 ? var : 0) / 1e3);
        printf("      Vazao no driver: %.0f instr/s | bloqueios >0,5 ms (FIFO cheia): %lu (%.2f ms)\n",
               (double)g_jan.n_rap * 1e9 / (double)g_jan.ns_rap, g_jan.n_stall, (double)g_jan.ns_stall / 1e6);
    }
    if (g_jan.n_noop > 0) {
        printf("      Quadros: NOOP-VSYNC enviados=%lu | frame_counter HW +%u | duracao total %.1f ms\n",
               g_jan.n_noop, delta_fc, wall_tot_ms);
        if (wall_tot_ms > 0)
            printf("      FPS_HW(frame_counter)=%.2f | FPS_app(noops)=%.2f | ocupacao de quadros=%.0f%%\n",
                   1000.0 * delta_fc / wall_tot_ms, 1000.0 * (double)g_jan.n_noop / wall_tot_ms,
                   delta_fc ? 100.0 * (double)g_jan.n_noop / (double)delta_fc : 0.0);
    }
    if (drain_ns >= 0)
        printf("      Fila residual ao fim da etapa: drenou em %.2f ms (~%.1f quadros de adiantamento)\n",
               (double)drain_ns / 1e6, (double)drain_ns / (double)g_periodo_ns);
    else if (drain_ns == -1)
        printf("      Fila residual: NAO drenou em 1 s (!)\n");
    printf("      PIO_STATUS=0x%08X | fc=%u | full=%u/%d empty=%u/%d err=%u/%d wrn=%u/%d wait=%u busy=%d (HW/driver)\n",
           st->raw, st->fc, st->full, st->d_full, st->empty, st->d_empty,
           st->err, st->d_err, st->wrn, st->d_wrn, st->wait, st->busy);
}

/* Relatorio parcial no meio de uma etapa longa (sem drenar a fila) */
static void relatorio_parcial(const char *rotulo)
{
    Snap st = ler_snap();
    relatorio_janela(rotulo, agora_ns(), -2, &st);
    resetar_janela();
}

/* Le e zera o contador do driver; devolve o que o driver contou e o que o teste enviou */
static void ler_zerar_contador(unsigned *drv, unsigned long *meu)
{
    *drv = obter_e_zerar_instrucoes();
    *meu = g_total_enviadas - g_ctr_ref;
    g_ctr_ref = g_total_enviadas;
}

/* Reconciliacao do driver novo: contador de instrucoes e mascaras de show */
static void conciliar_driver(const char *titulo)
{
    unsigned drv, ms = obter_mascara_sprites_ativos(), mp = obter_mascara_poligonos_ativos();
    unsigned long meu;
    ler_zerar_contador(&drv, &meu);
    checar((unsigned long)drv == meu && ms == g_mask_sp_mod && mp == g_mask_po_mod,
           "[%s] driver reconciliado: contador=%u (teste=%lu) | mascara sprites=0x%08X (modelo 0x%08X) | poligonos=0x%04X (modelo 0x%04X)",
           titulo, drv, meu, ms, g_mask_sp_mod, mp, g_mask_po_mod);
}

/* Encerra a etapa: drena, confere flags, imprime telemetria, aguarda o usuario */
static void esperar(const char *titulo, const char *esperado)
{
    uint64_t t_fim_prod = agora_ns();
    int64_t  drain = uc_drenar(1000000000ULL);
    Snap     st;
    int      k;

    for (k = 0; k < 3; k++) (void)raw_status();
    st = ler_snap();

    printf("\n  [%s]\n      Esperado na tela: %s\n", titulo, esperado);
    relatorio_janela(titulo, t_fim_prod, drain, &st);

    if (drain < 0) checar(0, "[%s] UC nao drenou a FIFO em 1 s", titulo);
    conciliar_driver(titulo);
    if (st.busy != 0) checar(0, "[%s] busy() deveria ser 0 com a UC ociosa (busy=%d)", titulo, st.busy);
    if (g_auto_flags) {
        checar(st.err == 0 && st.wrn == 0 && st.d_err == 0 && st.d_wrn == 0,
               "[%s] flags limpas ao fim da etapa (err=%u/%d wrn=%u/%d)",
               titulo, st.err, st.d_err, st.wrn, st.d_wrn);
        if (st.err || st.wrn) cmd_clear_status();
    }

    if (g_tempo_ms >= 0) {
        printf("      (Avancando automaticamente em %d ms...)\n", g_tempo_ms);
        fflush(stdout);
        dormir_ms(g_tempo_ms);
    } else {
        char buf[16];
        printf("      ENTER = continuar | q+ENTER = sair > ");
        fflush(stdout);
        if (!fgets(buf, sizeof buf, stdin) || buf[0] == 'q' || buf[0] == 'Q') g_sair = 1;
    }
    if (g_sair) sair_limpo(130);
    resetar_janela();
}

/* ---- Registro de cadencia por quadro (pacing) ------------------------------ */
typedef struct {
    uint64_t *t;
    uint32_t *fc;
    size_t    n, cap;
    uint32_t  fc0;
} Pacing;

static void pacing_ini(Pacing *p, size_t cap)
{
    p->t  = (uint64_t *)malloc(cap * sizeof *p->t);
    p->fc = (uint32_t *)malloc(cap * sizeof *p->fc);
    p->n = 0; p->cap = cap;
    p->fc0 = raw_status() >> ST_SHIFT_FC;
}

static void pacing_marca(Pacing *p)
{
    if (p->n < p->cap) {
        p->t[p->n]  = agora_ns();
        p->fc[p->n] = raw_status() >> ST_SHIFT_FC;
        p->n++;
    }
}

static FILE *csv_abrir(const char *sufixo, const char *cabecalho)
{
    char nome[256];
    FILE *f;
    if (!g_csv_prefixo) return NULL;
    snprintf(nome, sizeof nome, "%s_%s.csv", g_csv_prefixo, sufixo);
    f = fopen(nome, "w");
    if (f) { fprintf(f, "%s\n", cabecalho); printf("      (CSV: %s)\n", nome); }
    return f;
}

/* Analisa os intervalos entre retornos consecutivos de noop(): em regime, devem
 * ser = periodo do VSYNC; 'adiantamento' = noops aceitos - quadros ja exibidos. */
static void pacing_relatorio(Pacing *p, const char *rotulo, size_t pular, const char *csv_sufixo, int estrito)
{
    size_t i, m = 0, estouros = 0;
    uint64_t *dt;
    double soma_ahead = 0.0;
    long max_ahead = 0;
    Estat e;
    FILE *f;

    if (p->n <= pular + 2) { informar("%s: amostras insuficientes", rotulo); return; }
    dt = (uint64_t *)malloc((p->n - pular) * sizeof *dt);
    f  = csv_abrir(csv_sufixo, "quadro,dt_ns,frame_counter,adiantamento_quadros");
    for (i = 0; i < p->n; i++) {
        long ahead = (long)(i + 1) - (long)((p->fc[i] - p->fc0) & 0xFFFFu);
        if (f) fprintf(f, "%zu,%llu,%u,%ld\n", i, i ? (unsigned long long)(p->t[i] - p->t[i - 1]) : 0ULL, p->fc[i], ahead);
        if (i >= pular) {
            dt[m++] = p->t[i] - p->t[i - 1];
            if (p->t[i] - p->t[i - 1] > g_periodo_ns + g_periodo_ns / 2) estouros++;
            soma_ahead += (double)ahead;
            if (ahead > max_ahead) max_ahead = ahead;
        }
    }
    if (f) fclose(f);
    e = estat_calc(dt, m);
    printf("      Cadencia [%s] (%zu quadros em regime): intervalo medio %.3f ms (alvo %.3f) | desvio %.3f ms\n",
           rotulo, m, e.media / 1e6, (double)g_periodo_ns / 1e6, e.desvio / 1e6);
    printf("        min %.3f | p50 %.3f | p99 %.3f | max %.3f ms | FPS medio da aplicacao: %.2f\n",
           (double)e.min / 1e6, (double)e.p50 / 1e6, (double)e.p99 / 1e6, (double)e.max / 1e6, 1e9 / e.media);
    printf("        Quadros estourados (>1,5x periodo): %zu | adiantamento da fila: medio %.1f, max %ld quadros "
           "(~%.0f ms de latencia de exibicao)\n",
           estouros, soma_ahead / (double)m, max_ahead, (soma_ahead / (double)m) * (double)g_periodo_ns / 1e6);
    if (estrito) {
        checar(estouros == 0, "[%s] nenhum quadro estourou 1,5x o periodo do VSYNC (%zu estouros)", rotulo, estouros);
        checar(e.media > 0.95 * (double)g_periodo_ns && e.media < 1.05 * (double)g_periodo_ns,
               "[%s] cadencia media dentro de +-5%% do VSYNC (%.3f ms)", rotulo, e.media / 1e6);
    } else {
        informar("[%s] capacidade: %zu estouros; cadencia media %.3f ms (%s)", rotulo, estouros, e.media / 1e6,
                 (e.media > 1.05 * (double)g_periodo_ns) ? "carga ACIMA do orcamento de quadro" : "dentro do orcamento");
    }
    free(dt);
    free(p->t); free(p->fc);
    p->t = NULL; p->fc = NULL;
}

/* ============================================================================
 *  6. Auxiliares matematicos e de alto nivel (sem -lm)
 * ========================================================================== */
static const int8_t TAB_SEN[64] = {
      0,  12,  25,  37,  49,  60,  71,  81,  90,  98, 106, 112, 117, 122, 125, 126,
    127, 126, 125, 122, 117, 112, 106,  98,  90,  81,  71,  60,  49,  37,  25,  12,
      0, -12, -25, -37, -49, -60, -71, -81, -90, -98,-106,-112,-117,-122,-125,-126,
   -127,-126,-125,-122,-117,-112,-106, -98, -90, -81, -71, -60, -49, -37, -25, -12
};
static int isen(int idx) { return TAB_SEN[idx & 63]; }
static int icos(int idx) { return TAB_SEN[(idx + 16) & 63]; }

static int clampi(int v, int lo, int hi) { return (v < lo) ? lo : ((v > hi) ? hi : v); }

static int tri(int t, int max)
{
    int p;
    if (max <= 0) return 0;
    p = t % (2 * max);
    if (p < 0) p += 2 * max;
    return (p <= max) ? p : (2 * max - p);
}

static uint32_t g_rng = 0xCAFEBABEu;
static uint32_t rnd(void)
{
    g_rng = g_rng * 1664525u + 1013904223u;
    return g_rng >> 8;
}

/* ---- Fundo por pixel (BTILE: END = y*320 + x) ------------------------------ */
static void bg_pixel(int x, int y, unsigned cor)
{
    cmd_btile((unsigned)(y * LARGURA + x), cor);
}

static void bg_preencher(unsigned cor)
{
    int i;
    for (i = 0; i < N_PIXELS_BG; i++) cmd_btile((unsigned)i, cor);
}

static void bg_retangulo(int x0, int y0, int w, int h, unsigned cor)
{
    int x, y;
    if (x0 < 0) { w += x0; x0 = 0; }
    if (y0 < 0) { h += y0; y0 = 0; }
    if (x0 + w > LARGURA) w = LARGURA - x0;
    if (y0 + h > ALTURA)  h = ALTURA - y0;
    for (y = y0; y < y0 + h; y++) {
        unsigned base = (unsigned)(y * LARGURA);
        for (x = x0; x < x0 + w; x++) cmd_btile(base + (unsigned)x, cor);
    }
}

static void bg_contorno(int x0, int y0, int x1, int y1, unsigned cor)   /* 1 px, inclusivo */
{
    bg_retangulo(x0, y0, x1 - x0 + 1, 1, cor);
    bg_retangulo(x0, y1, x1 - x0 + 1, 1, cor);
    bg_retangulo(x0, y0, 1, y1 - y0 + 1, cor);
    bg_retangulo(x1, y0, 1, y1 - y0 + 1, cor);
}

static void bg_cruz(int cx, int cy, int r, unsigned cor)
{
    bg_retangulo(cx - r, cy, 2 * r + 1, 1, cor);
    bg_retangulo(cx, cy - r, 1, 2 * r + 1, cor);
}

static void bg_xadrez(unsigned c1, unsigned c2, int lado_px)
{
    int x, y;
    for (y = 0; y < ALTURA; y++)
        for (x = 0; x < LARGURA; x++)
            cmd_btile((unsigned)(y * LARGURA + x), (((x / lado_px) + (y / lado_px)) & 1) ? c1 : c2);
}

static void bg_gradiente(void)
{
    int x, y;
    for (y = 0; y < ALTURA; y++)
        for (x = 0; x < LARGURA; x++)
            cmd_btile((unsigned)(y * LARGURA + x),
                      (unsigned)(((x * 8 / LARGURA) << 5) | ((y * 8 / ALTURA) << 2) |
                                 (((x + y) * 4 / (LARGURA + ALTURA)) & 3)));
}

static void bg_cenario_noturno(void)
{
    int x, y;
    for (y = 0; y < ALTURA; y++) {
        int ty = y / 8;
        for (x = 0; x < LARGURA; x++) {
            int tx = x / 8;
            unsigned cor;
            if (ty < 6)       cor = ((tx + ty) % 7 == 0) ? COR_AMARELO : COR_AZUL_ESCURO;
            else if (ty < 14) cor = COR_ROXO;
            else if (ty < 22) cor = ((tx / 3 + ty) & 1) ? COR_AZUL : COR_ROXO;
            else              cor = ((tx + ty) & 1) ? COR_VERDE_ESCURO : COR_VERDE;
            cmd_btile((unsigned)(y * LARGURA + x), cor);
        }
    }
}

static void bg_moldura(unsigned cor, int esp)
{
    bg_retangulo(0, 0, LARGURA, esp, cor);
    bg_retangulo(0, ALTURA - esp, LARGURA, esp, cor);
    bg_retangulo(0, 0, esp, ALTURA, cor);
    bg_retangulo(LARGURA - esp, 0, esp, ALTURA, cor);
}

/* ---- Sprites ---------------------------------------------------------------- */
static void sprite_pos(int id, int x, int y)
{
    cmd_possp((unsigned)id, (unsigned)clampi(x, 0, LARGURA - 1), (unsigned)clampi(y, 0, ALTURA - 1));
}

static void sprite_cfg(int id, int slot, int x, int y, int flip, int show)
{
    cmd_rgsp((unsigned)id, (unsigned)slot);
    sprite_pos(id, x, y);
    cmd_atsp((unsigned)id, (unsigned)flip, (unsigned)show);
}

static void esconder_todos_sprites(void)
{
    int i;
    for (i = 0; i < N_SPRITES; i++) cmd_atsp((unsigned)i, 0, 0);
}

/* ---- Poligonos (com FLIP) --------------------------------------------------- */
static void poligono_cfg(int id, int tipo, int flip, int x, int y, int larg, int alt, unsigned cor, int show)
{
    cmd_pospo((unsigned)id, (unsigned)clampi(x, 0, LARGURA - 1), (unsigned)clampi(y, 0, ALTURA - 1));
    cmd_tampo((unsigned)id, (unsigned)clampi(larg, 0, LARGURA - 1), (unsigned)clampi(alt, 0, ALTURA - 1));
    cmd_corpo((unsigned)id, cor);
    cmd_atpo((unsigned)id, (unsigned)show, (unsigned)tipo, (unsigned)flip);
}

/* Ancora do hardware: equilatero/isosceles ancoram no X central; demais no X esquerdo */
static void poligono_ancora(int tipo, int cx, int cy, int larg, int alt, int *ax, int *ay)
{
    int h = (tipo == POLY_QUADRADO) ? larg : alt;
    *ax = (tipo == POLY_EQUILATERO || tipo == POLY_ISOSCELES) ? cx : (cx - larg / 2);
    *ay = cy - h / 2;
}

static void poligono_centro(int id, int tipo, int flip, int cx, int cy, int larg, int alt, unsigned cor)
{
    int ax, ay;
    poligono_ancora(tipo, cx, cy, larg, alt, &ax, &ay);
    poligono_cfg(id, tipo, flip, ax, ay, larg, alt, cor, 1);
}

static void poligono_mover_centro(int id, int tipo, int cx, int cy, int larg, int alt)
{
    int ax, ay;
    poligono_ancora(tipo, cx, cy, larg, alt, &ax, &ay);
    cmd_pospo((unsigned)id, (unsigned)clampi(ax, 0, LARGURA - 1), (unsigned)clampi(ay, 0, ALTURA - 1));
}

/* Caixa envolvente prevista (vertices do RTL): usada para desenhar guias no fundo */
static void poligono_bbox(int tipo, int cx, int cy, int larg, int alt, int *x0, int *y0, int *x1, int *y1)
{
    int h = (tipo == POLY_QUADRADO) ? larg : alt;
    *y0 = cy - h / 2;
    *y1 = *y0 + h;
    if (tipo == POLY_EQUILATERO)      { *x0 = cx - larg / 2; *x1 = cx + larg / 2; }
    else if (tipo == POLY_ISOSCELES)  { *x0 = cx - larg / 4; *x1 = cx + larg / 4; }
    else                              { *x0 = cx - larg / 2; *x1 = *x0 + larg; }
}

static void esconder_todos_poligonos(void)
{
    int i;
    for (i = 0; i < N_POLIGONOS; i++) cmd_atpo((unsigned)i, 0, 0, 0);
}

static void limpar_sprites_poligonos(void)
{
    esconder_todos_sprites();
    esconder_todos_poligonos();
    cmd_prior(0);
    cmd_bscroll(0, 0);
}

static void limpar_tela_padrao(void)
{
    limpar_sprites_poligonos();
    bg_preencher(COR_FUNDO_PRETO);
    cmd_clear_status();
    frames(2);
}

/* ---- Arte 16x16 usada no unico teste dinamico de WRSP ---------------------- */
static const char *ARTE_GATINHO_REAL[16] = {
    "..KK........KK..",
    ".KYYK..OO..KYYK.",
    ".KYPKKOYYOKKPYK.",
    ".KYYYYYYYYYYYYK.",
    "KYYYYOYYYYOYYYYK",
    "KYYOBWKYYKWBOYYK",
    "KYYOBBKYYKBBOYYK",
    "KYYYYYYPPYYYYYYK",
    "KYPYYYYKKYYYYPYK",
    ".KYYYYKYYKYYYYK.",
    ".KYYYYYYYYYYYYK.",
    "..KRRRRRRRRRRK..",
    "..KROOOOOOOORK..",
    ".KYYYYYYYYYYYYK.",
    ".KYYYK....KYYYK.",
    "..KKK......KKK.."
};

static unsigned cor_arte_gatinho(char c)
{
    switch (c) {
    case 'K': return COR_PRETO_SOLIDO;
    case 'Y': return COR_AMARELO;
    case 'O': return COR_LARANJA;
    case 'P': return COR_ROSA_CHOQUE;
    case 'W': return COR_BRANCO;
    case 'B': return COR_AZUL;
    case 'R': return COR_VERMELHO;
    default:  return COR_TRANSP;
    }
}

/* Pinta o slot dinamico inteiro com a arte (256 WRSP) */
static void pintar_slot_arte(int slot)
{
    int x, y;
    for (y = 0; y < SPR_TAM; y++)
        for (x = 0; x < SPR_TAM; x++)
            cmd_wrsp((unsigned)(slot * 256 + y * SPR_TAM + x), cor_arte_gatinho(ARTE_GATINHO_REAL[y][x]));
}

/* ============================================================================
 *  7. CENAS - Grupo A: link MMIO, VSYNC, NOOP e FIFO
 * ========================================================================== */
static void op_ck(int op, uint32_t i)
{
    if (enviar_op(op, i) != 0) abortar_erro_hw(NOMES_OP[op]);
    g_total_enviadas++;
}

static int flags_conferem(unsigned exp_err, unsigned exp_wrn)
{
    Snap s;
    int k;
    if (uc_drenar(1000000000ULL) < 0) return 0;
    for (k = 0; k < 4; k++) (void)raw_status();
    s = ler_snap();
    return s.err == exp_err && s.wrn == exp_wrn && s.d_err == (int)exp_err && s.d_wrn == (int)exp_wrn;
}

static void imprimir_estat_us(const char *rotulo, const Estat *e)
{
    printf("      %-34s n=%-7zu min %8.3f | p50 %8.3f | p90 %8.3f | p99 %8.3f | p99.9 %9.3f | max %10.3f | med %8.3f | dp %8.3f us\n",
           rotulo, e->n, (double)e->min / 1e3, (double)e->p50 / 1e3, (double)e->p90 / 1e3,
           (double)e->p99 / 1e3, (double)e->p999 / 1e3, (double)e->max / 1e3, e->media / 1e3, e->desvio / 1e3);
}

/* Mede o periodo real do VSYNC observando o frame_counter (independe de instrucoes) */
static Estat medir_periodo(int n, int *saltos, int *timeout)
{
    uint64_t *per = (uint64_t *)malloc((size_t)n * sizeof *per);
    uint32_t fc_ant, fc2;
    uint64_t t_ant, t;
    int i, m = 0;
    Estat e;
    *saltos = 0; *timeout = 0;
    t_ant = sincronizar_inicio_de_quadro(&fc_ant);
    if (!t_ant) { *timeout = 1; free(per); memset(&e, 0, sizeof e); return e; }
    for (i = 0; i < n; i++) {
        t = aguardar_fc_mudar(fc_ant, &fc2, 200000000ULL);
        if (!t) { *timeout = 1; break; }
        per[m++] = t - t_ant;
        if (((fc2 - fc_ant) & 0xFFFFu) != 1u) (*saltos)++;
        t_ant = t; fc_ant = fc2;
    }
    e = estat_calc(per, (size_t)m);
    free(per);
    return e;
}

static int g_periodo_calibrado = 0;
static void garantir_periodo(void)
{
    int s, t;
    Estat e;
    if (g_periodo_calibrado) return;
    e = medir_periodo(40, &s, &t);
    if (!t && e.n > 10) g_periodo_ns = (uint64_t)e.media;
    g_periodo_calibrado = 1;
}

/* ---------------------------------------------------------------------------
 *  CENA 1: Link MMIO - overhead do timer, latencia de leitura do PIO_STATUS,
 *          bits reservados, consistencia driver x registrador, ciclo init/close
 * ------------------------------------------------------------------------- */
static void cena_01_link_mmio(void)
{
    int i, N, mism, rc;
    uint64_t *v;
    unsigned long h[HIST_BINS];
    unsigned long reservado = 0, invalido = 0;
    Estat e;
    Snap s;

    resetar_janela();
    limpar_sprites_poligonos();
    cmd_clear_status();

    printf("      --- 1.1 Overhead do cronometro (clock_gettime) ---\n");
    N = esc(100000);
    v = (uint64_t *)malloc((size_t)N * sizeof *v);
    for (i = 0; i < N; i++) { uint64_t t0 = agora_ns(); uint64_t t1 = agora_ns(); v[i] = t1 - t0; }
    e = estat_calc(v, (size_t)N);
    g_overhead_timer_ns = e.p50;
    imprimir_estat_us("clock_gettime x2 (back-to-back)", &e);
    informar("overhead mediano do cronometro = %llu ns (descontado nas tabelas de latencia)",
             (unsigned long long)g_overhead_timer_ns);
    free(v);

    printf("      --- 1.2 Latencia de 1 leitura MMIO do PIO_STATUS (status()) ---\n");
    N = esc(50000);
    v = (uint64_t *)malloc((size_t)N * sizeof *v);
    memset(h, 0, sizeof h);
    for (i = 0; i < N; i++) {
        uint64_t t0 = agora_ns();
        uint32_t r = raw_status();
        uint64_t dt = agora_ns() - t0;
        v[i] = (dt > g_overhead_timer_ns) ? dt - g_overhead_timer_ns : 0;
        h[bin_log2(v[i] ? v[i] : 1)]++;
        if (r & ST_MASK_RESERVADO) reservado++;
        if (r == 0xFFFFFFFFu) invalido++;
    }
    e = estat_calc(v, (size_t)N);
    imprimir_estat_us("status() leitura unica", &e);
    imprimir_hist("status()", h);
    checar(reservado == 0, "bits reservados [15:5] do PIO_STATUS sempre 0 em %d leituras (violacoes: %lu)", N, reservado);
    checar(invalido == 0, "status() nunca retornou 0xFFFFFFFF (-1) com o driver inicializado (%lu)", invalido);
    free(v);

    printf("      --- 1.3 Consistencia driver x registrador bruto ---\n");
    verificar_flags("estado inicial apos CLST", 0, 0);
    mism = 0;
    for (i = 0; i < 200; i++) {
        s = ler_snap();
        if ((int)s.full != s.d_full || (int)s.empty != s.d_empty || (int)s.err != s.d_err ||
            (int)s.wrn != s.d_wrn || ((((unsigned)s.d_fc - s.fc) & 0xFFFFu) > 1u)) mism++;
    }
    checar(mism == 0, "fifo_full/fifo_empty/error/warning/frame_counter() == bits do status() em 200 amostras (div=%d)", mism);

    cmd_raw(13u | (0x1234u << 5));
    verificar_flags("opcode invalido: so err_opcode (bit 2)", 1, 0);
    s = ler_snap();
    checar(((s.raw >> 2) & 1u) == 1u && ((s.raw >> 3) & 1u) == 0u && error() == 1 && warning() == 0,
           "mapeamento: error() -> bit 2 | warning() -> bit 3 (raw=0x%08X error()=%d warning()=%d)", s.raw, error(), warning());
    cmd_btile(76800, 0);
    verificar_flags("btile fora do limite: err retido + wrn_param", 1, 1);
    s = ler_snap();
    checar(((s.raw >> 3) & 1u) == 1u && warning() == 1 && error() == 1,
           "mapeamento: warning() -> bit 3 (raw=0x%08X)", s.raw);
    cmd_clear_status();
    verificar_flags("CLST limpa as duas", 0, 0);

    printf("      --- 1.4 Ciclo gpu_close() / gpu_init() e idempotencia ---\n");
    uc_drenar(1000000000ULL);
    gpu_close();
    rc = enviar_instrucao(1u << 5);
    checar(rc == -1, "enviar_instrucao() apos gpu_close() retorna -1 (rc=%d)", rc);
    rc = status();
    checar(rc == -1, "status() apos gpu_close() retorna -1 (rc=%d)", rc);
    gpu_close();
    checar(1, "gpu_close() repetido nao trava");
    rc = gpu_init();
    checar(rc == 0, "gpu_init() reabre o mapeamento (rc=%d)", rc);
    rc = gpu_init();
    checar(rc == 0, "gpu_init() chamado de novo e idempotente (rc=%d)", rc);
    rc = enviar_instrucao(1u << 5);
    if (rc == 0) g_total_enviadas++;
    checar(rc == 0, "instrucao enviada normalmente apos reabrir (rc=%d)", rc);
    verificar_flags("flags continuam limpas apos reabrir", 0, 0);

    printf("      --- 1.5 Convencao de argumentos do atpo (libAssembly_1.S: flip, id, show, tipo) ---\n");
    cmd_clear_status();
    cmd_atpo(0, 0, 5, 0);                       /* tipo=5 -> wrn_param (prova que tipo chega em instr[7:5]) */
    verificar_flags("atpo(id0,show0,tipo5,flip0): tipo=5 aciona wrn", 0, 1);
    cmd_clear_status();
    cmd_atpo(15, 0, 0, 5);                      /* flip=5 (vira 1 por mascara de 2 bits) NAO pode virar tipo */
    verificar_flags("atpo(id15,show0,tipo0,flip5): flip nao e lido como tipo", 0, 0);
    cmd_clear_status();
    cmd_atpo(7, 0, 4, 3);
    verificar_flags("atpo(id7,show0,tipo4,flip3): maximo valido", 0, 0);

    esperar("LINK 1/1 - Overhead, latencia do status e consistencia do driver",
            "Sem mudanca visual; veja as tabelas de latencia do status() e as verificacoes acima.");
}

/* ---------------------------------------------------------------------------
 *  CENA 2: VSYNC e frame_counter - periodo, jitter, FPS, incremento unitario
 * ------------------------------------------------------------------------- */
static void cena_02_vsync_framecounter(void)
{
    int N = esc(300), saltos, timeout;
    Estat e;
    uint32_t fc0 = 0, fc1 = 0;
    uint64_t ta, tb;
    FILE *f;

    resetar_janela();
    printf("      Observando %d quadros pelo frame_counter (polling do PIO_STATUS)...\n", N);
    e = medir_periodo(N, &saltos, &timeout);
    checar(!timeout, "frame_counter evoluiu continuamente (sem timeout de 200 ms)");
    if (e.n > 2) {
        double fps = 1e9 / e.media;
        imprimir_estat_us("periodo entre incrementos do fc", &e);
        printf("      Periodo medio %.4f ms | FPS = %.3f | jitter (desvio) %.1f us | min %.3f | max %.3f ms\n",
               e.media / 1e6, fps, e.desvio / 1e3, (double)e.min / 1e6, (double)e.max / 1e6);
        checar(fps > 59.0 && fps < 61.0, "taxa de quadros do VGA entre 59 e 61 FPS (medido %.3f)", fps);
        checar(saltos == 0, "frame_counter incrementa sempre de 1 em 1 (saltos/duplicatas: %d)", saltos);
        checar(e.desvio < 0.05 * e.media, "jitter do VSYNC < 5%% do periodo (%.1f us)", e.desvio / 1e3);
        g_periodo_ns = (uint64_t)e.media;
        g_periodo_calibrado = 1;
        f = csv_abrir("vsync_periodo", "estatistica,valor_ns");
        if (f) {
            fprintf(f, "media,%.0f\ndesvio,%.0f\nmin,%llu\np50,%llu\np99,%llu\nmax,%llu\n", e.media, e.desvio,
                    (unsigned long long)e.min, (unsigned long long)e.p50, (unsigned long long)e.p99,
                    (unsigned long long)e.max);
            fclose(f);
        }
    }

    printf("      Longo prazo: frame_counter x relogio do Linux (150 quadros)...\n");
    ta = sincronizar_inicio_de_quadro(&fc0);
    dormir_ms(150 * 1000 / 60);
    (void)aguardar_fc_mudar(raw_status() >> ST_SHIFT_FC, &fc1, 200000000ULL);
    tb = agora_ns();
    if (ta) {
        double fps = 1e9 * (double)((fc1 - fc0) & 0xFFFFu) / (double)(tb - ta);
        printf("      %u quadros em %.2f ms -> %.3f FPS\n", (fc1 - fc0) & 0xFFFFu, (double)(tb - ta) / 1e6, fps);
        checar(fps > 59.0 && fps < 61.0, "FPS de longo prazo entre 59 e 61 (%.3f)", fps);
    }
    esperar("VSYNC 1/1 - Periodo e frame_counter",
            "Sem mudanca visual; o frame_counter do hardware deve ter ~60 FPS estaveis.");
}

/* ---------------------------------------------------------------------------
 *  CENA 3: NOOP - vsync_wait (atomico com frame_counter), NOOP rapido, sincronismo
 * ------------------------------------------------------------------------- */
static void cena_03_noop_vsync_wait(void)
{
    int rep, R = esc(30), i, k;
    static const int NS[5] = { 1, 2, 5, 10, 30 };
    uint64_t *durs, *errs;
    Estat e, ee;
    int m = 0, incons_total = 0, visto_falha = 0;
    int sight = 0;

    resetar_janela();
    garantir_periodo();
    limpar_sprites_poligonos();
    cmd_clear_status();
    uc_drenar(1000000000ULL);

    printf("      --- 3.1 Estado ocioso ---\n");
    {
        Snap s = ler_snap();
        checar(s.wait == 0 && s.empty == 1 && s.full == 0,
               "ocioso: vsync_wait=0 fifo_empty=1 fifo_full=0 (raw=0x%08X)", s.raw);
    }

    printf("      --- 3.2 NOOP (bit5=0): vsync_wait e frame_counter lidos de forma atomica ---\n");
    durs = (uint64_t *)malloc((size_t)R * sizeof *durs);
    errs = (uint64_t *)malloc((size_t)R * sizeof *errs);
    for (rep = 0; rep < R; rep++) {
        uint32_t fce, fc_w = 0, fc_end = 0, r;
        uint64_t t_edge, atraso, t_w = 0, t_end = 0, tp, t;
        int incons = 0;

        t_edge = sincronizar_inicio_de_quadro(&fce);
        atraso = ((uint64_t)rep * 700000ULL) % (g_periodo_ns - 3000000ULL);
        espera_ativa_ns(atraso);
        cmd_noop();
        tp = agora_ns();
        for (;;) {
            r = raw_status();
            if (r & ST_BIT_VSYNC_WAIT) { t_w = agora_ns(); fc_w = r >> ST_SHIFT_FC; break; }
            if (agora_ns() - tp > 2000000ULL) break;
        }
        if (!t_w) { visto_falha++; uc_drenar(100000000ULL); continue; }
        for (;;) {
            r = raw_status();
            t = agora_ns();
            if (r & ST_BIT_VSYNC_WAIT) {
                if ((r >> ST_SHIFT_FC) != fc_w) incons++;     /* fc nao pode mudar durante a espera */
                if (!(r & ST_BIT_FIFO_EMPTY))   incons++;     /* so o NOOP foi enviado */
                if (t - t_w > 2 * g_periodo_ns) break;
            } else { t_end = t; fc_end = r >> ST_SHIFT_FC; break; }
        }
        incons_total += incons;
        if (t_end) {
            uint64_t dur = t_end - t_w, esp = g_periodo_ns - (t_w - t_edge);
            durs[m] = dur;
            errs[m] = (dur > esp) ? dur - esp : esp - dur;
            m++;
            if (((fc_end - fc_w) & 0xFFFFu) != 1u) incons++, incons_total++;
        } else incons_total++;
    }
    checar(visto_falha == 0, "vsync_wait=1 observado apos cada NOOP (falhas: %d/%d)", visto_falha, R);
    checar(incons_total == 0, "durante a espera fc constante, fifo_empty=1 e, ao sair, fc+1 no mesmo ciclo (inconsistencias: %d)", incons_total);
    if (m > 2) {
        e  = estat_calc(durs, (size_t)m);
        ee = estat_calc(errs, (size_t)m);
        imprimir_estat_us("duracao de vsync_wait (fase variada)", &e);
        imprimir_estat_us("|duracao - (periodo - fase)|", &ee);
        checar(e.max < g_periodo_ns + 1000000ULL, "vsync_wait nunca excede 1 periodo (+1 ms): max %.3f ms", (double)e.max / 1e6);
        checar(ee.p99 < 1000000ULL, "duracao prevista pela fase bate (p99 do erro %.1f us < 1000 us)", (double)ee.p99 / 1e3);
    }
    free(durs); free(errs);

    printf("      --- 3.3 NOOP rapido (bit5=1) nunca entra em WAIT ---\n");
    for (i = 0; i < esc(500); i++) {
        uint32_t r;
        cmd_noop_rapido();
        r = raw_status();
        if (r & ST_BIT_VSYNC_WAIT) sight++;
    }
    checar(sight == 0, "vsync_wait=0 depois de %d NOOPs rapidos (avistamentos: %d)", esc(500), sight);

    printf("      --- 3.4 N NOOPs = exatamente N quadros ---\n");
    for (k = 0; k < 5; k++) {
        uint32_t fc0, fc1;
        uint64_t t0, wall;
        int64_t d;
        (void)sincronizar_inicio_de_quadro(&fc0);
        t0 = agora_ns();
        for (i = 0; i < NS[k]; i++) cmd_noop();
        d = uc_drenar(2000000000ULL);
        fc1 = raw_status() >> ST_SHIFT_FC;
        wall = agora_ns() - t0;
        checar(d >= 0 && ((fc1 - fc0) & 0xFFFFu) == (unsigned)NS[k],
               "%2d NOOP(s) -> frame_counter +%u (esperado +%d) em %.2f ms (ideal %.2f ms)",
               NS[k], (fc1 - fc0) & 0xFFFFu, NS[k], (double)wall / 1e6, (double)NS[k] * (double)g_periodo_ns / 1e6);
    }

    printf("      --- 3.5 noop() retorna ao ser ACEITO pela FIFO (nao bloqueia em VSYNC se ha vaga) ---\n");
    {
        uint64_t mx = 0, t0, dt;
        for (i = 0; i < esc(10); i++) {
            uc_drenar(1000000000ULL);
            t0 = agora_ns();
            if (noop() != 0) abortar_erro_hw("noop");
            dt = agora_ns() - t0;
            g_total_enviadas++;
            if (dt > mx) mx = dt;
        }
        checar(mx < 1000000ULL, "noop() com FIFO vazia retorna em < 1 ms (max %.2f us)", (double)mx / 1e3);
    }

    esperar("NOOP 1/1 - vsync_wait, NOOP rapido e sincronismo",
            "Sem mudanca visual; verificacoes de vsync_wait/frame_counter acima.");
}

/* ---------------------------------------------------------------------------
 *  CENA 4: FIFO de 16 posicoes - full/empty, backpressure e adiantamento maximo
 * ------------------------------------------------------------------------- */
static void cena_04_fifo(void)
{
    int rep, R = esc(12), k, i, validos = 0;
    uint64_t *bloq, *erro;
    int m = 0;
    Estat e;

    resetar_janela();
    garantir_periodo();
    limpar_sprites_poligonos();
    cmd_clear_status();
    uc_drenar(1000000000ULL);

    printf("      --- 4.1 Encher a FIFO enquanto a UC espera o VSYNC ---\n");
    bloq = (uint64_t *)malloc((size_t)R * sizeof *bloq);
    erro = (uint64_t *)malloc((size_t)R * sizeof *erro);
    for (rep = 0; rep < R; rep++) {
        uint32_t fce, r = 0;
        uint64_t t_edge, tp, t17, b;
        int visto = 0, primeiro_full = -1, primeiro_nao_vazia = -1;

        t_edge = sincronizar_inicio_de_quadro(&fce);
        espera_ativa_ns(((uint64_t)rep * 900000ULL) % 11000000ULL);
        cmd_noop();
        tp = agora_ns();
        while (agora_ns() - tp < 2000000ULL) { if (raw_status() & ST_BIT_VSYNC_WAIT) { visto = 1; break; } }
        if (!visto) { checar(0, "rep %d: UC nao entrou em vsync_wait", rep); uc_drenar(100000000ULL); continue; }

        for (k = 1; k <= FIFO_PROFUNDIDADE; k++) {
            cmd_noop_rapido();
            r = raw_status();
            if ((r & ST_BIT_FIFO_FULL) && primeiro_full < 0) primeiro_full = k;
            if (!(r & ST_BIT_FIFO_EMPTY) && primeiro_nao_vazia < 0) primeiro_nao_vazia = k;
        }
        if ((r >> ST_SHIFT_FC) != fce) {
            informar("rep %d: VSYNC ocorreu durante o enchimento; repeticao descartada", rep);
            uc_drenar(100000000ULL);
            continue;
        }
        validos++;
        checar(primeiro_full == FIFO_PROFUNDIDADE && primeiro_nao_vazia == 1,
               "rep %2d: fifo_full sobe exatamente na 16a instrucao (visto na %d) | fifo_empty cai na 1a (%d) | "
               "estado final full=%u empty=%u wait=%u",
               rep, primeiro_full, primeiro_nao_vazia, (r & 1u), (r >> 1) & 1u, (r >> 4) & 1u);

        t17 = agora_ns();
        cmd_noop_rapido();                      /* 17a: bloqueia ate o VSYNC liberar a UC */
        b = agora_ns() - t17;
        {
            uint64_t esp = g_periodo_ns - (t17 - t_edge);
            bloq[m] = b;
            erro[m] = (b > esp) ? b - esp : esp - b;
            m++;
            printf("      17a instrucao bloqueou %.3f ms (previsto pela fase: %.3f ms)\n", (double)b / 1e6, (double)esp / 1e6);
        }
        {
            int64_t d = uc_drenar(200000000ULL);
            Snap s = ler_snap();
            checar(d >= 0 && s.empty == 1 && s.full == 0 && s.wait == 0,
                   "rep %2d: FIFO drenada em %.1f us apos o VSYNC (empty=%u full=%u wait=%u)",
                   rep, d >= 0 ? (double)d / 1e3 : -1.0, s.empty, s.full, s.wait);
        }
    }
    if (m > 1) {
        e = estat_calc(erro, (size_t)m);
        imprimir_estat_us("|bloqueio - previsto|", &e);
        checar(e.max < 1500000ULL, "tempo de bloqueio da 17a instrucao segue a fase do VSYNC (erro max %.1f us)", (double)e.max / 1e3);
        e = estat_calc(bloq, (size_t)m);
        imprimir_estat_us("bloqueio da 17a instrucao", &e);
    }
    checar(validos > 0, "ao menos 1 repeticao valida (%d/%d)", validos, R);
    free(bloq); free(erro);

    printf("      --- 4.2 Adiantamento maximo: quantos NOOPs cabem sem bloquear ---\n");
    {
        int aceitos = 0;
        uint32_t fc0;
        (void)sincronizar_inicio_de_quadro(&fc0);
        for (i = 0; i < 40; i++) {
            uint64_t t0 = agora_ns(), dt;
            cmd_noop();
            dt = agora_ns() - t0;
            if (dt > 2000000ULL) break;
            aceitos++;
        }
        uc_drenar(2000000000ULL);
        checar(aceitos == FIFO_PROFUNDIDADE + 1,
               "aceitos sem bloqueio: %d (esperado 17 = 1 em WAIT + 16 na FIFO) => ate %.0f ms de adiantamento",
               aceitos, 17.0 * (double)g_periodo_ns / 1e6);
    }

    printf("      --- 4.3 Backpressure sustentado: n NOOPs levam (n-17) quadros para serem aceitos ---\n");
    {
        int n = esc(40);
        uint32_t fc0, fc1;
        uint64_t t0, t_send;
        int64_t d;
        if (n < 24) n = 24;
        (void)sincronizar_inicio_de_quadro(&fc0);
        t0 = agora_ns();
        for (i = 0; i < n; i++) cmd_noop();
        t_send = agora_ns() - t0;
        d = uc_drenar(3000000000ULL);
        fc1 = raw_status() >> ST_SHIFT_FC;
        printf("      %d NOOPs aceitos em %.2f ms (ideal %.2f ms = (n-17) quadros); fila drenou em %.2f ms\n",
               n, (double)t_send / 1e6, (double)(n - 17) * (double)g_periodo_ns / 1e6, (double)d / 1e6);
        checar((double)t_send > ((double)(n - 17) - 0.15) * (double)g_periodo_ns &&
               (double)t_send < ((double)(n - 17) + 0.15) * (double)g_periodo_ns,
               "tempo para aceitar %d NOOPs = (n-17) quadros +-0,15 (%.3f quadros)", n, (double)t_send / (double)g_periodo_ns);
        checar(((fc1 - fc0) & 0xFFFFu) == (unsigned)n,
               "frame_counter avancou exatamente n=%d quadros (+%u): nenhum NOOP perdido/duplicado", n, (fc1 - fc0) & 0xFFFFu);
    }

    printf("      --- 4.4 Instrucoes rapidas: a UC consome mais rapido que o ARM envia? ---\n");
    {
        int visto_full = 0, N = esc(5000);
        for (i = 0; i < N; i++) {
            cmd_noop_rapido();
            if (raw_status() & ST_BIT_FIFO_FULL) visto_full++;
        }
        informar("fifo_full observada em %d de %d envios rapidos (0 = UC drena mais rapido que o ARM envia)", visto_full, N);
    }

    esperar("FIFO 1/1 - full/empty, backpressure e adiantamento",
            "Sem mudanca visual; veja as verificacoes de FIFO (16 posicoes + 1 em WAIT = 17 quadros de adiantamento).");
}

/* ============================================================================
 *  8. CENAS - Grupo B: flags de status (wrn_param, err_opcode) e fuzz
 * ========================================================================== */
typedef struct {
    int op;
    unsigned a, b, c, d;
    unsigned wrn;
    const char *desc;
} CasoWrn;

/* (a,b,c,d) seguem a ordem dos argumentos do driver */
static const CasoWrn CASOS_WRN[] = {
    { OP_BSCROLL,   0,  0, 0, 0, 0, "BSCROLL(0,0) origem" },
    { OP_BSCROLL,  39, 29, 0, 0, 0, "BSCROLL(39,29) maximo valido" },
    { OP_BSCROLL,  20, 15, 0, 0, 0, "BSCROLL(20,15) meio" },
    { OP_BSCROLL,  40,  0, 0, 0, 1, "BSCROLL X=40 (>=40)" },
    { OP_BSCROLL,   0, 30, 0, 0, 1, "BSCROLL Y=30 (>=30)" },
    { OP_BSCROLL,  40, 30, 0, 0, 1, "BSCROLL X=40 e Y=30" },
    { OP_BSCROLL,  39, 30, 0, 0, 1, "BSCROLL (39,30)" },
    { OP_BSCROLL,  40, 29, 0, 0, 1, "BSCROLL (40,29)" },
    { OP_BSCROLL,  63, 31, 0, 0, 1, "BSCROLL (63,31) maximo do campo" },

    { OP_BTILE,     0, 0x00, 0, 0, 0, "BTILE end=0 cor=0x00 (preto)" },
    { OP_BTILE, 76799, 0xFF, 0, 0, 0, "BTILE end=76799 (pixel 319,239)" },
    { OP_BTILE, 65535, 0x1C, 0, 0, 0, "BTILE end=65535" },
    { OP_BTILE, 76800, 0xE0, 0, 0, 1, "BTILE end=76800 (>=76800)" },
    { OP_BTILE, 76801, 0xE0, 0, 0, 1, "BTILE end=76801" },
    { OP_BTILE,100000, 0xE0, 0, 0, 1, "BTILE end=100000" },
    { OP_BTILE,131071, 0xE0, 0, 0, 1, "BTILE end=131071 (maximo do campo 17b)" },

    { OP_POSSP,  0,   0,   0, 0, 0, "POSSP(0,0,0)" },
    { OP_POSSP, 31, 319, 239, 0, 0, "POSSP(31,319,239) maximo valido" },
    { OP_POSSP, 15, 160, 120, 0, 0, "POSSP(15,160,120)" },
    { OP_POSSP, 31, 320, 100, 0, 1, "POSSP X=320 (>=320)" },
    { OP_POSSP, 31, 100, 240, 0, 1, "POSSP Y=240 (>=240)" },
    { OP_POSSP, 31, 320, 240, 0, 1, "POSSP X=320 e Y=240" },
    { OP_POSSP,  5, 319, 240, 0, 1, "POSSP (319,240)" },
    { OP_POSSP,  5, 320, 239, 0, 1, "POSSP (320,239)" },
    { OP_POSSP,  0, 511, 255, 0, 1, "POSSP (511,255) maximo do campo" },

    { OP_ATSP,   0, 0, 0, 0, 0, "ATSP(0,flip0,show0)" },
    { OP_ATSP,  31, 3, 1, 0, 0, "ATSP(31,flip3,show1) maximo" },
    { OP_WRSP,   4608, 0x00, 0, 0, 0, "WRSP slot18 pixel0 cor=0 (transparente)" },
    { OP_WRSP,   5119, 0x00, 0, 0, 0, "WRSP slot19 pixel255 cor=0" },
    { OP_RGSP,   0,  0, 0, 0, 0, "RGSP(0,0)" },
    { OP_RGSP,  31, 31, 0, 0, 0, "RGSP(31,31) maximo" },

    { OP_ATPO,  15, 1, 4, 3, 0, "ATPO(15,show1,tipo4,flip3) maximo valido" },
    { OP_ATPO,   0, 0, 5, 0, 1, "ATPO tipo=5 (>4)" },
    { OP_ATPO,   7, 0, 6, 2, 1, "ATPO tipo=6 flip2" },
    { OP_ATPO,  15, 1, 7, 3, 1, "ATPO tipo=7 flip3 (maximo do campo)" },

    { OP_POSPO,  0,   0,   0, 0, 0, "POSPO(0,0,0)" },
    { OP_POSPO, 15, 319, 239, 0, 0, "POSPO(15,319,239) maximo valido" },
    { OP_POSPO, 15, 320,  50, 0, 1, "POSPO X=320 (>=320)" },
    { OP_POSPO, 15,  50, 240, 0, 1, "POSPO Y=240 (>=240)" },
    { OP_POSPO, 15, 511, 255, 0, 1, "POSPO (511,255) maximo do campo" },

    { OP_TAMPO,  0,   0,   0, 0, 0, "TAMPO(0,0,0) tamanho zero e valido" },
    { OP_TAMPO, 15, 319, 239, 0, 0, "TAMPO(15,319,239) maximo valido" },
    { OP_TAMPO, 15, 320,  40, 0, 1, "TAMPO larg=320 (>=320)" },
    { OP_TAMPO, 15,  40, 240, 0, 1, "TAMPO alt=240 (>=240)" },
    { OP_TAMPO, 15, 511, 255, 0, 1, "TAMPO (511,255) maximo do campo" },

    { OP_CORPO,  0,   0, 0, 0, 0, "CORPO(0,0x00)" },
    { OP_CORPO, 15, 255, 0, 0, 0, "CORPO(15,0xFF)" },
    { OP_PRIOR,  0,   0, 0, 0, 0, "PRIOR(0)" },
    { OP_PRIOR,  1,   0, 0, 0, 0, "PRIOR(1)" },
    { OP_NOOP_RAPIDO, 0, 0, 0, 0, 0, "NOOP rapido" }
};
#define N_CASOS_WRN ((int)(sizeof CASOS_WRN / sizeof CASOS_WRN[0]))

/* ---------------------------------------------------------------------------
 *  CENA 5: wrn_param - limites de TODAS as instrucoes + retencao + CLST
 * ------------------------------------------------------------------------- */
static void cena_05_wrn_param(void)
{
    int i, tipo, flip, ok_total, total;
    static const int OPS_VALIDOS[] = { OP_NOOP_RAPIDO, OP_BSCROLL, OP_BTILE, OP_POSSP, OP_ATSP, OP_WRSP,
                                       OP_RGSP, OP_ATPO, OP_POSPO, OP_TAMPO, OP_CORPO, OP_PRIOR };
    int antigo = g_auto_flags;
    g_auto_flags = 0;

    resetar_janela();
    limpar_sprites_poligonos();
    cmd_clear_status();

    printf("      --- 5.1 Tabela de limites (CLST antes de cada caso) ---\n");
    for (i = 0; i < N_CASOS_WRN; i++) {
        const CasoWrn *c = &CASOS_WRN[i];
        cmd_clear_status();
        cmd_gen(c->op, c->a, c->b, c->c, c->d);
        verificar_flags(c->desc, 0, c->wrn);
    }

    printf("      --- 5.2 ATPO: todos os TIPOS x FLIPS (flip nunca gera wrn; tipo>4 sempre gera) ---\n");
    ok_total = 0; total = 0;
    for (tipo = 0; tipo < 8; tipo++) {
        for (flip = 0; flip < 4; flip++) {
            cmd_clear_status();
            cmd_atpo((unsigned)((tipo * 4 + flip) & 15), 0, (unsigned)tipo, (unsigned)flip);
            total++;
            if (flags_conferem(0, tipo > 4 ? 1u : 0u)) ok_total++;
        }
    }
    checar(ok_total == total, "ATPO tipo 0..7 x flip 0..3: wrn so para tipo>4 (%d/%d corretos)", ok_total, total);

    printf("      --- 5.3 Retencao (sticky): wrn_param continua em 1 apos instrucoes validas ---\n");
    cmd_clear_status();
    cmd_btile(76800, 0);
    verificar_flags("wrn_param levantada (BTILE end=76800)", 0, 1);
    for (i = 0; i < 12 * 12; i++) op_ck(OPS_VALIDOS[i % 12], (uint32_t)i);
    verificar_flags("wrn_param retida apos 144 instrucoes validas", 0, 1);
    cmd_clear_status();
    verificar_flags("CLST limpa wrn_param", 0, 0);

    printf("      --- 5.4 Varias violacoes seguidas e uma unica CLST ---\n");
    cmd_bscroll(40, 0); cmd_btile(76800, 0); cmd_possp(0, 320, 0); cmd_atpo(0, 0, 5, 0);
    cmd_pospo(0, 0, 240); cmd_tampo(0, 320, 0);
    verificar_flags("6 violacoes (uma por instrucao com limite)", 0, 1);
    cmd_clear_status();
    verificar_flags("uma CLST zera todas", 0, 0);

    printf("      --- 5.5 CLST em estado limpo e idempotente ---\n");
    cmd_clear_status(); cmd_clear_status(); cmd_clear_status();
    verificar_flags("3 CLSTs seguidas em estado limpo", 0, 0);

    g_auto_flags = antigo;
    esperar("WRN_PARAM 1/1 - Limites de todas as instrucoes",
            "Sem mudanca visual relevante; veja as verificacoes de wrn_param acima.");
}

/* ---------------------------------------------------------------------------
 *  CENA 6: err_opcode - opcodes 13..31, validos nao setam, retencao, ordem na FIFO
 * ------------------------------------------------------------------------- */
static void executar_sequencia(const char *seq, unsigned *exp_err, unsigned *exp_wrn)
{
    ModeloFlags m = { 0, 0 };
    const char *p;
    for (p = seq; *p; p++) {
        switch (*p) {
        case 'I': cmd_raw(13u | (0x1AAAAu << 5));  modelo_aplicar(&m, OP_RAW_INV, 13u, 0, 0); break;
        case 'C': cmd_clear_status();              modelo_aplicar(&m, OP_CLST, 0, 0, 0); break;
        case 'W': cmd_btile(76800, 0);             modelo_aplicar(&m, OP_BTILE, 76800u, 0, 0); break;
        case 'N': cmd_noop_rapido(); break;
        default: break;
        }
    }
    *exp_err = m.err; *exp_wrn = m.wrn;
}

static void cena_06_err_opcode(void)
{
    int opc, i, rep, R = esc(300), max_polls = 0, op;
    static const char *SEQS[] = { "IC", "CI", "ICI", "WIC", "WCI", "IW", "NIN", "ICWIC", "WWWCI", "CICIC" };
    static const int OPS_VALIDOS[] = { OP_NOOP_RAPIDO, OP_BSCROLL, OP_BTILE, OP_POSSP, OP_ATSP, OP_WRSP,
                                       OP_RGSP, OP_ATPO, OP_POSPO, OP_TAMPO, OP_CORPO, OP_PRIOR };
    uint64_t *polls_t;
    unsigned long soma_polls = 0;
    int antigo = g_auto_flags;
    g_auto_flags = 0;

    resetar_janela();
    garantir_periodo();
    limpar_sprites_poligonos();
    cmd_clear_status();

    printf("      --- 6.1 Opcodes invalidos 13..31 (3 cargas uteis cada) ---\n");
    for (opc = 13; opc <= 31; opc++) {
        static const uint32_t PAY[3] = { 0x0000000u, 0x7FFFFFFu, 0x2AAAAAAu };
        int ok = 0, p;
        for (p = 0; p < 3; p++) {
            uint32_t carga = (p == 2) ? (rnd() & 0x7FFFFFFu) : PAY[p];
            cmd_clear_status();
            cmd_raw((uint32_t)opc | (carga << 5));
            if (flags_conferem(1, 0)) ok++;
        }
        checar(ok == 3, "opcode %2d invalido: err_opcode=1 e wrn_param=0 nas 3 cargas (%d/3)", opc, ok);
    }
    cmd_clear_status();
    verificar_flags("CLST limpa err_opcode", 0, 0);

    printf("      --- 6.2 Opcodes VALIDOS 0..12 nunca setam err_opcode ---\n");
    for (op = OP_NOOP; op <= OP_CLST; op++) {
        cmd_clear_status();
        op_ck(op, 3u);
        {
            char d[64];
            snprintf(d, sizeof d, "opcode valido '%s' (err deve ficar 0)", NOMES_OP[op]);
            verificar_flags(d, 0, 0);
        }
    }

    printf("      --- 6.3 Retencao de err_opcode apos 240 instrucoes validas ---\n");
    cmd_clear_status();
    cmd_raw(20u | (0x0F0F0u << 5));
    for (i = 0; i < 240; i++) op_ck(OPS_VALIDOS[i % 12], (uint32_t)i);
    verificar_flags("err_opcode retido apos 240 validas", 1, 0);
    cmd_clear_status();
    verificar_flags("CLST limpa", 0, 0);

    printf("      --- 6.4 Ordem das instrucoes na FIFO (I=invalido C=CLST W=wrn N=noop rapido) ---\n");
    for (i = 0; i < (int)(sizeof SEQS / sizeof SEQS[0]); i++) {
        unsigned ee, ew;
        char d[80];
        cmd_clear_status();
        executar_sequencia(SEQS[i], &ee, &ew);
        snprintf(d, sizeof d, "sequencia \"%s\" enviada em rajada", SEQS[i]);
        verificar_flags(d, ee, ew);
    }

    printf("      --- 6.5 Latencia de visibilidade da flag apos o envio ---\n");
    polls_t = (uint64_t *)malloc((size_t)R * sizeof *polls_t);
    for (rep = 0; rep < R; rep++) {
        int polls = 0;
        uint64_t t0;
        uint32_t r;
        cmd_clear_status();
        uc_drenar(1000000000ULL);
        cmd_raw(13u | (0x55u << 5));
        t0 = agora_ns();
        do { r = raw_status(); polls++; } while (!(r & ST_BIT_ERR_OPCODE) && polls < 10000);
        polls_t[rep] = agora_ns() - t0;
        soma_polls += (unsigned long)polls;
        if (polls > max_polls) max_polls = polls;
    }
    {
        Estat e = estat_calc(polls_t, (size_t)R);
        imprimir_estat_us("retorno do envio -> flag visivel", &e);
        informar("leituras de status ate a flag aparecer: media %.2f, max %d", (double)soma_polls / (double)R, max_polls);
        checar(max_polls <= 8, "err_opcode visivel em <= 8 leituras apos o retorno do envio (max %d)", max_polls);
    }
    free(polls_t);
    cmd_clear_status();
    verificar_flags("limpeza final", 0, 0);

    g_auto_flags = antigo;
    esperar("ERR_OPCODE 1/1 - Opcodes invalidos, retencao e ordem",
            "Sem mudanca visual relevante; veja as verificacoes de err_opcode acima.");
}

/* ---------------------------------------------------------------------------
 *  CENA 7: Fuzz com scoreboard - compara flags do hardware com um modelo em
 *          software depois de rajadas aleatorias (valores validos e invalidos).
 *          Tambem valida o EMPACOTAMENTO de campos do driver Assembly.
 * ------------------------------------------------------------------------- */
static unsigned gen_campo(unsigned limite, unsigned max_campo)
{
    switch (rnd() & 3u) {
    case 0: case 1:
        return rnd() % limite;
    case 2: {
        static const int d[4] = { -2, -1, 0, 1 };
        int v = (int)limite + d[rnd() & 3u];
        if (v < 0) v = 0;
        if ((unsigned)v > max_campo) v = (int)max_campo;
        return (unsigned)v;
    }
    default:
        return rnd() % (max_campo + 1u);
    }
}

typedef struct { int op; unsigned a, b, c, d; } Instr;

static void fuzz_gerar(Instr *in)
{
    static const int OPS[] = { OP_BSCROLL, OP_BTILE, OP_POSSP, OP_ATSP, OP_WRSP, OP_RGSP, OP_ATPO,
                               OP_POSPO, OP_TAMPO, OP_CORPO, OP_PRIOR, OP_CLST, OP_RAW_INV, OP_NOOP_RAPIDO,
                               OP_BTILE, OP_POSSP, OP_POSPO, OP_TAMPO, OP_ATPO, OP_BSCROLL };
    in->op = OPS[rnd() % (sizeof OPS / sizeof OPS[0])];
    in->a = in->b = in->c = in->d = 0;
    switch (in->op) {
    case OP_BSCROLL: in->a = gen_campo(40, 63); in->b = gen_campo(30, 31); break;
    case OP_BTILE:   in->a = gen_campo(N_PIXELS_BG, 131071u); in->b = rnd() & 0xFFu; break;
    case OP_POSSP: case OP_POSPO: case OP_TAMPO:
        in->a = rnd() & ((in->op == OP_POSSP) ? 31u : 15u);
        in->b = gen_campo(320, 511); in->c = gen_campo(240, 255); break;
    case OP_ATSP:    in->a = rnd() & 31u; in->b = rnd() & 3u; in->c = rnd() & 1u; break;
    case OP_WRSP:    in->a = (18u + (rnd() & 1u)) * 256u + (rnd() & 255u); in->b = rnd() & 0xFFu; break;
    case OP_RGSP:    in->a = rnd() & 31u; in->b = rnd() & 31u; break;
    case OP_ATPO:    in->a = rnd() & 15u; in->b = rnd() & 1u; in->c = gen_campo(5, 7); in->d = rnd() & 3u; break;
    case OP_CORPO:   in->a = rnd() & 15u; in->b = rnd() & 0xFFu; break;
    case OP_PRIOR:   in->a = rnd() & 1u; break;
    case OP_RAW_INV: in->a = (13u + rnd() % 19u) | ((rnd() & 0x7FFFFFFu) << 5); break;
    default: break;
    }
}

static void fuzz_imprimir(const Instr *seq, int k)
{
    int i;
    for (i = 0; i < k; i++)
        printf("          #%d %-12s a=%u b=%u c=%u d=%u\n", i, NOMES_OP[seq[i].op], seq[i].a, seq[i].b, seq[i].c, seq[i].d);
}

static void cena_07_fuzz_flags(void)
{
    int it, N = esc(20000), k, j, falhas = 0, drv_div = 0;
    unsigned long n_instr = 0, n_err_esp = 0, n_wrn_esp = 0;
    ModeloFlags m = { 0, 0 };
    Instr seq[8];
    uint64_t t0;
    int antigo = g_auto_flags;
    g_auto_flags = 0;

    resetar_janela();
    limpar_sprites_poligonos();
    cmd_clear_status();
    verificar_flags("fuzz: estado inicial", 0, 0);

    printf("      Fuzz: %d rajadas de 1..8 instrucoes aleatorias (valores validos, limites e invalidos)...\n", N);
    t0 = agora_ns();
    for (it = 0; it < N; it++) {
        Snap s;
        if (g_sair) sair_limpo(130);
        k = 1 + (int)(rnd() % 8u);
        for (j = 0; j < k; j++) {
            fuzz_gerar(&seq[j]);
            cmd_gen(seq[j].op, seq[j].a, seq[j].b, seq[j].c, seq[j].d);
            modelo_aplicar(&m, seq[j].op, seq[j].a, seq[j].b, seq[j].c);
        }
        n_instr += (unsigned long)k;
        if (uc_drenar(1000000000ULL) < 0) { checar(0, "fuzz: UC nao drenou na rajada %d", it); break; }
        for (j = 0; j < 4; j++) (void)raw_status();
        s = ler_snap();
        if (s.err != m.err || s.wrn != m.wrn) {
            falhas++;
            if (falhas <= 8) {
                checar(0, "fuzz rajada %d: HW(err=%u wrn=%u) != modelo(err=%u wrn=%u). Sequencia:", it, s.err, s.wrn, m.err, m.wrn);
                fuzz_imprimir(seq, k);
            } else g_ck_fail[g_cena_atual]++;
            m.err = s.err; m.wrn = s.wrn;         /* ressincroniza para nao propagar */
        }
        if ((int)s.err != s.d_err || (int)s.wrn != s.d_wrn) drv_div++;
        if (m.err) n_err_esp++;
        if (m.wrn) n_wrn_esp++;
    }
    {
        double dt = (double)(agora_ns() - t0) / 1e9;
        printf("      %lu instrucoes em %.2f s (%.0f instr/s incluindo drenagem e leitura de status)\n",
               n_instr, dt, (double)n_instr / dt);
        printf("      Estados observados: err=1 em %lu rajadas | wrn=1 em %lu rajadas (cobertura dos dois lados)\n", n_err_esp, n_wrn_esp);
    }
    checar(falhas == 0, "scoreboard: flags do hardware == modelo em %d rajadas (divergencias: %d)", N, falhas);
    checar(drv_div == 0, "error()/warning() do driver == bits do status() em todas as rajadas (div=%d)", drv_div);
    checar(n_err_esp > 0 && n_wrn_esp > 0, "fuzz exercitou err=1 e wrn=1 (a cobertura valida o teste)");

    cmd_clear_status();
    verificar_flags("fuzz: limpeza final", 0, 0);
    g_auto_flags = antigo;
    limpar_sprites_poligonos();
    esperar("FUZZ 1/1 - Scoreboard de flags e empacotamento do driver",
            "Caos de cores/sprites/poligonos (esperado); o importante e o scoreboard acima: 0 divergencias.");
}

/* ============================================================================
 *  9. CENAS - Grupo C: latencia, vazao e cadencia de quadros
 * ========================================================================== */

/* ---------------------------------------------------------------------------
 *  CENA 8: Latencia por opcode com percentis (amostras individuais)
 * ------------------------------------------------------------------------- */
static void cena_08_latencia_opcodes(void)
{
    int N = esc(5000), op, i;
    uint64_t *v = (uint64_t *)malloc((size_t)N * sizeof *v);
    uint64_t sv[2000];
    unsigned long hist_all[HIST_BINS];
    double min_p50 = 1e18, max_p50 = 0.0;
    const char *nome_min = "", *nome_max = "";
    Estat ref, e;
    FILE *f;
    int antigo = g_auto_flags;
    g_auto_flags = 0;

    resetar_janela();
    garantir_periodo();
    limpar_sprites_poligonos();
    cmd_clear_status();
    memset(hist_all, 0, sizeof hist_all);
    f = csv_abrir("latencia_opcodes", "opcode,n,min_ns,p50_ns,p90_ns,p99_ns,p999_ns,max_ns,media_ns,desvio_ns,bloqueios");

    for (i = 0; i < 2000; i++) { uint64_t t0 = agora_ns(); (void)raw_status(); sv[i] = agora_ns() - t0; }
    ref = estat_calc(sv, 2000);
    printf("      Referencia: 1 leitura MMIO do status() = %.3f us (p50). Overhead do cronometro descontado: %llu ns.\n",
           (double)ref.p50 / 1e3, (unsigned long long)g_overhead_timer_ns);
    printf("      Amostras por opcode: %d (cada chamada cronometrada individualmente, sem NOOP-VSYNC)\n", N);
    printf("  ------------------------------------------------------------------------------------------------------------\n");
    printf("  %-13s %6s %8s %8s %8s %8s %9s %10s %8s %7s %6s %9s %5s\n",
           "Opcode", "N", "min us", "p50 us", "p90 us", "p99 us", "p99.9 us", "max us", "med us", "dp us", "~leit", "instr/s", "blq");
    printf("  ------------------------------------------------------------------------------------------------------------\n");

    for (op = OP_NOOP_RAPIDO; op < N_OPS; op++) {
        unsigned long bloq = 0;
        int ok;
        uc_drenar(1000000000ULL);
        for (i = 0; i < N; i++) {
            uint64_t t0 = agora_ns(), dt;
            int rc = enviar_op(op, (uint32_t)i);
            dt = agora_ns() - t0;
            if (rc != 0) abortar_erro_hw(NOMES_OP[op]);
            g_total_enviadas++;
            if (dt > STALL_NS) bloq++;
            v[i] = (dt > g_overhead_timer_ns) ? dt - g_overhead_timer_ns : 0;
            hist_all[bin_log2(v[i] ? v[i] : 1)]++;
        }
        e = estat_calc(v, (size_t)N);
        printf("  %-13s %6zu %8.2f %8.2f %8.2f %8.2f %9.2f %10.2f %8.2f %7.2f %6.1f %9.0f %5lu\n",
               NOMES_OP[op], e.n, (double)e.min / 1e3, (double)e.p50 / 1e3, (double)e.p90 / 1e3, (double)e.p99 / 1e3,
               (double)e.p999 / 1e3, (double)e.max / 1e3, e.media / 1e3, e.desvio / 1e3,
               ref.p50 ? (double)e.p50 / (double)ref.p50 : 0.0, 1e9 / e.media, bloq);
        if (f) fprintf(f, "%s,%zu,%llu,%llu,%llu,%llu,%llu,%llu,%.0f,%.0f,%lu\n", NOMES_OP[op], e.n,
                       (unsigned long long)e.min, (unsigned long long)e.p50, (unsigned long long)e.p90,
                       (unsigned long long)e.p99, (unsigned long long)e.p999, (unsigned long long)e.max,
                       e.media, e.desvio, bloq);
        if ((double)e.p50 < min_p50) { min_p50 = (double)e.p50; nome_min = NOMES_OP[op]; }
        if ((double)e.p50 > max_p50) { max_p50 = (double)e.p50; nome_max = NOMES_OP[op]; }

        ok = (op == OP_RAW_INV) ? flags_conferem(1, 0) : flags_conferem(0, 0);
        checar(ok, "%-13s: p50=%.2f us (< 100 us) e flags %s apos %d envios validos",
               NOMES_OP[op], (double)e.p50 / 1e3, (op == OP_RAW_INV) ? "err=1/wrn=0" : "err=0/wrn=0", N);
        checar(e.p50 < 100000ULL, "%-13s: handshake saudavel (mediana %.2f us)", NOMES_OP[op], (double)e.p50 / 1e3);
        if (op == OP_RAW_INV) cmd_clear_status();
    }
    printf("  ------------------------------------------------------------------------------------------------------------\n");
    printf("      Colunas: ~leit = mediana expressa em leituras de status(); blq = chamadas > 0,5 ms (preempcao do Linux ou FIFO cheia).\n");
    informar("mediana mais rapida: %s (%.2f us) | mais lenta: %s (%.2f us) | razao %.2fx",
             nome_min, min_p50 / 1e3, nome_max, max_p50 / 1e3, min_p50 > 0 ? max_p50 / min_p50 : 0.0);
    imprimir_hist("todas as instrucoes", hist_all);
    if (f) fclose(f);
    free(v);
    cmd_bscroll(0, 0);
    g_auto_flags = antigo;
    esperar("LATENCIA 1/1 - Percentis por opcode",
            "Fundo/sprites/poligonos em caos aleatorio (esperado); o resultado esta na tabela de latencia acima.");
}

/* ---------------------------------------------------------------------------
 *  CENA 9: Vazao sustentada e orcamento de instrucoes por quadro
 * ------------------------------------------------------------------------- */
static void cena_09_vazao_orcamento(void)
{
    static const int OPS_V[] = { OP_NOOP_RAPIDO, OP_BSCROLL, OP_BTILE, OP_POSSP, OP_ATSP, OP_WRSP, OP_RGSP,
                                 OP_ATPO, OP_POSPO, OP_TAMPO, OP_CORPO, OP_PRIOR, OP_CLST };
    static const int KS[] = { 16, 64, 128, 256, 512, 1024, 2048, 3072, 4096, 5120, 6144, 8192, 12288, 16384 };
    int i, j, B = esc(50000), F = esc(60), k_ok = 0, falhas_seg = 0, rep;
    double us_por_instr_ref = 0.0;
    FILE *f;

    resetar_janela();
    garantir_periodo();
    limpar_tela_padrao();
    f = csv_abrir("orcamento_quadro", "instr_por_quadro,custo_estimado_ms,us_por_instr_no_laco(inclui_espera_FIFO),quadros_ociosos,fps_app,ok");

    printf("      --- 9.1 Vazao sustentada por opcode (rajada de %d, sem cronometro por chamada) ---\n", B);
    printf("  %-13s %9s %10s %10s %11s %14s\n", "Opcode", "N", "tempo ms", "us/instr", "instr/s", "instr/quadro");
    for (i = 0; i < (int)(sizeof OPS_V / sizeof OPS_V[0]); i++) {
        uint64_t t0, dt;
        int op = OPS_V[i];
        uc_drenar(1000000000ULL);
        t0 = agora_ns();
        for (j = 0; j < B; j++) if (enviar_op(op, (uint32_t)j) != 0) abortar_erro_hw(NOMES_OP[op]);
        dt = agora_ns() - t0;
        g_total_enviadas += (unsigned long)B;
        printf("  %-13s %9d %10.2f %10.3f %11.0f %14.0f\n", NOMES_OP[op], B, (double)dt / 1e6,
               (double)dt / 1e3 / (double)B, (double)B * 1e9 / (double)dt,
               (double)B * (double)g_periodo_ns / (double)dt);
        if (op == OP_POSPO) us_por_instr_ref = (double)dt / 1e3 / (double)B;
        checar(flags_conferem(0, 0), "%-13s: %d instrucoes em rajada sem acionar err/wrn", NOMES_OP[op], B);
    }
    cmd_bscroll(0, 0);

    printf("      --- 9.2 Orcamento por quadro: k instrucoes + 1 NOOP por quadro, %d quadros por ponto ---\n", F);
    if (F < 24) F = 24;
    limpar_tela_padrao();
    poligono_centro(0, POLY_RETANGULO, FLIP_CIMA, 160, 120, 28, 28, COR_AMARELO);
    printf("      (custo estimado = k x %.3f us/instr medidos em 9.1; o laco real e freado pela FIFO cheia enquanto a UC espera o VSYNC,\n"
           "       por isso o criterio de saturacao e 'qd ociosos' = quadros em que a UC ficou sem instrucoes)\n", us_por_instr_ref);
    printf("  %8s %13s %9s %11s %9s  %s\n", "k/quadro", "custo est. ms", "% quadro", "qd ociosos", "FPS app", "veredito");
    {
        int consecutivos = 0, parar = 0;
        for (i = 0; i < (int)(sizeof KS / sizeof KS[0]) && !parar; i++) {
            int k = KS[i], ok;
            uint32_t fc0, fc1;
            uint64_t t0, wall, ns_envio = 0;
            int64_t d;
            long perdidos;
            double fps;
            uc_drenar(2000000000ULL);
            (void)sincronizar_inicio_de_quadro(&fc0);
            t0 = agora_ns();
            for (rep = 0; rep < F; rep++) {
                unsigned x = (unsigned)tri(rep * 4, LARGURA - 30), y = (unsigned)tri(rep * 3, ALTURA - 30);
                uint64_t ta = agora_ns();
                for (j = 0; j < k; j++) if (pospo(0, x, y) != 0) abortar_erro_hw("pospo");
                ns_envio += agora_ns() - ta;
                g_total_enviadas += (unsigned long)k;
                frame();
            }
            d = uc_drenar(5000000000ULL);
            fc1 = raw_status() >> ST_SHIFT_FC;
            wall = agora_ns() - t0;
            perdidos = (long)((fc1 - fc0) & 0xFFFFu) - (long)F;
            fps = (double)F * 1e9 / (double)wall;
            ok = (d >= 0) && (perdidos <= (F / 50 > 1 ? F / 50 : 1));
            {
                double est_ms = (double)k * us_por_instr_ref / 1e3;
                printf("  %8d %13.3f %8.1f%% %11ld %9.2f  %s\n", k, est_ms, 100.0 * est_ms * 1e6 / (double)g_periodo_ns,
                       perdidos, fps, ok ? "SUSTENTA 60 FPS" : "ESTOURA");
                if (f) fprintf(f, "%d,%.4f,%.4f,%ld,%.3f,%d\n", k, est_ms, (double)ns_envio / 1e3 / ((double)F * (double)k), perdidos, fps, ok);
            }
            if (ok) { k_ok = k; consecutivos = 0; } else if (++consecutivos >= 2) parar = 1;
            if (i == 0) checar(ok, "16 instrucoes por quadro sustentam 60 FPS (qd ociosos %ld)", perdidos);
        }
    }
    if (f) fclose(f);
    informar("MAXIMO SUSTENTAVEL medido: ~%d instrucoes por quadro (teorico por latencia do POSPO: %.0f)",
             k_ok, us_por_instr_ref > 0 ? (double)g_periodo_ns / 1e3 / us_por_instr_ref : 0.0);
    falhas_seg = (k_ok == 0);
    checar(!falhas_seg, "ao menos um ponto do orcamento sustentou 60 FPS");

    printf("      --- 9.3 Custo de redesenhar o fundo inteiro (76800 BTILE) ---\n");
    for (rep = 0; rep < 3; rep++) {
        uint64_t t0, dt;
        int64_t d;
        uc_drenar(1000000000ULL);
        t0 = agora_ns();
        for (j = 0; j < N_PIXELS_BG; j++) if (btile((unsigned)j, (unsigned)(rep * 0x49 + 0x20)) != 0) abortar_erro_hw("btile");
        dt = agora_ns() - t0;
        d = uc_drenar(1000000000ULL);
        g_total_enviadas += N_PIXELS_BG;
        printf("      preenchimento %d: %.2f ms no driver (+%.2f ms ate a UC drenar) = %.1f quadros | %.0f pixels/s\n",
               rep + 1, (double)dt / 1e6, d > 0 ? (double)d / 1e6 : 0.0, (double)dt / (double)g_periodo_ns,
               (double)N_PIXELS_BG * 1e9 / (double)dt);
    }
    {
        uint64_t t0 = agora_ns(), dt;
        bg_preencher(COR_AZUL_ESCURO);
        dt = agora_ns() - t0;
        printf("      mesmo preenchimento COM cronometro por chamada: %.2f ms (custo da instrumentacao: ~%.0f ns/chamada)\n",
               (double)dt / 1e6, (double)g_overhead_timer_ns);
    }
    esperar("VAZAO 1/1 - Orcamento por quadro e custo do fundo",
            "Retangulo amarelo percorreu a tela com carga crescente; fundo trocou de cor 4 vezes.");
}

/* ---------------------------------------------------------------------------
 *  CENA 10: Cadencia de quadros sob carga (jitter, estouros, adiantamento da fila)
 * ------------------------------------------------------------------------- */
static void cena_10_pacing(void)
{
    static const int WS[5]       = { 2, 30, 300, 1500, 4000 };
    static const char *ROT[5]    = { "2 instr/quadro", "30 instr/quadro", "300 instr/quadro", "1500 instr/quadro", "4000 instr/quadro" };
    static const char *CSV[5]    = { "pacing_w2", "pacing_w30", "pacing_w300", "pacing_w1500", "pacing_w4000" };
    int w, f, j, F = esc(300);

    resetar_janela();
    garantir_periodo();
    limpar_tela_padrao();
    if (F < 80) F = 80;
    bg_xadrez(COR_CINZA_ESCURO, COR_AZUL_ESCURO, 16);
    poligono_centro(0, POLY_ESCALENO, FLIP_CIMA, 160, 120, 40, 40, COR_VERMELHO);
    sprite_cfg(0, SLOT_GATO_A, 100, 100, 0, 1);
    frames(2);
    esperar("PACING 0/5 - Cenario de teste",
            "Xadrez azul/cinza, gatinho e triangulo escaleno vermelho prontos para animar.");

    for (w = 0; w < 5; w++) {
        Pacing p;
        char titulo[96];
        pacing_ini(&p, (size_t)F);
        uc_drenar(2000000000ULL);
        p.fc0 = raw_status() >> ST_SHIFT_FC;
        for (f = 0; f < F; f++) {
            int gx = tri(f * 3, LARGURA - SPR_TAM), gy = tri(f * 2, ALTURA - SPR_TAM);
            sprite_pos(0, gx, gy);
            poligono_mover_centro(0, POLY_ESCALENO, 40 + tri(f * 2, 240), 60 + tri(f, 120), 40, 40);
            for (j = 2; j < WS[w]; j++) cmd_pospo(1, (unsigned)(f % 300), (unsigned)(j % 200));
            frame();
            pacing_marca(&p);
        }
        pacing_relatorio(&p, ROT[w], 24, CSV[w], WS[w] <= 300);
        snprintf(titulo, sizeof titulo, "PACING %d/5 - %s por quadro durante %d quadros", w + 1, ROT[w], F);
        esperar(titulo, "Gatinho e triangulo se movem; a suavidade depende de a carga caber em 16,7 ms por quadro.");
    }
}

/* ============================================================================
 *  10. CENAS - Grupo D: motores visuais (fundo por pixel, sprites, poligonos)
 * ========================================================================== */

/* ---------------------------------------------------------------------------
 *  CENA 11: BTILE por pixel - enderecamento, limites, descarte e velocidade
 * ------------------------------------------------------------------------- */
static void cena_11_btile_pixel(void)
{
    int x, y, i, k;
    static const unsigned BARRAS[8] = { COR_BRANCO, COR_AMARELO, COR_CIANO, COR_VERDE, COR_ROSA_CHOQUE, COR_VERMELHO, COR_AZUL, COR_FUNDO_PRETO };
    static const unsigned CINZAS[8] = { 0x00, 0x25, 0x49, 0x6E, 0x92, 0xB6, 0xDB, 0xFF };

    limpar_sprites_poligonos();
    resetar_janela();

    /* 11.1 enderecamento: cantos, bordas e mira central */
    bg_preencher(COR_FUNDO_PRETO);
    bg_contorno(0, 0, LARGURA - 1, ALTURA - 1, COR_BRANCO);
    bg_contorno(4, 4, LARGURA - 5, ALTURA - 5, COR_CINZA);
    bg_pixel(0, 0, COR_VERMELHO);
    bg_pixel(LARGURA - 1, 0, COR_VERDE);
    bg_pixel(0, ALTURA - 1, COR_AZUL);
    bg_pixel(LARGURA - 1, ALTURA - 1, COR_AMARELO);
    bg_retangulo(0, ALTURA / 2, LARGURA, 1, COR_CIANO);
    bg_retangulo(LARGURA / 2, 0, 1, ALTURA, COR_ROSA_CHOQUE);
    frames(2);
    esperar("BTILE-PIXEL 1/7 - Enderecamento y*320+x, cantos e bordas",
            "Moldura branca na borda (pode ser cortada pelo overscan do monitor) e cinza a 4 px; pixels de canto: sup-esq VERMELHO, sup-dir VERDE, inf-esq AZUL, inf-dir AMARELO; mira ciano (horizontal) e magenta (vertical) no centro.");

    /* 11.2 descarte de enderecos >= 76800 */
    bg_preencher(COR_FUNDO_PRETO);
    bg_retangulo(0, 0, LARGURA, 8, COR_VERMELHO);              /* primeiras 8 linhas em vermelho */
    for (k = 0; k < LARGURA; k++) cmd_btile((unsigned)(N_PIXELS_BG + k), COR_VERDE);   /* alias da linha 0 se houvesse wrap */
    cmd_btile(76800u, COR_AZUL); cmd_btile(131071u, COR_BRANCO); cmd_btile(100000u, COR_AMARELO);
    verificar_flags("BTILE end>=76800 levanta wrn_param", 0, 1);
    cmd_clear_status();
    verificar_flags("CLST apos escritas fora de faixa", 0, 0);
    frames(2);
    esperar("BTILE-PIXEL 2/7 - Escritas fora da faixa sao DESCARTADAS",
            "Faixa vermelha no topo (8 linhas) 100% vermelha: nenhum pixel verde/azul/branco/amarelo apareceu (sem wrap para o endereco 0). Resto preto.");

    /* 11.3 gradiente por pixel (preenchimento completo cronometrado pela janela) */
    bg_gradiente();
    frames(2);
    esperar("BTILE-PIXEL 3/7 - Gradiente RGB332 por pixel (76800 btile)",
            "Vermelho cresce da esquerda p/ direita, verde de cima p/ baixo, azul na diagonal; transicoes suaves em degraus de 40x30 px.");

    /* 11.4 barras de cor + rampa de cinza + preto explicito */
    for (i = 0; i < 8; i++) bg_retangulo(i * 40, 0, 40, 120, BARRAS[i]);
    for (i = 0; i < 8; i++) bg_retangulo(i * 40, 120, 40, 60, CINZAS[i]);
    bg_retangulo(0, 180, LARGURA, 60, COR_FUNDO_PRETO);
    bg_retangulo(100, 195, 120, 30, COR_CINZA_ESCURO);
    frames(2);
    esperar("BTILE-PIXEL 4/7 - Barras de cor, rampa de cinza e preto 0x00",
            "8 barras (branco, amarelo, ciano, verde, rosa, vermelho, azul, PRETO 0x00 visivel como preto); abaixo rampa de 8 cinzas; faixa preta com retangulo cinza escuro.");

    /* 11.5 procedural por pixel: aneis concentricos */
    for (y = 0; y < ALTURA; y++)
        for (x = 0; x < LARGURA; x++) {
            int dx = x - 160, dy = y - 120;
            unsigned r = (unsigned)((dx * dx + dy * dy) / 400);
            unsigned cor = (unsigned)(((r & 7u) << 5) | (((r >> 3) & 7u) << 2) | ((r >> 6) & 3u));
            cmd_btile((unsigned)(y * LARGURA + x), cor);
        }
    frames(2);
    esperar("BTILE-PIXEL 5/7 - Imagem procedural (aneis) escrita pixel a pixel",
            "Aneis concentricos coloridos centrados em (160,120), sem falhas nem pixels errados.");

    /* 11.6 retangulos aleatorios: pixels/s com area variada */
    bg_preencher(COR_FUNDO_PRETO);
    for (i = 0; i < esc(300); i++) {
        int w = 4 + (int)(rnd() % 60u), h = 4 + (int)(rnd() % 40u);
        bg_retangulo((int)(rnd() % (unsigned)(LARGURA - w)), (int)(rnd() % (unsigned)(ALTURA - h)), w, h, 1u + (rnd() % 255u));
    }
    frames(2);
    esperar("BTILE-PIXEL 6/7 - Retangulos aleatorios (mosaico)",
            "Mosaico de retangulos coloridos sobrepostos, todos dentro da tela.");

    /* 11.7 escrita de todos os 256 valores de cor */
    for (i = 0; i < 256; i++) bg_retangulo((i % 16) * 20, (i / 16) * 15, 20, 15, (unsigned)i);
    frames(2);
    esperar("BTILE-PIXEL 7/7 - Paleta completa RGB332 (256 cores)",
            "Grade 16x16 com as 256 cores RGB332 em ordem (0x00 preto no canto superior esquerdo, 0xFF branco no inferior direito).");
}

/* ---------------------------------------------------------------------------
 *  CENA 12: BSCROLL sobre o mapa por pixel (unidade = tile 8x8) e travamento no VSYNC
 * ------------------------------------------------------------------------- */
static void bg_padrao_scroll(void)
{
    int x, y;
    for (y = 0; y < ALTURA; y++)
        for (x = 0; x < LARGURA; x++) {
            int tx = x / 8, ty = y / 8, ox = x % 8, oy = y % 8;
            unsigned cor;
            if (tx < 3 && ty < 3)  cor = COR_AMARELO;                          /* marcador de origem 3x3 tiles */
            else if (ox == oy)     cor = COR_BRANCO;                           /* diagonal em cada tile */
            else if (ox == 0 || oy == 0) cor = COR_PRETO_SOLIDO;               /* grade de tiles */
            else cor = (unsigned)((((tx * 5) & 7) << 5) | (((ty * 7) & 7) << 2) | ((tx + ty) & 3)) | 0x01u;
            cmd_btile((unsigned)(y * LARGURA + x), cor);
        }
}

static void cena_12_bscroll(void)
{
    int i;

    limpar_sprites_poligonos();
    resetar_janela();
    bg_padrao_scroll();
    frames(2);
    esperar("BSCROLL 1/6 - Referencia (0,0)",
            "Mosaico de tiles 8x8 com diagonal branca e grade preta em cada tile; marcador AMARELO de 3x3 tiles no canto superior esquerdo.");

    for (i = 0; i < TILES_X; i++) { cmd_bscroll((unsigned)i, 0); frames(2); }
    esperar("BSCROLL 2/6 - Varredura horizontal X=0..39",
            "O mosaico andou 8 px por passo para a ESQUERDA com wrap perfeito; o marcador amarelo reapareceu pela direita.");

    for (i = 0; i < TILES_Y; i++) { cmd_bscroll(0, (unsigned)i); frames(2); }
    esperar("BSCROLL 3/6 - Varredura vertical Y=0..29",
            "O mosaico subiu 8 px por passo com wrap perfeito em 30 linhas; o marcador amarelo reapareceu por baixo.");

    {
        int N = esc(240);
        for (i = 0; i < N; i++) { cmd_bscroll((unsigned)(i % TILES_X), (unsigned)((i * 2) % TILES_Y)); frame(); }
    }
    esperar("BSCROLL 4/6 - Rolagem diagonal sincronizada (1 bscroll + 1 noop por quadro)",
            "Rolagem diagonal fluida, sem rasgos (tearing) nem saltos.");

    for (i = 0; i < esc(60); i++) {
        int j;
        for (j = 0; j < TILES_X; j++) cmd_bscroll((unsigned)j, 5u);   /* 40 escritas por quadro */
        frame();
    }
    esperar("BSCROLL 5/6 - 40 bscroll por quadro: so o ULTIMO vale (travado no VSYNC)",
            "Imagem PARADA (sem tremer) com scroll X=39,Y=5: so o ultimo valor de cada quadro e aplicado.");

    cmd_bscroll(TILES_X - 1, TILES_Y - 1);
    frames(2);
    cmd_bscroll(0, 0);
    frames(2);
    esperar("BSCROLL 6/6 - Extremo (39,29) e retorno a (0,0)",
            "Deslocamento maximo sem wrn_param, depois volta ao quadro de referencia (marcador amarelo no canto sup-esq).");
}

/* ---------------------------------------------------------------------------
 *  CENA 13: Sprites individuais (RGSP/POSSP/ATSP com os desenhos do sprites.mif)
 * ------------------------------------------------------------------------- */
static void cena_13_sprites_mif(void)
{
    int i, t;

    limpar_sprites_poligonos();
    resetar_janela();
    bg_xadrez(COR_CINZA_ESCURO, COR_ROXO, 16);
    frames(2);

    sprite_cfg(0, SLOT_GATO_A, 152, 112, 0, 1);
    frames(2);
    esperar("SPRITE 1/6 - Gatinho do slot 0 no centro",
            "Gatinho do sprites.mif (slot 0) em (152,112) com fundo transparente sobre o xadrez.");

    sprite_cfg(0, SLOT_GATO_A,  68,  84, 0, 1);
    sprite_cfg(1, SLOT_GATO_A, 124,  84, 1, 1);
    sprite_cfg(2, SLOT_GATO_A, 180,  84, 2, 1);
    sprite_cfg(3, SLOT_GATO_A, 236,  84, 3, 1);
    sprite_cfg(4, SLOT_LETRA_G,  68, 132, 0, 1);
    sprite_cfg(5, SLOT_LETRA_G, 124, 132, 1, 1);
    sprite_cfg(6, SLOT_LETRA_G, 180, 132, 2, 1);
    sprite_cfg(7, SLOT_LETRA_G, 236, 132, 3, 1);
    frames(2);
    esperar("SPRITE 2/6 - Espelhamentos (ATSP flip_h / flip_v)",
            "Linha de cima: gatinho Normal, FlipH, FlipV, FlipH+V | linha de baixo: letra 'G' nas mesmas 4 orientacoes.");

    esconder_todos_sprites();
    sprite_cfg(0, SLOT_GATO_A,   0,   0, 0, 1);
    sprite_cfg(1, SLOT_GATO_B, 304,   0, 0, 1);
    sprite_cfg(2, SLOT_FOGO,     0, 224, 0, 1);
    sprite_cfg(3, 12,          304, 224, 0, 1);
    frames(2);
    esperar("SPRITE 3/6 - Limites geometricos nos 4 cantos",
            "4 sprites cravados nos cantos: (0,0), (304,0), (0,224) e (304,224), sem cortes nem wrap.");

    sprite_cfg(0, SLOT_FOGO,   142, 102, 0, 1);
    sprite_cfg(1, SLOT_GATO_A, 148, 108, 0, 1);
    sprite_cfg(2, SLOT_GATO_B, 154, 114, 0, 1);
    sprite_cfg(3, 2,           160, 120, 0, 1);
    frames(2);
    esperar("SPRITE 4/6 - Prioridade por ID (0 > 1 > 2 > 3) e transparencia",
            "Pilha diagonal: Foguinho (ID 0) por cima do Gatinho 0 (ID 1), por cima do Gatinho 1 (ID 2), por cima do retrato slot 2 (ID 3).");

    esconder_todos_sprites();
    sprite_cfg(0, SLOT_GATO_A, 0, 0, 0, 1);
    sprite_cfg(1, SLOT_FOGO,   0, 0, 0, 1);
    for (t = 0; t < esc(240); t++) {
        int tt = t > 6 ? t - 6 : 0;
        sprite_pos(0, tri(t * 3, LARGURA - SPR_TAM), tri(t * 2, ALTURA - SPR_TAM));
        sprite_pos(1, tri(tt * 3, LARGURA - SPR_TAM), tri(tt * 2, ALTURA - SPR_TAM));
        frame();
    }
    esperar("SPRITE 5/6 - Animacao de rebote a 60 FPS (POSSP por quadro)",
            "Gatinho percorre a tela seguido pelo foguinho (6 quadros atras), sem tearing nem fantasma.");

    sprite_pos(0, 152, 112);
    cmd_atsp(1, 0, 0);
    for (i = 0; i < 8; i++) { cmd_atsp(0, 0, (unsigned)(i & 1)); frames(20); }
    esperar("SPRITE 6/6 - Visibilidade (ATSP show=0/1)",
            "O gatinho piscou 4 vezes no centro e terminou visivel.");
}

/* ---------------------------------------------------------------------------
 *  CENA 14: 32 sprites simultaneos e remapeamento de slots (RGSP)
 * ------------------------------------------------------------------------- */
static void cena_14_sprites_32(void)
{
    int i, t;
    int px[N_SPRITES], py[N_SPRITES];

    limpar_sprites_poligonos();
    resetar_janela();
    bg_cenario_noturno();

    for (i = 0; i < N_SPRITES; i++) {
        px[i] = 20 + (i % 8) * 36;
        py[i] = 28 + (i / 8) * 48;
        sprite_cfg(i, i, px[i], py[i], 0, 1);
    }
    frames(2);
    esperar("32 SPRITES 1/4 - Galeria dos 32 slots da RAM",
            "Grade 8x4 com TODOS os 32 desenhos do sprites.mif (gatinhos, retratos, foguinho e letras GATiNHOS PBL1).");

    for (i = 0; i < N_SPRITES; i++) cmd_rgsp((unsigned)i, (unsigned)(31 - i));
    frames(2);
    esperar("32 SPRITES 2/4 - Troca instantanea de slots (RGSP 31-i)",
            "Mesmas posicoes, desenhos invertidos (slot 31 no canto sup-esq ... slot 0 no inf-dir).");

    for (i = 0; i < N_SPRITES; i++) cmd_rgsp((unsigned)i, (unsigned)i);
    for (t = 0; t < esc(300); t++) {
        for (i = 0; i < N_SPRITES; i++)
            sprite_pos(i, tri(px[i] + t * (1 + (i % 4)), LARGURA - SPR_TAM), tri(py[i] + t * (1 + (i % 3)), ALTURA - SPR_TAM));
        frame();
    }
    esperar("32 SPRITES 3/4 - 32 sprites em movimento simultaneo (32 POSSP + NOOP por quadro)",
            "Todos rebatem pela tela em velocidades distintas a 60 FPS.");

    for (i = 0; i < N_SPRITES; i++) sprite_pos(i, 80 + i * 5, 40 + i * 4);
    frames(2);
    esperar("32 SPRITES 4/4 - Cascata de prioridade fisica (ID 0..31)",
            "Cascata diagonal: o sprite 0 sobrepoe o 1, que sobrepoe o 2 ... ate o 31.");
}

/* ---------------------------------------------------------------------------
 *  CENA 15: WRSP - pintura dinamica de UM slot (18) + velocidade + transparencia
 * ------------------------------------------------------------------------- */
static void cena_15_wrsp(void)
{
    int x, y, c;
    static const unsigned SOLIDAS[6] = { COR_VERMELHO, COR_VERDE, COR_AZUL, COR_AMARELO, COR_CIANO, COR_ROSA_CHOQUE };

    limpar_sprites_poligonos();
    resetar_janela();
    bg_xadrez(COR_AZUL_ESCURO, COR_PRETO_SOLIDO, 16);
    bg_moldura(COR_OURO, 4);

    poligono_centro(15, POLY_RETANGULO, FLIP_CIMA, 160, 120, 120, 90, COR_VINHO);
    poligono_centro(14, POLY_RETANGULO, FLIP_CIMA, 160, 120, 112, 82, COR_AZUL_ESCURO);
    for (x = 0; x < 8; x++) sprite_cfg(2 + x, 20 + x, 68 + x * 24, 24, 0, 1);
    sprite_cfg(0,  SLOT_DINAMICO, 152, 112, 0, 1);
    sprite_cfg(10, SLOT_DINAMICO, 112,  86, 0, 1);
    sprite_cfg(11, SLOT_DINAMICO, 192,  86, 1, 1);
    sprite_cfg(12, SLOT_DINAMICO, 112, 138, 2, 1);
    sprite_cfg(13, SLOT_DINAMICO, 192, 138, 3, 1);
    sprite_cfg(1, SLOT_FOGO, 152, 112, 0, 1);
    frames(2);
    esperar("WRSP 1/5 - Antes: slot 18 original em 5 copias",
            "Palco central com 5 copias (4 espelhadas + 1 central) do slot 18 original do .mif; 'GATiNHOS' intacto no topo.");

    printf("      Pintando o slot %d lentamente (256 wrsp, 1 quadro a cada 2 pixels)...\n", SLOT_DINAMICO);
    for (y = 0; y < SPR_TAM; y++) {
        for (x = 0; x < SPR_TAM; x++) {
            sprite_pos(1, 152 + x - 4, 112 + y - 12);
            cmd_wrsp((unsigned)(SLOT_DINAMICO * 256 + y * SPR_TAM + x), cor_arte_gatinho(ARTE_GATINHO_REAL[y][x]));
            if (x & 1) frame();
        }
        printf("\r      -> linha %2d/16 (%3d/256 pixels)", y + 1, (y + 1) * 16);
        fflush(stdout);
    }
    printf("\n");
    cmd_atsp(1, 0, 0);
    frames(2);
    esperar("WRSP 2/5 - Pintura lenta pixel a pixel",
            "O slot 18 virou o Gatinho Dourado de coleira vermelha nas 5 copias, enquanto 'GATiNHOS' no topo segue intacto.");

    for (c = 0; c < 6; c++) {
        for (y = 0; y < SPR_TAM; y++)
            for (x = 0; x < SPR_TAM; x++) cmd_wrsp((unsigned)(SLOT_DINAMICO * 256 + y * SPR_TAM + x), SOLIDAS[c]);
        frames(10);
    }
    esperar("WRSP 3/5 - Reescrita rapida do slot inteiro (6 cores x 256 wrsp)",
            "As 5 copias mudaram de cor em bloco 6 vezes (vermelho, verde, azul, amarelo, ciano, rosa), sem artefatos.");

    pintar_slot_arte(SLOT_DINAMICO);
    for (y = 6; y < 10; y++)
        for (x = 6; x < 10; x++) cmd_wrsp((unsigned)(SLOT_DINAMICO * 256 + y * SPR_TAM + x), COR_TRANSP);
    frames(2);
    esperar("WRSP 4/5 - Furo transparente (cor 0x00 gravada via WRSP)",
            "Gatinho restaurado com um buraco 4x4 no centro do rosto: o retangulo do palco aparece atras do furo.");

    {
        uint64_t t0 = agora_ns(), dt;
        pintar_slot_arte(SLOT_DINAMICO);
        dt = agora_ns() - t0;
        informar("restaurar o slot inteiro = 256 wrsp em %.3f ms (%.2f quadros; %.0f pixels/s)",
                 (double)dt / 1e6, (double)dt / (double)g_periodo_ns, 256.0 * 1e9 / (double)dt);
    }
    frames(2);
    esperar("WRSP 5/5 - Restauracao final",
            "Gatinho Dourado completo (sem furo) nas 5 copias.");
}

/* ---------------------------------------------------------------------------
 *  CENA 16: Poligonos individuais (ATPO/POSPO/TAMPO/CORPO), flip padrao CIMA
 * ------------------------------------------------------------------------- */
static void cena_16_poligonos(void)
{
    static const char *DESC[5] = {
        "Retangulo preenchido 120x96 (ancora = canto sup-esq)",
        "Quadrado preenchido (lado = largura = 120, altura ignorada)",
        "Triangulo equilatero apontando para CIMA (ancora no topo central)",
        "Triangulo isosceles apontando para CIMA (base estreita = L/2)",
        "Triangulo escaleno/retangulo: angulo reto no canto INFERIOR ESQUERDO"
    };
    static const unsigned PALETA[8] = { COR_VERMELHO, COR_LARANJA, COR_AMARELO, COR_VERDE, COR_CIANO, COR_AZUL, COR_ROSA_CHOQUE, COR_BRANCO };
    int tipo, i, t;
    char titulo[96];

    limpar_sprites_poligonos();
    resetar_janela();
    bg_xadrez(COR_CINZA_ESCURO, COR_AZUL_ESCURO, 32);
    frames(2);

    for (tipo = 0; tipo < 5; tipo++) {
        poligono_centro(0, tipo, FLIP_CIMA, 160, 120, 120, 96, COR_AMARELO);
        frames(2);
        snprintf(titulo, sizeof titulo, "POLIGONO 1/5 - Tipo %d (%s)", tipo, NOME_TIPO[tipo]);
        esperar(titulo, DESC[tipo]);
    }

    poligono_centro(0, POLY_RETANGULO, FLIP_CIMA, 160, 120, 120, 96, COR_VERMELHO);
    for (i = 0; i < 8; i++) { cmd_corpo(0, PALETA[i]); frames(20); }
    esperar("POLIGONO 2/5 - Troca dinamica de cor (CORPO)", "O retangulo trocou de cor 8 vezes passando pela paleta.");

    cmd_corpo(0, COR_TRANSP);
    frames(2);
    esperar("POLIGONO 3/5 - Cor 0x00 = transparente", "Retangulo invisivel (cor 0): so o xadrez aparece.");

    cmd_corpo(0, COR_VERDE);
    for (t = 10; t <= 190; t += 2) { cmd_tampo(0, (unsigned)(t * 3 / 2), (unsigned)(t / 2 + 20)); frame(); }
    for (t = 190; t >= 10; t -= 2) { cmd_tampo(0, (unsigned)(t * 3 / 2), (unsigned)(t / 2 + 20)); frame(); }
    esperar("POLIGONO 4/5 - Escalonamento em tempo real (TAMPO)", "Retangulo verde expandiu e contraiu suavemente a 60 FPS.");

    cmd_tampo(0, 64, 44);
    for (t = 0; t < esc(240); t++) {
        cmd_pospo(0, (unsigned)tri(t * 3, LARGURA - 64), (unsigned)tri(t * 2, ALTURA - 44));
        frame();
    }
    cmd_atpo(0, 0, POLY_RETANGULO, FLIP_CIMA);
    frames(2);
    esperar("POLIGONO 5/5 - Movimento (POSPO) e ocultacao (ATPO show=0)", "O retangulo percorreu a tela e foi ocultado ao final.");
}

/* ============================================================================
 *  11. CENAS do DRIVER NOVO: busy(), contador de instrucoes, mascaras de show
 * ========================================================================== */
static const int OPS_ROTATIVOS[12] = { OP_NOOP_RAPIDO, OP_BSCROLL, OP_BTILE, OP_POSSP, OP_ATSP, OP_WRSP,
                                       OP_RGSP, OP_ATPO, OP_POSPO, OP_TAMPO, OP_CORPO, OP_PRIOR };

static int popcount32(uint32_t v)
{
    int n = 0;
    while (v) { n += (int)(v & 1u); v >>= 1; }
    return n;
}

/* ---------------------------------------------------------------------------
 *  CENA: busy() - semantica, latencia e impacto no handshake de envio
 * ------------------------------------------------------------------------- */
static void cena_busy(void)
{
    int i, N, travado = 0, nao_bit = 0, rc, vistos = 0;
    uint64_t *vb, *vs, *vn;
    Estat eb, es, en;
    uint32_t fce, r = 0;
    uint64_t t_edge, tp, t17, b;
    int visto = 0, busy_cheia = -1;

    resetar_janela();
    garantir_periodo();
    limpar_sprites_poligonos();
    cmd_clear_status();
    uc_drenar(1000000000ULL);

    printf("      --- BUSY 1: com a UC ociosa busy() deve ser 0 e so retornar 0/1 ---\n");
    N = esc(5000);
    for (i = 0; i < N; i++) {
        int v = busy();
        if (v != 0) travado++;
        if (v < 0 || v > 1) nao_bit++;
    }
    checar(travado == 0, "busy()==0 em %d leituras com a UC ociosa (leituras com 1: %d)", N, travado);
    checar(nao_bit == 0, "busy() retorna apenas 0 ou 1 (ubfx de 1 bit) (violacoes: %d)", nao_bit);

    printf("      --- BUSY 2: latencia da leitura de busy() x status() (intercaladas) ---\n");
    N = esc(20000);
    vb = (uint64_t *)malloc((size_t)N * sizeof *vb);
    vs = (uint64_t *)malloc((size_t)N * sizeof *vs);
    for (i = 0; i < N; i++) {
        uint64_t t0 = agora_ns(), t1, t2;
        (void)busy();
        t1 = agora_ns();
        (void)raw_status();
        t2 = agora_ns();
        vb[i] = (t1 - t0 > g_overhead_timer_ns) ? t1 - t0 - g_overhead_timer_ns : 0;
        vs[i] = (t2 - t1 > g_overhead_timer_ns) ? t2 - t1 - g_overhead_timer_ns : 0;
    }
    eb = estat_calc(vb, (size_t)N);
    es = estat_calc(vs, (size_t)N);
    imprimir_estat_us("busy()   leitura MMIO (PIO_BUSY)", &eb);
    imprimir_estat_us("status() leitura MMIO (PIO_STATUS)", &es);
    informar("razao p50 busy/status = %.2f (ambos sao 1 leitura MMIO na LW bridge)",
             es.p50 ? (double)eb.p50 / (double)es.p50 : 0.0);
    free(vb); free(vs);

    printf("      --- BUSY 3: busy() logo apos o retorno de cada envio ---\n");
    N = esc(20000);
    for (i = 0; i < N; i++) {
        op_ck(OPS_ROTATIVOS[i % 12], (uint32_t)i);
        if (busy() == 1) vistos++;
    }
    informar("busy=1 visto logo apos o retorno do envio em %d de %d envios (0 = busy so pulsa DURANTE o handshake)", vistos, N);
    checar(uc_drenar(1000000000ULL) >= 0 && busy() == 0, "apos drenar a UC, busy()==0 (nunca fica preso em 1)");
    cmd_bscroll(0, 0);
    cmd_clear_status();

    printf("      --- BUSY 4: busy() com a FIFO CHEIA e a UC em vsync_wait ---\n");
    t_edge = sincronizar_inicio_de_quadro(&fce);
    espera_ativa_ns(2000000ULL);
    cmd_noop();
    tp = agora_ns();
    while (agora_ns() - tp < 2000000ULL) { if (raw_status() & ST_BIT_VSYNC_WAIT) { visto = 1; break; } }
    if (!visto) {
        checar(0, "UC nao entrou em vsync_wait");
    } else {
        for (i = 0; i < FIFO_PROFUNDIDADE; i++) cmd_noop_rapido();
        r = raw_status();
        busy_cheia = busy();
        if ((r >> ST_SHIFT_FC) == fce) {
            informar("FIFO cheia (full=%u wait=%u): busy()=%d  => %s",
                     r & 1u, (r >> 4) & 1u, busy_cheia,
                     busy_cheia ? "busy ACOMPANHA FIFO cheia (envio espera no laco de busy)"
                                : "busy NAO acompanha FIFO cheia (o envio bloqueia na espera de 'done')");
            checar((r & ST_BIT_FIFO_FULL) != 0, "FIFO ficou cheia apos 16 envios (full=%u)", r & 1u);
        }
        t17 = agora_ns();
        cmd_noop_rapido();
        b = agora_ns() - t17;
        printf("      17a instrucao (com FIFO cheia) bloqueou %.3f ms (previsto pela fase: %.3f ms)\n",
               (double)b / 1e6, (double)(g_periodo_ns - (t17 - t_edge)) / 1e6);
        checar(b < g_periodo_ns + 1500000ULL, "envio com FIFO cheia termina em <= 1 periodo de VSYNC (%.3f ms) - nao estoura o TIMEOUT do driver", (double)b / 1e6);
    }
    checar(uc_drenar(200000000ULL) >= 0 && busy() == 0, "busy()==0 depois que a fila drenou");

    printf("      --- BUSY 5: custo do handshake em acessos MMIO ---\n");
    N = esc(10000);
    vn = (uint64_t *)malloc((size_t)N * sizeof *vn);
    for (i = 0; i < N; i++) {
        uint64_t t0 = agora_ns(), dt;
        rc = enviar_instrucao(1u << 5);
        dt = agora_ns() - t0;
        if (rc != 0) abortar_erro_hw("noop_rapido");
        g_total_enviadas++;
        vn[i] = (dt > g_overhead_timer_ns) ? dt - g_overhead_timer_ns : 0;
    }
    en = estat_calc(vn, (size_t)N);
    imprimir_estat_us("envio de 1 NOOP rapido", &en);
    informar("1 envio = %.1f leituras de status() (esperado >= 6 acessos: busy, escrita inst, valid=1, done alto, valid=0, done baixo)",
             es.p50 ? (double)en.p50 / (double)es.p50 : 0.0);
    checar(es.p50 == 0 || (double)en.p50 / (double)es.p50 >= 3.0, "envio custa varios acessos MMIO (>=3 leituras equivalentes)");
    free(vn);

    printf("      --- BUSY 6: busy() com o driver fechado ---\n");
    uc_drenar(1000000000ULL);
    gpu_close();
    rc = busy();
    checar(rc == -1, "busy() apos gpu_close() retorna -1 (rc=%d)", rc);
    rc = gpu_init();
    checar(rc == 0, "gpu_init() reabre (rc=%d)", rc);
    checar(busy() == 0, "busy()==0 apos reabrir");

    esperar("BUSY 1/1 - Semantica, latencia e custo do handshake com busy",
            "Sem mudanca visual; veja as medidas de busy() e o comportamento com FIFO cheia.");
}

/* ---------------------------------------------------------------------------
 *  CENA: contador de instrucoes (obter_e_zerar_instrucoes)
 * ------------------------------------------------------------------------- */
static void cena_contador(void)
{
    static const int OPS[] = { OP_NOOP_RAPIDO, OP_BSCROLL, OP_BTILE, OP_POSSP, OP_ATSP, OP_WRSP, OP_RGSP,
                               OP_ATPO, OP_POSPO, OP_TAMPO, OP_CORPO, OP_PRIOR, OP_CLST, OP_RAW_INV };
    unsigned drv;
    unsigned long meu;
    int i, j, op, falhas, F = esc(60), rc;
    uint64_t t0, dt;

    resetar_janela();
    garantir_periodo();
    limpar_sprites_poligonos();
    cmd_clear_status();
    uc_drenar(1000000000ULL);
    ler_zerar_contador(&drv, &meu);

    printf("      --- CONTADOR 1: leitura destrutiva ---\n");
    for (i = 0; i < 25; i++) op_ck(OP_NOOP_RAPIDO, (uint32_t)i);
    ler_zerar_contador(&drv, &meu);
    checar(drv == 25, "25 envios -> contador = 25 (lido %u)", drv);
    drv = obter_e_zerar_instrucoes();
    checar(drv == 0, "segunda leitura imediata = 0 (a leitura zera) (lido %u)", drv);

    printf("      --- CONTADOR 2: exatidao por opcode (37 + indice envios cada) ---\n");
    for (op = 0; op < (int)(sizeof OPS / sizeof OPS[0]); op++) {
        int n = 37 + op;
        for (i = 0; i < n; i++) op_ck(OPS[op], (uint32_t)i);
        ler_zerar_contador(&drv, &meu);
        checar(drv == (unsigned)n, "%-13s: %d envios -> contador %u", NOMES_OP[OPS[op]], n, drv);
    }
    cmd_clear_status();
    ler_zerar_contador(&drv, &meu);
    checar(drv == 1, "clear_status() conta 1 instrucao (lido %u)", drv);
    for (i = 0; i < 3; i++) cmd_noop();
    uc_drenar(1000000000ULL);
    ler_zerar_contador(&drv, &meu);
    checar(drv == 3, "3 NOOPs (vsync) -> contador 3 (conta no ACEITE pela FIFO, nao na execucao) (lido %u)", drv);

    printf("      --- CONTADOR 3: envios que FALHAM nao sao contados ---\n");
    uc_drenar(1000000000ULL);
    ler_zerar_contador(&drv, &meu);
    gpu_close();
    {
        int r1 = enviar_instrucao(1u << 5), r2 = noop(), r3 = bscroll(1, 1), r4 = clear_status();
        drv = obter_e_zerar_instrucoes();
        checar(r1 == -1 && r2 == -1 && r3 == -1 && r4 == -1, "4 envios com driver fechado retornam -1 (%d %d %d %d)", r1, r2, r3, r4);
        checar(drv == 0, "...e o contador nao muda (lido %u)", drv);
    }
    {
        /* quirk documentado pelo asm: a mascara muda ANTES do envio, mesmo quando o envio falha */
        unsigned antes;
        gpu_init();
        cmd_atsp(5, 0, 0);                    /* garante o bit 5 desligado antes do teste */
        gpu_close();
        antes = obter_mascara_sprites_ativos();
        modelo_mascara_atsp(5, 1);
        rc = atsp(5, 0, 1);
        informar("atsp(5,0,1) com driver fechado: rc=%d | mascara 0x%08X -> 0x%08X (%s)", rc, antes, obter_mascara_sprites_ativos(),
                 obter_mascara_sprites_ativos() != antes ? "a mascara MUDA mesmo com envio falho" : "mascara intacta");
    }
    rc = gpu_init();
    checar(rc == 0, "gpu_init() reabre (rc=%d)", rc);
    cmd_atsp(5, 0, 0);
    ler_zerar_contador(&drv, &meu);

    printf("      --- CONTADOR 4: o contador sobrevive a gpu_close()/gpu_init() ---\n");
    for (i = 0; i < 5; i++) op_ck(OP_NOOP_RAPIDO, (uint32_t)i);
    uc_drenar(1000000000ULL);
    gpu_close();
    rc = gpu_init();
    drv = obter_e_zerar_instrucoes();
    g_ctr_ref = g_total_enviadas;
    checar(rc == 0 && drv == 5, "5 envios, close, init -> contador continua 5 (lido %u, init rc=%d)", drv, rc);

    printf("      --- CONTADOR 5: contagem de uma rajada grande e vazao medida pelo contador ---\n");
    {
        int B = esc(100000);
        t0 = agora_ns();
        for (i = 0; i < B; i++) if (btile((unsigned)(i % N_PIXELS_BG), (unsigned)(i & 255)) != 0) abortar_erro_hw("btile");
        dt = agora_ns() - t0;
        g_total_enviadas += (unsigned long)B;
        ler_zerar_contador(&drv, &meu);
        checar(drv == (unsigned)B, "rajada de %d BTILE -> contador %u", B, drv);
        printf("      vazao medida: %.0f instr/s (%u instrucoes em %.2f ms)\n", (double)drv * 1e9 / (double)dt, drv, (double)dt / 1e6);
    }

    printf("      --- CONTADOR 6: instrucoes POR QUADRO, lidas do contador a cada quadro ---\n");
    poligono_centro(0, POLY_EQUILATERO, FLIP_CIMA, 160, 120, 40, 36, COR_AMARELO);
    ler_zerar_contador(&drv, &meu);
    falhas = 0;
    {
        long soma = 0, mx = 0;
        for (j = 0; j < F; j++) {
            int k = 10 + (j * 7) % 90;
            for (i = 0; i < k; i++) cmd_pospo(0, (unsigned)tri(j * 5 + i, 280), (unsigned)tri(j * 3 + i, 200));
            frame();
            ler_zerar_contador(&drv, &meu);
            if (drv != (unsigned)(k + 1)) falhas++;
            soma += (long)drv;
            if ((long)drv > mx) mx = (long)drv;
        }
        printf("      instrucoes/quadro lidas do contador: media %.1f, max %ld\n", (double)soma / (double)F, mx);
    }
    checar(falhas == 0, "contador por quadro == k pospo + 1 noop em todos os %d quadros (divergencias: %d)", F, falhas);

    esperar("CONTADOR 1/1 - obter_e_zerar_instrucoes()",
            "Triangulo equilatero amarelo percorreu a tela com cargas variaveis (10..99 instrucoes por quadro).");
}

/* ---------------------------------------------------------------------------
 *  CENA: mascaras de sprites/poligonos ativos
 * ------------------------------------------------------------------------- */
static void esconder_ativos_sprites(void)
{
    uint32_t m = obter_mascara_sprites_ativos();
    int i;
    for (i = 0; i < N_SPRITES; i++) if (m & (1u << i)) cmd_atsp((unsigned)i, 0, 0);
}

static void esconder_ativos_poligonos(void)
{
    uint32_t m = obter_mascara_poligonos_ativos();
    int i;
    for (i = 0; i < N_POLIGONOS; i++) if (m & (1u << i)) cmd_atpo((unsigned)i, 0, 0, 0);
}

static void cena_mascaras(void)
{
    int i, falhas, N = esc(6000), pc, vis_sp = 0, vis_po = 0;
    unsigned drv, ms, mp;
    unsigned long meu;

    resetar_janela();
    limpar_sprites_poligonos();
    bg_cenario_noturno();
    cmd_clear_status();
    uc_drenar(1000000000ULL);

    printf("      --- MASCARAS 1: apos esconder tudo ---\n");
    ms = obter_mascara_sprites_ativos(); mp = obter_mascara_poligonos_ativos();
    checar(ms == 0 && mp == 0, "sprites=0x%08X poligonos=0x%04X (esperado 0)", ms, mp);

    printf("      --- MASCARAS 2: sprite a sprite (id 0..31) ---\n");
    falhas = 0;
    for (i = 0; i < N_SPRITES; i++) {
        cmd_atsp((unsigned)i, 0, 1);
        if (obter_mascara_sprites_ativos() != (1u << i)) falhas++;
        cmd_atsp((unsigned)i, 0, 0);
        if (obter_mascara_sprites_ativos() != 0) falhas++;
    }
    checar(falhas == 0, "mascara == 1<<id ao mostrar e == 0 ao esconder, para os 32 sprites (erros: %d)", falhas);

    printf("      --- MASCARAS 3: acumulando, padroes e limpeza ---\n");
    falhas = 0;
    for (i = 0; i < N_SPRITES; i++) {
        uint32_t esp = (i == 31) ? 0xFFFFFFFFu : ((1u << (i + 1)) - 1u);
        cmd_atsp((unsigned)i, 0, 1);
        if (obter_mascara_sprites_ativos() != esp) falhas++;
    }
    checar(falhas == 0 && obter_mascara_sprites_ativos() == 0xFFFFFFFFu, "32 sprites ligados em sequencia -> 0xFFFFFFFF (erros: %d)", falhas);
    for (i = 0; i < N_SPRITES; i += 2) cmd_atsp((unsigned)i, 0, 0);
    checar(obter_mascara_sprites_ativos() == 0xAAAAAAAAu, "ids pares desligados -> 0xAAAAAAAA (0x%08X)", obter_mascara_sprites_ativos());
    for (i = 1; i < N_SPRITES; i += 2) cmd_atsp((unsigned)i, 0, 0);
    checar(obter_mascara_sprites_ativos() == 0u, "ids impares desligados -> 0 (0x%08X)", obter_mascara_sprites_ativos());

    printf("      --- MASCARAS 4: flip nao altera a mascara; show usa so o bit 0 ---\n");
    for (i = 0; i < 4; i++) {
        cmd_atsp(7, (unsigned)i, 1);
        checar(obter_mascara_sprites_ativos() == (1u << 7), "atsp(7, flip=%d, show=1): bit 7 ligado e nenhum outro (0x%08X)", i, obter_mascara_sprites_ativos());
    }
    cmd_atsp(9, 0, 3);
    checar((obter_mascara_sprites_ativos() & (1u << 9)) != 0, "atsp(9,0,show=3): show&1=1 -> ligado");
    cmd_atsp(9, 0, 2);
    checar((obter_mascara_sprites_ativos() & (1u << 9)) == 0, "atsp(9,0,show=2): show&1=0 -> DESLIGA (so o bit 0 de show vale)");
    cmd_atsp(7, 0, 0);
    cmd_atsp(37, 0, 1);                       /* id 37 -> 37&31 = 5 */
    checar(obter_mascara_sprites_ativos() == (1u << 5), "atsp(id=37) e mascarado para id 5 (0x%08X)", obter_mascara_sprites_ativos());
    cmd_atsp(37, 0, 0);

    printf("      --- MASCARAS 5: poligonos (id 0..15), flip/tipo nao alteram ---\n");
    falhas = 0;
    for (i = 0; i < N_POLIGONOS; i++) {
        cmd_atpo((unsigned)i, 1, (unsigned)(i % 5), (unsigned)(i & 3));
        if (obter_mascara_poligonos_ativos() != (1u << i)) falhas++;
        cmd_atpo((unsigned)i, 0, (unsigned)(i % 5), (unsigned)((i + 1) & 3));
        if (obter_mascara_poligonos_ativos() != 0) falhas++;
    }
    checar(falhas == 0, "mascara de poligonos == 1<<id ao mostrar, 0 ao esconder, qualquer tipo/flip (erros: %d)", falhas);
    for (i = 0; i < N_POLIGONOS; i++) cmd_atpo((unsigned)i, 1, POLY_RETANGULO, FLIP_CIMA);
    checar(obter_mascara_poligonos_ativos() == 0xFFFFu, "16 poligonos ligados -> 0xFFFF (0x%08X; bits 16..31 sempre 0)", obter_mascara_poligonos_ativos());
    for (i = 0; i < N_POLIGONOS; i++) cmd_atpo((unsigned)i, 0, POLY_RETANGULO, FLIP_CIMA);
    cmd_atpo(17, 1, POLY_RETANGULO, FLIP_CIMA);   /* 17 & 15 = 1 */
    checar(obter_mascara_poligonos_ativos() == (1u << 1), "atpo(id=17) e mascarado para id 1 (0x%04X)", obter_mascara_poligonos_ativos());
    cmd_atpo(17, 0, POLY_RETANGULO, FLIP_CIMA);

    printf("      --- MASCARAS 6: quirk - tipo invalido (>4) marca ATIVO mas nada e desenhado ---\n");
    cmd_clear_status();
    cmd_atpo(3, 1, 5, FLIP_CIMA);
    informar("atpo(id3,show1,tipo5): mascara=0x%04X (bit 3 %s) | wrn_param=%u",
             obter_mascara_poligonos_ativos(), (obter_mascara_poligonos_ativos() & 8u) ? "LIGADO" : "desligado", ler_snap().wrn);
    verificar_flags("tipo>4: wrn_param levantada", 0, 1);
    informar("a mascara reflete o show PEDIDO; no hardware tipo 5..7 nao e rasterizado (poligono invisivel)");
    cmd_atpo(3, 0, 0, FLIP_CIMA);
    cmd_clear_status();
    verificar_flags("limpeza", 0, 0);

    printf("      --- MASCARAS 7: fuzz de %d operacoes (ids ate 63, show 0..3, flips) ---\n", N);
    falhas = 0;
    for (i = 0; i < N; i++) {
        if (rnd() & 1u) cmd_atsp(rnd() & 63u, rnd() & 3u, rnd() & 3u);
        else            cmd_atpo(rnd() & 31u, rnd() & 3u, rnd() % 5u, rnd() & 3u);
        if (obter_mascara_sprites_ativos() != g_mask_sp_mod ||
            obter_mascara_poligonos_ativos() != g_mask_po_mod ||
            (obter_mascara_poligonos_ativos() & 0xFFFF0000u)) falhas++;
    }
    checar(falhas == 0, "mascaras do driver == modelo apos cada uma das %d operacoes (divergencias: %d)", N, falhas);
    limpar_sprites_poligonos();
    checar(obter_mascara_sprites_ativos() == 0 && obter_mascara_poligonos_ativos() == 0, "limpar tudo zera as duas mascaras");

    printf("      --- MASCARAS 8: esconder SO os ativos (economiza instrucoes) ---\n");
    for (i = 0; i < N_SPRITES; i++) sprite_cfg(i, i, 20 + (i % 8) * 36, 28 + (i / 8) * 40, 0, (i % 3 == 0));
    for (i = 0; i < N_POLIGONOS; i += 5) poligono_centro(i, 2 + (i % 3), (i / 5) & 3, 40 + i * 14, 205, 36, 30, COR_ROSA_CHOQUE);
    for (i = 0; i < N_SPRITES; i++) if (i % 3 == 0) vis_sp++;
    for (i = 0; i < N_POLIGONOS; i += 5) vis_po++;
    frames(2);
    ms = obter_mascara_sprites_ativos(); mp = obter_mascara_poligonos_ativos();
    checar(popcount32(ms) == vis_sp && popcount32(mp) == vis_po,
           "mascaras: %d sprites ativos (0x%08X) e %d poligonos ativos (0x%04X)", popcount32(ms), ms, popcount32(mp), mp);
    esperar("MASCARAS 1/2 - 11 sprites (ids multiplos de 3) e 4 triangulos visiveis",
            "11 sprites do .mif espalhados pela grade e 4 triangulos rosa na faixa inferior (flips diferentes).");

    ler_zerar_contador(&drv, &meu);
    pc = popcount32(obter_mascara_sprites_ativos()) + popcount32(obter_mascara_poligonos_ativos());
    esconder_ativos_sprites();
    esconder_ativos_poligonos();
    frames(2);
    ler_zerar_contador(&drv, &meu);
    checar(drv == (unsigned)(pc + 2), "esconder por mascara enviou %d atsp/atpo (+2 noops) = %u instrucoes, em vez de %d", pc, drv, N_SPRITES + N_POLIGONOS + 2);
    checar(obter_mascara_sprites_ativos() == 0 && obter_mascara_poligonos_ativos() == 0, "mascaras zeradas depois de esconder");
    esperar("MASCARAS 2/2 - Tudo escondido apenas pelos bits ativos",
            "Tela sem sprites nem poligonos; so o fundo noturno permanece.");
}

/* ============================================================================
 *  12. FLIP DOS POLIGONOS (atpo: 0=CIMA 1=BAIXO 2=DIREITA 3=ESQUERDA; tipos 2,3,4)
 * ========================================================================== */
static void cena_flip_poligonos(void)
{
    static const unsigned COR_FLIP[4] = { COR_VERMELHO, COR_VERDE, COR_AZUL_CEU, COR_AMARELO };
    static const int CX[4]    = { 40, 120, 200, 280 };
    static const int CY[3]    = { 50, 125, 200 };
    static const int TIPOS[3] = { POLY_EQUILATERO, POLY_ISOSCELES, POLY_ESCALENO };
    static const int LW[3]    = { 48, 72, 44 };
    static const int LH[3]    = { 42, 48, 44 };
    static const int ROT[4]   = { FLIP_CIMA, FLIP_DIREITA, FLIP_BAIXO, FLIP_ESQUERDA };   /* sentido horario */
    static const unsigned PAL[8] = { COR_VERMELHO, COR_LARANJA, COR_AMARELO, COR_VERDE, COR_CIANO, COR_AZUL_CEU, COR_ROSA_CHOQUE, COR_BRANCO };
    int r, c, i, t, N, x0, y0, x1, y1, ok, total;
    Pacing p;
    char titulo[96];

    limpar_sprites_poligonos();
    resetar_janela();
    garantir_periodo();

    /* ---- A: 3 tipos x 4 flips com guias da caixa envolvente ---- */
    bg_preencher(COR_CINZA_ESCURO);
    for (r = 0; r < 3; r++)
        for (c = 0; c < 4; c++) {
            poligono_bbox(TIPOS[r], CX[c], CY[r], LW[r], LH[r], &x0, &y0, &x1, &y1);
            bg_contorno(x0, y0, x1, y1, COR_CINZA);
            bg_cruz(CX[c], CY[r], 3, COR_BRANCO);
        }
    for (r = 0; r < 3; r++)
        for (c = 0; c < 4; c++) poligono_centro(r * 4 + c, TIPOS[r], c, CX[c], CY[r], LW[r], LH[r], COR_FLIP[c]);
    frames(2);
    checar(obter_mascara_poligonos_ativos() == 0x0FFFu, "12 poligonos ativos na mascara do driver (0x%04X)", obter_mascara_poligonos_ativos());
    esperar("FLIP 1/9 - Equilatero (linha 1), Isosceles (linha 2), Escaleno (linha 3) x flips 0..3",
            "Colunas = flip 0 CIMA (vermelho), 1 BAIXO (verde), 2 DIREITA (azul), 3 ESQUERDA (amarelo). Equilatero/Isosceles: apice aponta para CIMA, BAIXO, DIREITA, ESQUERDA, sempre dentro da caixa cinza. Escaleno (RTL atual): flip0 = angulo reto inf-esq, flip1 = sup-esq, flip3 = inf-dir; o flip2 usa os MESMOS 3 vertices do flip0 (angulo reto inf-esq) - veja se a coluna azul repete a vermelha.");

    /* ---- B: retangulo e quadrado ignoram o flip ---- */
    limpar_sprites_poligonos();
    bg_preencher(COR_CINZA_ESCURO);
    for (c = 0; c < 4; c++) {
        poligono_centro(c, POLY_RETANGULO, c, CX[c], 70, 56, 32, COR_FLIP[c]);
        poligono_centro(4 + c, POLY_QUADRADO, c, CX[c], 160, 44, 44, COR_FLIP[c]);
    }
    frames(2);
    esperar("FLIP 2/9 - Tipos 0 e 1 IGNORAM o flip",
            "Linha de cima: 4 retangulos 56x32 identicos (so a cor muda); linha de baixo: 4 quadrados 44x44 identicos.");

    /* ---- C: rotacao animada (muda a cada 20 quadros) ---- */
    limpar_sprites_poligonos();
    bg_xadrez(COR_AZUL_ESCURO, COR_CINZA_ESCURO, 32);
    poligono_centro(0, POLY_EQUILATERO, FLIP_CIMA, 160, 120, 100, 88, COR_AMARELO);
    poligono_centro(1, POLY_ISOSCELES,  FLIP_CIMA,  60, 120, 80, 80, COR_CIANO);
    poligono_centro(2, POLY_ESCALENO,   FLIP_CIMA, 260, 120, 70, 70, COR_ROSA_CHOQUE);
    N = esc(320);
    for (t = 0; t < N; t++) {
        if (t % 20 == 0) {
            int f = t / 20;
            cmd_atpo(0, 1, POLY_EQUILATERO, (unsigned)ROT[f & 3]);
            cmd_atpo(1, 1, POLY_ISOSCELES,  (unsigned)ROT[(f + 1) & 3]);
            cmd_atpo(2, 1, POLY_ESCALENO,   (unsigned)ROT[(f + 2) & 3]);
        }
        frame();
    }
    esperar("FLIP 3/9 - Rotacao animada (CIMA -> DIREITA -> BAIXO -> ESQUERDA a cada 20 quadros)",
            "Equilatero amarelo (centro), isosceles ciano (esq.) e escaleno rosa (dir.) giram em sentido horario, defasados. No escaleno, a passagem CIMA->DIREITA nao muda a forma (flip2 = flip0 no RTL atual).");

    /* ---- D: persistencia do flip ao mover/redimensionar/recolorir; ATPO e a unica instrucao que o altera ---- */
    limpar_sprites_poligonos();
    bg_xadrez(COR_CINZA_ESCURO, COR_AZUL_ESCURO, 16);
    poligono_centro(0, POLY_EQUILATERO, FLIP_ESQUERDA, 80, 120, 60, 52, COR_VERMELHO);
    poligono_centro(1, POLY_ISOSCELES,  FLIP_DIREITA, 240, 120, 80, 60, COR_VERDE);
    poligono_centro(2, POLY_ESCALENO,   FLIP_BAIXO,   160, 60, 50, 50, COR_AMARELO);
    N = esc(180);
    for (t = 0; t < N; t++) {
        int w0 = 50 + tri(t, 30);
        poligono_mover_centro(0, POLY_EQUILATERO, 70 + tri(t * 2, 60), 90 + tri(t, 60), w0, 52);
        cmd_tampo(0, (unsigned)w0, 52);
        cmd_corpo(1, PAL[(t / 15) & 7]);
        poligono_mover_centro(1, POLY_ISOSCELES, 240 - tri(t * 2, 50), 120 + tri(t * 3, 40) - 20, 80, 60);
        cmd_tampo(2, (unsigned)(40 + tri(t, 40)), (unsigned)(40 + tri(t * 2, 30)));
        frame();
    }
    esperar("FLIP 4/9 - Orientacao PERSISTE com POSPO/TAMPO/CORPO (sem novo ATPO)",
            "Apos 180 quadros movendo, redimensionando e recolorindo: equilatero vermelho continua apontando ESQUERDA, isosceles verde DIREITA, escaleno amarelo (angulo reto sup-esq = flip BAIXO).");

    cmd_atpo(0, 1, POLY_EQUILATERO, FLIP_CIMA);
    cmd_atpo(1, 1, POLY_ISOSCELES,  FLIP_ESQUERDA);
    cmd_atpo(2, 1, POLY_ESCALENO,   FLIP_DIREITA);
    frames(2);
    esperar("FLIP 5/9 - Novo ATPO reorienta sem alterar posicao/tamanho/cor",
            "Mesmos 3 poligonos, mesmas posicoes/cores, agora: equilatero para CIMA, isosceles para ESQUERDA, escaleno com flip DIREITA.");

    cmd_atpo(0, 0, POLY_EQUILATERO, FLIP_CIMA);
    frames(20);
    cmd_atpo(0, 1, POLY_EQUILATERO, FLIP_BAIXO);
    frames(2);
    esperar("FLIP 6/9 - Ocultar e reexibir com outro flip (ATPO show=0 -> show=1)",
            "O equilatero vermelho sumiu por 20 quadros e voltou apontando para BAIXO. ATENCAO: toda ATPO regrava flip; usar flip=0 so para alternar o show reorienta para CIMA.");

    /* ---- E: flip exaustivo de todos os tipos/ids com verificacao de flags ---- */
    limpar_sprites_poligonos();
    bg_preencher(COR_FUNDO_PRETO);
    ok = 0; total = 0;
    for (i = 0; i < esc(4000); i++) {
        int id = (int)(rnd() & 15u), tipo = (int)(rnd() % 5u), fl = (int)(rnd() & 3u);
        cmd_pospo((unsigned)id, rnd() % (LARGURA - 40), rnd() % (ALTURA - 40));
        cmd_tampo((unsigned)id, 8u + rnd() % 60u, 8u + rnd() % 50u);
        cmd_corpo((unsigned)id, 1u + rnd() % 255u);
        cmd_atpo((unsigned)id, 1, (unsigned)tipo, (unsigned)fl);
        total++;
        if ((i % 40) == 39) { frame(); if (flags_conferem(0, 0)) ok++; }
    }
    checar(ok == total / 40, "flags limpas a cada 40 combinacoes aleatorias tipo x flip x id (%d/%d)", ok, total / 40);
    esperar("FLIP 7/9 - 4000 combinacoes aleatorias de tipo x flip x id",
            "Caos de formas coloridas (retangulos, quadrados e triangulos em 4 orientacoes) sem wrn_param/err_opcode.");

    /* ---- F: troca de flip em alta taxa nos 16 poligonos (16 ATPO + 1 NOOP por quadro) ---- */
    limpar_sprites_poligonos();
    bg_xadrez(COR_AZUL_ESCURO, COR_PRETO_SOLIDO, 16);
    for (i = 0; i < N_POLIGONOS; i++)
        poligono_centro(i, 2 + (i % 3), 0, 40 + (i % 4) * 80, 30 + (i / 4) * 60, 46, 40, PAL[i & 7]);
    frames(2);
    N = esc(360);
    pacing_ini(&p, (size_t)N);
    {
        unsigned d0;
        unsigned long m0;
        ler_zerar_contador(&d0, &m0);                   /* zera o contador para medir instrucoes/quadro */
    }
    for (t = 0; t < N; t++) {
        for (i = 0; i < N_POLIGONOS; i++) cmd_atpo((unsigned)i, 1, (unsigned)(2 + (i % 3)), (unsigned)(((t / 6) + i) & 3));
        frame();
        pacing_marca(&p);
    }
    {
        unsigned drv;
        unsigned long meu;
        ler_zerar_contador(&drv, &meu);
        checar(drv == (unsigned long)N * 17u, "contador do driver: %d quadros x (16 ATPO + 1 NOOP) = %lu (lido %u)", N, (unsigned long)N * 17u, drv);
    }
    pacing_relatorio(&p, "16 ATPO por quadro", 24, "pacing_flip16", 1);
    {
        Acum *a = &g_op[OP_ATPO];
        printf("      ATPO acumulado: %lu chamadas | media %.2f us | min %.2f | max %.2f us\n", a->n,
               a->n ? (double)a->soma / (double)a->n / 1e3 : 0.0, (double)a->min / 1e3, (double)a->max / 1e3);
    }
    esperar("FLIP 8/9 - 16 poligonos trocando de flip a cada 6 quadros (16 ATPO + NOOP por quadro)",
            "Ondas de triangulos girando em 4 orientacoes (tipos 2,3,4 alternados) em grade 4x4, a 60 FPS.");

    /* ---- G: flip x PRIOR x sprites ---- */
    limpar_sprites_poligonos();
    bg_gradiente();
    sprite_cfg(0, SLOT_GATO_A, 124, 104, 0, 1);
    sprite_cfg(1, SLOT_GATO_B, 180, 120, 1, 1);
    for (c = 0; c < 4; c++) poligono_centro(c, POLY_EQUILATERO, c, 80 + c * 55, 120, 60, 52, COR_FLIP[c]);
    cmd_prior(0);
    frames(2);
    esperar("FLIP 9/9a - PRIOR=0 (sprites acima) com 4 triangulos em 4 flips",
            "Os 2 gatinhos aparecem inteiros por cima dos 4 triangulos (CIMA, BAIXO, DIREITA, ESQUERDA).");
    cmd_prior(1);
    frames(2);
    snprintf(titulo, sizeof titulo, "FLIP 9/9b - PRIOR=1 (poligonos acima)");
    esperar(titulo, "Os triangulos cobrem os gatinhos nas areas de intersecao, em todas as orientacoes.");
}

/* ============================================================================
 *  13. CENAS restantes: 16 poligonos, PRIOR, geometria limite, integracao, estresse
 * ========================================================================== */
static const unsigned CORES_16[16] = {
    COR_VERMELHO, COR_VERDE, COR_AZUL, COR_AMARELO, COR_CIANO, COR_ROSA_CHOQUE, COR_LARANJA, COR_BRANCO,
    0xE8, 0x3C, 0x6F, 0xB4, 0x5B, 0xD2, 0x27, 0x93
};

static void cena_poligonos_16(void)
{
    int i, t;

    limpar_sprites_poligonos();
    resetar_janela();
    bg_xadrez(COR_CINZA_ESCURO, COR_AZUL_ESCURO, 16);

    for (i = 0; i < N_POLIGONOS; i++)
        poligono_centro(i, i % 5, (i / 4) & 3, 40 + (i % 4) * 80, 30 + (i / 4) * 60, 54, 42, CORES_16[i]);
    frames(2);
    checar(obter_mascara_poligonos_ativos() == 0xFFFFu, "mascara do driver = 0xFFFF com os 16 poligonos ativos (0x%04X)", obter_mascara_poligonos_ativos());
    esperar("16 POLIGONOS 1/4 - Grade 4x4 com os 16 registradores",
            "16 poligonos (ids 0..15) alternando os 5 tipos; o flip muda por LINHA (0 CIMA, 1 BAIXO, 2 DIREITA, 3 ESQUERDA) e so afeta os triangulos.");

    for (t = 0; t < esc(300); t++) {
        for (i = 0; i < N_POLIGONOS; i++) {
            int cx = 160 + isen(t + i * 4) * 110 / 127 * (1 + (i & 1)) / 2 + icos(i * 4) * 36 / 127;
            int cy = 120 + icos(t + i * 4) * 80 / 127;
            poligono_mover_centro(i, i % 5, cx, cy, 54, 42);
        }
        frame();
    }
    esperar("16 POLIGONOS 2/4 - Orbita simultanea (16 POSPO + NOOP por quadro)",
            "As 16 formas orbitam sem atraso; os triangulos mantem a orientacao do flip ao se mover.");

    for (i = 0; i < N_POLIGONOS; i++)
        poligono_centro(i, POLY_RETANGULO, FLIP_CIMA, 160 - i * 4, 120 - i * 3, 150 - i * 6, 120 - i * 5, CORES_16[i]);
    frames(2);
    esperar("16 POLIGONOS 3/4 - Prioridade interna (poligono 0 > ... > 15)",
            "Pilha concentrica de retangulos: o 0 (vermelho) fica por cima, o 15 por baixo.");

    for (i = 0; i < N_POLIGONOS; i++)
        poligono_centro(i, POLY_EQUILATERO, i & 3, 160, 120, 210 - i * 11, 180 - i * 9, CORES_16[i]);
    frames(2);
    esperar("16 POLIGONOS 4/4 - Pilha de 16 triangulos equilateros com flips alternados",
            "Estrela/ampulheta concentrica: triangulos em 4 orientacoes (CIMA, BAIXO, DIREITA, ESQUERDA) empilhados, o 0 por cima.");
}

static void cena_prior(void)
{
    int i;

    limpar_sprites_poligonos();
    resetar_janela();
    bg_gradiente();
    sprite_cfg(0, SLOT_GATO_A,   124, 104, 0, 1);
    sprite_cfg(1, SLOT_DINAMICO, 152, 112, 0, 1);
    sprite_cfg(2, SLOT_GATO_B,   180, 120, 1, 1);
    sprite_cfg(3, SLOT_FOGO,     152,  92, 0, 1);
    poligono_cfg(0, POLY_RETANGULO,  FLIP_CIMA,     96,  88, 128, 64, COR_BRANCO,   1);
    poligono_cfg(1, POLY_EQUILATERO, FLIP_CIMA,    160,  76, 110, 88, COR_VERMELHO, 1);
    poligono_cfg(2, POLY_ESCALENO,   FLIP_ESQUERDA, 200,  90,  70, 70, COR_AZUL_CEU, 1);

    cmd_prior(0);
    frames(2);
    esperar("PRIOR 1/3 - Prioridade 0 (sprites ACIMA dos poligonos)",
            "Gatinhos e foguinho INTEIROS por cima do retangulo branco, do triangulo vermelho e do escaleno azul (flip ESQUERDA).");

    cmd_prior(1);
    frames(2);
    esperar("PRIOR 2/3 - Prioridade 1 (poligonos ACIMA dos sprites)",
            "Retangulo, triangulo vermelho e escaleno azul cobrem os gatinhos nas areas de intersecao.");

    for (i = 0; i < 12; i++) { cmd_prior((unsigned)(i & 1)); frames(20); }
    esperar("PRIOR 3/3 - Comutacao dinamica (12 trocas, 1 a cada 20 quadros)",
            "A ordem sprites x poligonos alternou 12 vezes em tempo real, sem piscar o fundo.");
}

/* ---------------------------------------------------------------------------
 *  CENA: geometria nos limites (recorte, tamanhos minimo/maximo, casos degenerados)
 * ------------------------------------------------------------------------- */
static void cena_geometria_limite(void)
{
    static const unsigned COR_FLIP[4] = { COR_VERMELHO, COR_VERDE, COR_AZUL_CEU, COR_AMARELO };
    int i, c, tipo;

    limpar_sprites_poligonos();
    resetar_janela();
    bg_xadrez(COR_CINZA_ESCURO, COR_AZUL_ESCURO, 20);
    bg_contorno(0, 0, LARGURA - 1, ALTURA - 1, COR_BRANCO);

    /* A: recorte nas bordas (coordenadas negativas/estouro nao podem dar wrap) */
    poligono_cfg(0, POLY_RETANGULO,  FLIP_CIMA, 290, 200, 60, 60, COR_VERMELHO, 1);
    poligono_cfg(1, POLY_QUADRADO,   FLIP_CIMA, 270, 10,  80, 0,  COR_VERDE, 1);
    poligono_cfg(2, POLY_EQUILATERO, FLIP_CIMA, 310, 100, 60, 50, COR_AMARELO, 1);
    poligono_cfg(3, POLY_ISOSCELES,  FLIP_BAIXO, 10, 100, 60, 50, COR_CIANO, 1);
    poligono_cfg(4, POLY_ESCALENO,   FLIP_CIMA,   0, 200, 60, 60, COR_ROSA_CHOQUE, 1);
    poligono_cfg(5, POLY_EQUILATERO, FLIP_DIREITA, 5, 20, 60, 50, COR_LARANJA, 1);
    poligono_cfg(6, POLY_ESCALENO,   FLIP_ESQUERDA, 319, 239, 40, 40, COR_BRANCO, 1);
    frames(2);
    esperar("GEOMETRIA 1/8 - Recorte nas bordas (ancoras em x=0..319, y=0..239 com formas que saem da tela)",
            "Formas cortadas pelas bordas direita/inferior/esquerda; NADA reaparece do lado oposto (sem wrap). Canto inf-dir (319,239) mostra no maximo 1 pixel branco.");

    /* B: tamanhos pequenos (4, 6, 8 px) para os 5 tipos - abaixo de 4 px o isosceles degenera (w>>2 = 0) */
    limpar_sprites_poligonos();
    for (tipo = 0; tipo < 5; tipo++)
        for (c = 0; c < 3; c++)
            poligono_cfg(tipo * 3 + c, tipo, (tipo + c) & 3, 70 + c * 90, 30 + tipo * 45, 4 + 2 * c, 4 + 2 * c, COR_FLIP[(tipo + c) & 3], 1);
    frames(2);
    esperar("GEOMETRIA 2/8 - Tamanhos pequenos (4, 6 e 8 px) nos 5 tipos",
            "5 linhas (retangulo, quadrado, equilatero, isosceles, escaleno) x 3 colunas de formas minusculas 4/6/8 px, com flips diferentes. Nenhuma deve preencher a tela.");

    /* C: tamanho maximo 319x239 para cada tipo */
    limpar_sprites_poligonos();
    poligono_cfg(0, POLY_RETANGULO, FLIP_CIMA, 0, 0, 319, 239, COR_AZUL_ESCURO, 1);
    frames(2);
    esperar("GEOMETRIA 3/8 - Retangulo 319x239 (tamanho maximo valido do TAMPO)",
            "Retangulo azul escuro cobrindo praticamente a tela inteira, sem wrn_param.");
    cmd_atpo(0, 0, POLY_RETANGULO, FLIP_CIMA);
    for (c = 0; c < 4; c++) {
        poligono_cfg(1, POLY_EQUILATERO, c, 160, 0, 319, 239, COR_FLIP[c], 1);
        frames(30);
    }
    esperar("GEOMETRIA 4/8 - Equilatero 319x239 nos 4 flips (30 quadros cada)",
            "Triangulo gigante ocupando a tela: CIMA (vermelho), BAIXO (verde), DIREITA (azul), ESQUERDA (amarelo).");
    cmd_atpo(1, 0, POLY_EQUILATERO, FLIP_CIMA);
    for (c = 0; c < 4; c++) {
        poligono_cfg(2, POLY_ESCALENO, c, 0, 0, 319, 239, COR_FLIP[c], 1);
        frames(30);
    }
    esperar("GEOMETRIA 5/8 - Escaleno 319x239 nos 4 flips (30 quadros cada)",
            "Triangulo retangulo gigante: flip0 inf-esq, flip1 sup-esq, flip2 (= flip0 no RTL atual), flip3 inf-dir.");
    cmd_atpo(2, 0, POLY_ESCALENO, FLIP_CIMA);

    /* D: degenerados - EXPLORATORIO (previsao pela leitura do rasterizador) */
    limpar_sprites_poligonos();
    poligono_cfg(0, POLY_EQUILATERO, FLIP_CIMA, 160, 100, 0, 60, COR_AMARELO, 1);
    frames(2);
    esperar("GEOMETRIA 6/8 [exploratorio] - Equilatero com LARGURA 0 e altura 60",
            "Pela equacao de borda do rasterizador, area zero => reta: esperada uma LINHA VERTICAL em x=160 (possivelmente de tela inteira). O mesmo vale para equilatero com largura 1 e isosceles com largura < 4 (w>>1 / w>>2 = 0). Informativo: reporte o que viu.");
    poligono_cfg(0, POLY_EQUILATERO, FLIP_CIMA, 160, 100, 60, 0, COR_AMARELO, 1);
    frames(2);
    esperar("GEOMETRIA 7/8 [exploratorio] - Equilatero com largura 60 e ALTURA 0",
            "Area zero => reta: esperada uma LINHA HORIZONTAL em y=100 (possivelmente de tela inteira). Informativo.");
    poligono_cfg(0, POLY_EQUILATERO, FLIP_CIMA, 160, 100, 0, 0, COR_AMARELO, 1);
    frames(2);
    esperar("GEOMETRIA 8/8 [exploratorio] - Equilatero 0x0 (3 vertices iguais)",
            "e1=e2=e3=0 em todo pixel => a regra 'todos <=0' e satisfeita: possivel PREENCHIMENTO DA TELA INTEIRA com amarelo. Informativo: se acontecer, evite TAMPO(0,0) em triangulos.");
    cmd_atpo(0, 0, POLY_EQUILATERO, FLIP_CIMA);
    for (i = 0; i < 2; i++) frame();
}

/* ---------------------------------------------------------------------------
 *  CENA: Tudo junto - 600 quadros com todos os motores, flips e telemetria
 * ------------------------------------------------------------------------- */
static void cena_tudo_junto(void)
{
    const int N_FRAMES = esc(600);
    int i, t;
    Pacing p;

    limpar_sprites_poligonos();
    resetar_janela();
    garantir_periodo();
    bg_cenario_noturno();
    bg_moldura(COR_BRANCO, 2);
    for (i = 0; i < N_SPRITES; i++) sprite_cfg(i, i, 152, 112, i & 1, 1);
    for (i = 0; i < N_POLIGONOS; i++) poligono_centro(i, i % 5, (i / 5) & 3, 160, 120, 48, 38, CORES_16[i]);
    frames(2);
    esperar("TUDO JUNTO 1/2 - Cenario preparado",
            "Fundo noturno com moldura, 32 sprites e 16 poligonos empilhados no centro.");

    printf("      Executando %d quadros: bscroll + 16 pospo + 32 possp (+16 atpo/30 quadros, +prior/120) por quadro...\n", N_FRAMES);
    pacing_ini(&p, (size_t)N_FRAMES);
    for (t = 0; t < N_FRAMES; t++) {
        cmd_bscroll((unsigned)((t / 4) % TILES_X), (unsigned)((t / 8) % TILES_Y));
        for (i = 0; i < N_POLIGONOS; i++)
            poligono_mover_centro(i, i % 5, 160 + isen(t + i * 4) * 104 / 127, 120 + icos(t * 2 + i * 4) * 72 / 127, 48, 38);
        for (i = 0; i < N_SPRITES; i++)
            sprite_pos(i, 152 + isen(t * (1 + (i % 3)) + i * 2) * 140 / 127, 112 + icos(t * (1 + (i % 2)) + i * 3) * 100 / 127);
        if (t % 30 == 0)
            for (i = 0; i < N_POLIGONOS; i++) cmd_atpo((unsigned)i, 1, (unsigned)(i % 5), (unsigned)(((t / 30) + i) & 3));
        if (t % 120 == 0) cmd_prior((unsigned)((t / 120) & 1));
        frame();
        pacing_marca(&p);
        if ((t + 1) % 150 == 0 && (t + 1) < N_FRAMES) {
            char sub[64];
            snprintf(sub, sizeof sub, "Parcial Tudo Junto (%d/%d quadros)", t + 1, N_FRAMES);
            relatorio_parcial(sub);
        }
    }
    pacing_relatorio(&p, "tudo junto", 24, "pacing_tudo_junto", 1);
    esperar("TUDO JUNTO 2/2 - Quadros concluidos",
            "Fundo rolando, 16 poligonos orbitando e trocando de flip, 32 sprites em movimento e PRIOR alternando, a 60 FPS, com err=0/wrn=0.");
}

/* ---------------------------------------------------------------------------
 *  CENA: Estresse - rajadas aleatorias, backpressure misto e teste de integridade
 * ------------------------------------------------------------------------- */
static void estresse_instr_valida(int permitir_clst)
{
    switch (rnd() % (permitir_clst ? 13u : 12u)) {
    case 0:  cmd_bscroll(rnd() % TILES_X, rnd() % TILES_Y); break;
    case 1:  cmd_btile(rnd() % N_PIXELS_BG, rnd() & 0xFFu); break;
    case 2:  cmd_possp(rnd() % N_SPRITES, rnd() % LARGURA, rnd() % ALTURA); break;
    case 3:  cmd_atsp(rnd() % N_SPRITES, rnd() & 3u, rnd() & 1u); break;
    case 4:  cmd_wrsp((18u + (rnd() & 1u)) * 256u + (rnd() & 0xFFu), rnd() & 0xFFu); break;
    case 5:  cmd_rgsp(rnd() % N_SPRITES, rnd() % N_SPRITE_RAM); break;
    case 6:  cmd_atpo(rnd() % N_POLIGONOS, rnd() & 1u, rnd() % 5u, rnd() & 3u); break;
    case 7:  cmd_pospo(rnd() % N_POLIGONOS, rnd() % LARGURA, rnd() % ALTURA); break;
    case 8:  cmd_tampo(rnd() % N_POLIGONOS, 8u + (rnd() % 110u), 8u + (rnd() % 90u)); break;
    case 9:  cmd_corpo(rnd() % N_POLIGONOS, rnd() & 0xFFu); break;
    case 10: cmd_prior(rnd() & 1u); break;
    case 11: cmd_noop_rapido(); break;
    default: cmd_clear_status(); break;
    }
}

static void cena_estresse(void)
{
    int i, n, rep, R, ok, L;
    int N = esc(200000);

    limpar_sprites_poligonos();
    resetar_janela();
    garantir_periodo();

    printf("      --- ESTRESSE 1: %d instrucoes aleatorias validas em rajada (sem NOOP) ---\n", N);
    for (i = 0; i < N; i++) {
        if (g_sair) sair_limpo(130);
        estresse_instr_valida(1);
        if ((i + 1) % 50000 == 0 && (i + 1) < N) {
            char s[64];
            snprintf(s, sizeof s, "Estresse lote %d/%d", i + 1, N);
            relatorio_parcial(s);
        }
    }
    frames(2);
    verificar_flags("pos-estresse: err=0 e wrn=0", 0, 0);
    esperar("ESTRESSE 1/4 - Rajada maxima de instrucoes validas",
            "Caos colorido (esperado). O hardware absorveu a rajada sem timeout, err_opcode=0 e wrn_param=0.");

    printf("      --- ESTRESSE 2: rajadas com NOOP-VSYNC intercalado (backpressure) ---\n");
    {
        uint32_t fc0, fc1;
        int nn = 0, alvo = esc(6000);
        (void)sincronizar_inicio_de_quadro(&fc0);
        for (i = 0, n = 0; n < alvo; ) {
            int bloco = 48 + (int)(rnd() % 100u);
            for (i = 0; i < bloco; i++) estresse_instr_valida(1);
            n += bloco;
            cmd_noop();
            nn++;
        }
        uc_drenar(5000000000ULL);
        fc1 = raw_status() >> ST_SHIFT_FC;
        checar(((fc1 - fc0) & 0xFFFFu) >= (unsigned)nn && ((fc1 - fc0) & 0xFFFFu) <= (unsigned)nn + 3u,
               "frame_counter avancou %u quadros para %d NOOPs (tolerancia +3 por ociosidade) - nenhum NOOP perdido",
               (fc1 - fc0) & 0xFFFFu, nn);
    }
    esperar("ESTRESSE 2/4 - Rajadas + NOOP-VSYNC com a FIFO saturando",
            "Caos colorido sincronizado ao VSYNC; sem travar, sem perder NOOPs.");

    printf("      --- ESTRESSE 3: integridade (instrucao 'veneno' no meio do fluxo nao pode se perder) ---\n");
    R = esc(60);
    L = 400;
    ok = 0;
    for (rep = 0; rep < R; rep++) {
        int pos = (int)(rnd() % (unsigned)L), pos2 = pos + 1 + (int)(rnd() % (unsigned)(L - pos));
        int com_noop = (rep % 3 == 2), exp_wrn;
        cmd_clear_status();
        for (i = 0; i < L; i++) {
            if (i == pos)  cmd_btile(76800u + (rnd() % 1000u), 0);            /* veneno: wrn_param */
            if (i == pos2) { if (rep & 1) cmd_clear_status(); }              /* em reps impares um CLST apaga o veneno */
            estresse_instr_valida(0);
            if (com_noop && (i % 130) == 129) cmd_noop();
        }
        exp_wrn = (rep & 1) ? ((pos2 < L) ? 0 : 1) : 1;
        if (flags_conferem(0, (unsigned)exp_wrn)) ok++;
        else printf("      rep %d: veneno em %d, CLST em %d, esperado wrn=%d\n", rep, pos, pos2, exp_wrn);
    }
    checar(ok == R, "integridade: veneno/CLST no meio de %d instrucoes (%d reps, %d com NOOPs) -> flags corretas em %d/%d",
           L, R, R / 3, ok, R);
    cmd_clear_status();
    limpar_sprites_poligonos();
    esperar("ESTRESSE 3/4 - Integridade de ordem e perda de instrucoes",
            "Caos visual; verificacoes de integridade acima.");

    printf("      --- ESTRESSE 3b: fronteira da FIFO (16/17): as ultimas instrucoes da rajada sao DECISIVAS ---\n");
    {
        static const char *TAIL[6] = { "WCI", "ICW", "CW", "CI", "W", "I" };
        int Lb, rb, Rb = esc(6), tot = 0, bons = 0;
        if (Rb < 2) Rb = 2;
        for (Lb = 14; Lb <= 22; Lb++)
            for (rb = 0; rb < Rb; rb++) {
                unsigned ee, ew;
                const char *tl = TAIL[(unsigned)(Lb + rb) % 6u];
                cmd_clear_status();
                uc_drenar(1000000000ULL);
                cmd_noop();                                  /* a UC passa a esperar o VSYNC; a FIFO enche */
                for (i = 0; i < Lb; i++) estresse_instr_valida(0);
                executar_sequencia(tl, &ee, &ew);
                tot++;
                if (flags_conferem(ee, ew)) bons++;
                else printf("      rajada L=%d + cauda \"%s\": esperado err=%u wrn=%u\n", Lb, tl, ee, ew);
            }
        checar(bons == tot, "fronteira da FIFO: rajadas de 14..22 fillers apos NOOP + cauda decisiva -> flags corretas em %d/%d", bons, tot);
        cmd_clear_status();
    }
    esperar("ESTRESSE 3b/4 - Fronteira da FIFO com cauda decisiva",
            "Caos visual; verificacao de que nenhuma instrucao da cauda foi perdida quando a FIFO satura.");

    printf("      --- ESTRESSE 4: reconciliacao final com o driver ---\n");
    frames(2);
    esperar("ESTRESSE 4/4 - Reconciliacao final",
            "Tela limpa de sprites/poligonos; contador e mascaras do driver conferem com o teste.");
}

/* ============================================================================
 *  14. Resumo final, menu de cenas e main
 * ========================================================================== */
typedef struct {
    const char *nome;
    void (*func)(void);
} Cena;

static const Cena CENAS[] = {
    { "Link MMIO: cronometro, status(), consistencia do driver, atpo(flip,id,show,tipo)", cena_01_link_mmio },
    { "busy(): semantica, latencia e custo do handshake (driver novo)",   cena_busy },
    { "Contador de instrucoes: obter_e_zerar_instrucoes() (driver novo)",  cena_contador },
    { "Mascaras de sprites/poligonos ativos (driver novo)",                cena_mascaras },
    { "VSYNC e frame_counter: periodo, jitter, FPS",                       cena_02_vsync_framecounter },
    { "NOOP: vsync_wait atomico, NOOP rapido, N noops = N quadros",        cena_03_noop_vsync_wait },
    { "FIFO de 16: full/empty, backpressure, adiantamento maximo",         cena_04_fifo },
    { "wrn_param: limites de todas as instrucoes + retencao + CLST",       cena_05_wrn_param },
    { "err_opcode: opcodes 13..31, validos, retencao, ordem na FIFO",      cena_06_err_opcode },
    { "Fuzz com scoreboard de flags (valida empacotamento do driver)",     cena_07_fuzz_flags },
    { "Latencia por opcode com percentis",                                 cena_08_latencia_opcodes },
    { "Vazao sustentada e orcamento de instrucoes por quadro",             cena_09_vazao_orcamento },
    { "Cadencia de quadros sob carga (jitter, adiantamento da fila)",      cena_10_pacing },
    { "BTILE por pixel: enderecamento, limites, descarte, procedural",     cena_11_btile_pixel },
    { "BSCROLL em tiles sobre o mapa por pixel + travamento no VSYNC",     cena_12_bscroll },
    { "Sprites individuais (RGSP/POSSP/ATSP) com o sprites.mif",           cena_13_sprites_mif },
    { "32 sprites simultaneos e remapeamento via RGSP",                    cena_14_sprites_32 },
    { "WRSP dinamico: pintura, reescrita rapida, transparencia",           cena_15_wrsp },
    { "Poligonos individuais (ATPO/POSPO/TAMPO/CORPO)",                    cena_16_poligonos },
    { "FLIP dos poligonos (tipos 2,3,4 x 4 flips, rotacao, persistencia)", cena_flip_poligonos },
    { "16 poligonos simultaneos (com flips)",                              cena_poligonos_16 },
    { "PRIOR: sprites x poligonos (inclui triangulo com flip)",            cena_prior },
    { "Geometria nos limites: recorte, tamanhos extremos, degenerados",    cena_geometria_limite },
    { "Tudo junto: 600 quadros com todos os motores e flips",              cena_tudo_junto },
    { "Estresse: rajadas, backpressure misto e integridade",               cena_estresse }
};
#define N_CENAS ((int)(sizeof CENAS / sizeof CENAS[0]))

static int g_cena_rodou[MAX_CENAS];
static int g_sel[MAX_CENAS];

static void imprimir_resumo_final(void)
{
    int i;
    unsigned long ok = 0, fa = 0, inf = 0;
    uint64_t tot_ns = 0;
    unsigned long tot_n = 0;
    unsigned drv_final = obter_e_zerar_instrucoes();

    printf("\n================================================================================\n");
    printf("  RELATORIO GLOBAL DE LATENCIA POR FUNCAO (todas as chamadas instrumentadas)\n");
    printf("================================================================================\n");
    printf("  %-13s | %10s | %10s | %9s | %9s | %9s | %9s | %10s\n", "Funcao", "Chamadas", "Total ms", "Min us", "Media us", "Desvio", "Max us", "Vazao/s");
    printf("  --------------+------------+------------+-----------+-----------+-----------+-----------+-----------\n");
    for (i = 0; i < N_OPS; i++) {
        const Acum *a = &g_op[i];
        if (a->n == 0) { printf("  %-13s | %10lu |          - |         - |         - |         - |         - |         -\n", NOMES_OP[i], 0UL); continue; }
        {
            double med = (double)a->soma / (double)a->n, var = a->soma_q / (double)a->n - med * med;
            printf("  %-13s | %10lu | %10.2f | %9.2f | %9.2f | %9.2f | %9.2f | %10.0f\n", NOMES_OP[i], a->n, (double)a->soma / 1e6,
                   (double)a->min / 1e3, med / 1e3, raiz(var > 0 ? var : 0) / 1e3, (double)a->max / 1e3, 1e9 / med);
        }
        if (i != OP_NOOP) { tot_ns += a->soma; tot_n += a->n; }
    }
    if (tot_n) printf("  TOTAL (sem NOOP-VSYNC): %lu instrucoes, media %.2f us (%.0f instr/s)\n", tot_n, (double)tot_ns / (double)tot_n / 1e3, (double)tot_n * 1e9 / (double)tot_ns);
    printf("  Total instrumentado (inclui NOOP-VSYNC): %lu envios sem timeout.\n", g_total_enviadas);
    imprimir_hist("instrucoes rapidas", g_hist_rap);
    imprimir_hist("NOOP-VSYNC (bloqueio no VSYNC)", g_hist_noop);

    printf("\n================================================================================\n");
    printf("  VEREDITOS POR CENA\n");
    printf("================================================================================\n");
    printf("  %-4s %-72s %6s %7s %6s\n", "Cena", "Descricao", "OK", "FALHAS", "INFO");
    for (i = 1; i <= N_CENAS; i++) {
        if (!g_cena_rodou[i]) continue;
        printf("  %-4d %-72.72s %6lu %7lu %6lu  %s\n", i, CENAS[i - 1].nome, g_ck_ok[i], g_ck_fail[i], g_ck_info[i],
               g_ck_fail[i] ? "<== FALHOU" : "");
        ok += g_ck_ok[i]; fa += g_ck_fail[i]; inf += g_ck_info[i];
    }
    printf("  ---------------------------------------------------------------------------------\n");
    printf("  TOTAL: %lu verificacoes OK | %lu FALHAS | %lu informacoes\n", ok, fa, inf);
    if (g_n_falhas) {
        printf("\n  Falhas registradas (ate 64):\n");
        for (i = 0; i < g_n_falhas; i++) printf("    - %s\n", g_falhas[i]);
    }
    {
        Snap s = ler_snap();
        printf("\n  Driver ao encerrar: contador residual=%u | mascara sprites=0x%08X | mascara poligonos=0x%04X\n",
               drv_final, obter_mascara_sprites_ativos(), obter_mascara_poligonos_ativos());
        printf("  PIO_STATUS final: 0x%08X (frame_counter=%u full=%u empty=%u err=%u wrn=%u wait=%u busy=%d)\n",
               s.raw, s.fc, s.full, s.empty, s.err, s.wrn, s.wait, s.busy);
    }
    printf("\n  VEREDITO FINAL: %s\n", fa ? "REPROVADO (veja as linhas [FALHA])" : "APROVADO");
    printf("================================================================================\n");
}

static int validar_arte_interna(void)
{
    int y;
    for (y = 0; y < 16; y++)
        if (strlen(ARTE_GATINHO_REAL[y]) != 16) {
            fprintf(stderr, "Erro interno: linha %d de ARTE_GATINHO_REAL nao tem 16 colunas.\n", y);
            return -1;
        }
    return 0;
}

static void mostrar_uso(const char *prog)
{
    int i;
    printf("Uso: %s [-t ms] [-s pct] [-c lista] [-o prefixo] [-Q] [-l]\n"
           "  -t ms      avanca sozinho a cada 'ms' ms (0 = sem pausa). Padrao: espera ENTER\n"
           "  -s pct     escala de amostras/quadros em %% (padrao 100; 20 = teste rapido)\n"
           "  -c lista   cenas a executar, ex.: 20  |  1-4,8  |  2,3,4,20  (padrao: todas)\n"
           "  -o prefixo exporta CSVs (prefixo_latencia_opcodes.csv, prefixo_pacing_*.csv, ...)\n"
           "  -Q         silencia as linhas [ OK ] (mostra so falhas)\n"
           "  -l         lista as cenas\n\nCenas (%d):\n", prog, N_CENAS);
    for (i = 0; i < N_CENAS; i++) printf("  %2d. %s\n", i + 1, CENAS[i].nome);
}

static int parse_lista_cenas(const char *s)
{
    const char *p = s;
    while (*p) {
        int a = 0, b;
        if (*p < '0' || *p > '9') return -1;
        while (*p >= '0' && *p <= '9') a = a * 10 + (*p++ - '0');
        b = a;
        if (*p == '-') {
            p++;
            if (*p < '0' || *p > '9') return -1;
            b = 0;
            while (*p >= '0' && *p <= '9') b = b * 10 + (*p++ - '0');
        }
        if (a < 1 || b > N_CENAS || a > b) return -1;
        while (a <= b) g_sel[a++] = 1;
        if (*p == ',') p++; else if (*p) return -1;
    }
    return 0;
}

int main(int argc, char **argv)
{
    int i, algum_sel = 0;

    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-t") && i + 1 < argc)      g_tempo_ms = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-s") && i + 1 < argc) { g_escala = atoi(argv[++i]); if (g_escala < 1) g_escala = 1; }
        else if (!strcmp(argv[i], "-c") && i + 1 < argc) {
            if (parse_lista_cenas(argv[++i]) != 0) { mostrar_uso(argv[0]); return 1; }
            algum_sel = 1;
        }
        else if (!strcmp(argv[i], "-o") && i + 1 < argc) g_csv_prefixo = argv[++i];
        else if (!strcmp(argv[i], "-Q"))                 g_verbose = 0;
        else if (!strcmp(argv[i], "-l")) { mostrar_uso(argv[0]); return 0; }
        else { mostrar_uso(argv[0]); return 1; }
    }
    if (validar_arte_interna() != 0) return 1;

    signal(SIGINT, on_sigint);
    if (gpu_init() != 0) {
        fprintf(stderr, "ERRO: gpu_init() falhou! Execute como root (sudo) para mapear /dev/mem.\n");
        return 1;
    }
    zerar_metricas();
    (void)obter_e_zerar_instrucoes();           /* zera o contador do driver antes de comecar */
    g_ctr_ref = g_total_enviadas;

    printf("================================================================================\n");
    printf("  COPROCESSADOR GRAFICO DE1-SoC - BATERIA COMPLETA (%d CENAS) - driver libAssembly_1.S\n", N_CENAS);
    printf("  ISA 0..12 + PIO_STATUS + busy() + contador + mascaras | FLIP de poligonos | Ctrl+C encerra\n");
    printf("  escala=%d%% | modo=%s | CSV=%s\n", g_escala, g_tempo_ms >= 0 ? "automatico" : "interativo (ENTER)",
           g_csv_prefixo ? g_csv_prefixo : "desligado");
    printf("================================================================================\n");

    resetar_janela();
    for (i = 0; i < N_CENAS; i++) {
        if (algum_sel && !g_sel[i + 1]) continue;
        g_cena_atual = i + 1;
        g_cena_rodou[i + 1] = 1;
        g_auto_flags = 1;
        printf("\n==================== CENA %02d/%02d: %s ====================\n", i + 1, N_CENAS, CENAS[i].nome);
        CENAS[i].func();
        printf("\n  >> Cena %02d: %lu OK | %lu FALHAS | %lu info\n", i + 1, g_ck_ok[i + 1], g_ck_fail[i + 1], g_ck_info[i + 1]);
    }

    printf("\nTodas as cenas solicitadas foram concluidas.\n");
    {
        unsigned long falhas = 0;
        int k;
        for (k = 1; k <= N_CENAS; k++) falhas += g_ck_fail[k];
        sair_limpo(falhas ? 2 : 0);
    }
    return 0;
}
