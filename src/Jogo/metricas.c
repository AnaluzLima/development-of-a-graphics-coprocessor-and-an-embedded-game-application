#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "jogo.h"

#define ARQUIVO_RANKING "rankintomb_of_the_mask.dat"
#define ARQUIVO_RANKINLEGADO "rankintombofthedesk.dat"
#define PONTUACAO_MAXIMA 99999999
#define INTERVALO_RELATORIO 1.0

RegistroRanking ranking[MAX_RANKING];

typedef struct {
    unsigned long frames;
    unsigned long frames_lentos;
    unsigned long inst_janela;
    int flabusy;
    int flafifo_cheia;
    int flafifo_vazia;
    int flaerro_gpu;
    int flaaviso_gpu;
    double soma_frame_ms;
    double maior_frame_ms;
    double menor_frame_ms;
    double inicio_janela;
    double cpu_inicio_janela;
} MetricasJanela;

static MetricasJanela metricas = {0};
static unsigned long total_frames_acumulados = 0;
static unsigned long total_inst_acumuladas = 0;

double obter_tempo_atual_segundos(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

static double obter_tempo_cpu_segundos(void) {
    struct timespec ts;
    clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

static int contar_bits_ativos(uint32_t mascara) {
    int c = 0;
    while (mascara) {
        c += (mascara & 1U);
        mascara >>= 1;
    }
    return c;
}

static void limpar_metricas_janela(double agora, double cpu_agora) {
    metricas.frames = 0;
    metricas.frames_lentos = 0;
    metricas.inst_janela = 0;
    metricas.flabusy = 0;
    metricas.flafifo_cheia = 0;
    metricas.flafifo_vazia = 0;
    metricas.flaerro_gpu = 0;
    metricas.flaaviso_gpu = 0;
    metricas.soma_frame_ms = 0.0;
    metricas.maior_frame_ms = 0.0;
    metricas.menor_frame_ms = 1e9;
    metricas.inicio_janela = agora;
    metricas.cpu_inicio_janela = cpu_agora;
}

static int rankinvalido(const RegistroRanking *r) {
    return r->valido == 1 && r->pontos_totais >= 0 &&
           r->pontos_totais <= PONTUACAO_MAXIMA &&
           r->skin_id >= 0 && r->skin_id < TOTAL_SKINS_GATO;
}

static void ordenar_ranking(void) {
    for (int i = 0; i < MAX_RANKING; i++) {
        if (!rankinvalido(&ranking[i])) {
            ranking[i].valido = 0;
            ranking[i].pontos_totais = 0;
            ranking[i].skin_id = SKIN_GATO_PRETO;
        }
    }
    for (int i = 0; i < MAX_RANKING - 1; i++) {
        for (int j = i + 1; j < MAX_RANKING; j++) {
            if (ranking[j].valido &&
                (!ranking[i].valido ||
                 ranking[j].pontos_totais > ranking[i].pontos_totais)) {
                RegistroRanking tmp = ranking[i];
                ranking[i] = ranking[j];
                ranking[j] = tmp;
            }
        }
    }
}

void carregar_ranking_arquivo(void) {
    for (int i = 0; i < MAX_RANKING; i++) {
        ranking[i].pontos_totais = 0;
        ranking[i].skin_id = SKIN_GATO_PRETO;
        ranking[i].valido = 0;
    }

    FILE *f = fopen(ARQUIVO_RANKING, "rb");
    if (!f) f = fopen(ARQUIVO_RANKINLEGADO, "rb");
    if (!f) return;

    RegistroRanking lidos[MAX_RANKING];
    size_t n = fread(lidos, sizeof(RegistroRanking), MAX_RANKING, f);
    fclose(f);
    for (size_t i = 0; i < n; i++) {
        if (rankinvalido(&lidos[i])) ranking[i] = lidos[i];
    }
    ordenar_ranking();
}

void salvar_rankinarquivo(void) {
    ordenar_ranking();
    FILE *f = fopen(ARQUIVO_RANKING, "wb");
    if (!f) {
        perror("Não foi possível salvar o ranking");
        return;
    }
    if (fwrite(ranking, sizeof(RegistroRanking), MAX_RANKING, f) != MAX_RANKING) {
        perror("Falha ao gravar o ranking");
    }
    fflush(f);
    fclose(f);
}

void registrar_no_ranking(int pontos_totais, int skin_id) {
    if (pontos_totais < 0) pontos_totais = 0;
    if (pontos_totais > PONTUACAO_MAXIMA) pontos_totais = PONTUACAO_MAXIMA;
    if (skin_id < 0 || skin_id >= TOTAL_SKINS_GATO) skin_id = SKIN_GATO_PRETO;

    ordenar_ranking();
    int pos = -1;
    for (int i = 0; i < MAX_RANKING; i++) {
        if (!ranking[i].valido || pontos_totais > ranking[i].pontos_totais) {
            pos = i;
            break;
        }
    }
    if (pos < 0) return;

    for (int i = MAX_RANKING - 1; i > pos; i--) ranking[i] = ranking[i - 1];
    ranking[pos].pontos_totais = pontos_totais;
    ranking[pos].skin_id = skin_id;
    ranking[pos].valido = 1;
    salvar_rankinarquivo();
}

void metricas_registrar_frame(double duracao_segundos) {
    double agora = obter_tempo_atual_segundos();
    double cpu_agora = obter_tempo_cpu_segundos();

    if (metricas.inicio_janela == 0.0) {
        limpar_metricas_janela(agora, cpu_agora);
    }

    if (duracao_segundos < 0.0) duracao_segundos = 0.0;
    double ms = duracao_segundos * 1000.0;

    uint32_t inst_frame = obter_e_zerar_instrucoes();

    metricas.frames++;
    total_frames_acumulados++;
    metricas.inst_janela += inst_frame;
    total_inst_acumuladas += inst_frame;
    metricas.soma_frame_ms += ms;

    if (ms > metricas.maior_frame_ms) metricas.maior_frame_ms = ms;
    if (ms < metricas.menor_frame_ms) metricas.menor_frame_ms = ms;
    if (ms > 16.67) metricas.frames_lentos++;

    metricas.flabusy |= (busy() != 0) ? 1 : 0;
    metricas.flafifo_vazia = (fifo_empty() != 0) ? 1 : 0;
    metricas.flafifo_cheia |= (fifo_full() != 0) ? 1 : 0;
    metricas.flaerro_gpu |= (error() != 0) ? 1 : 0;
    metricas.flaaviso_gpu |= (warning() != 0) ? 1 : 0;

    double janela = agora - metricas.inicio_janela;
    if (janela >= INTERVALO_RELATORIO) {
        double fps = (janela > 0.0) ? (double)metricas.frames / janela : 0.0;
        double media_ms = metricas.frames ? metricas.soma_frame_ms / (double)metricas.frames : 0.0;
        if (metricas.menor_frame_ms == 1e9) metricas.menor_frame_ms = 0.0;

        double cpu_usado = cpu_agora - metricas.cpu_inicio_janela;
        double uso_cpu_pct = (janela > 0.0) ? (cpu_usado / janela) * 100.0 : 0.0;
        if (uso_cpu_pct > 100.0) uso_cpu_pct = 100.0;

        double lat_cmd_us = (metricas.inst_janela > 0)
            ? (metricas.soma_frame_ms * 1000.0) / (double)metricas.inst_janela : 0.0;

        int sprites_ativos = contar_bits_ativos(obter_mascara_sprites_ativos());
        int poligonos_ativos = contar_bits_ativos(obter_mascara_poligonos_ativos() & 0xFFFFU);
        unsigned long inst_por_frame = metricas.frames
            ? metricas.inst_janela / metricas.frames : 0;

        fprintf(stdout,
            "[METRICAS] FPS=%.1f | CPU=%.1f%% | Quadro(ms): med=%.3f min=%.3f max=%.3f | "
            "Cmd=%.2f us | Inst/s=%lu (med/f=%lu) | Ativos: Sprites=%d/32 Poligonos=%d/16 | "
            "busy=%d fifo_full=%d fifo_empty=%d err=%d warn=%d\n",
            fps, uso_cpu_pct, media_ms, metricas.menor_frame_ms, metricas.maior_frame_ms,
            lat_cmd_us, metricas.inst_janela, inst_por_frame,
            sprites_ativos, poligonos_ativos,
            metricas.flabusy, metricas.flafifo_cheia, metricas.flafifo_vazia,
            metricas.flaerro_gpu, metricas.flaaviso_gpu);
        fflush(stdout);

        if (metricas.flaerro_gpu || metricas.flaaviso_gpu) {
            clear_status();
        }
        limpar_metricas_janela(agora, cpu_agora);
    }
}