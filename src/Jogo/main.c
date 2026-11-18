#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include "jogo.h"

volatile EstadoJogo estado_atual = ESTADO_TELA_INICIAL;

int main(void) {
    pthread_t th_teclado;

    //inicializa mapeamento de memoria da GPU, limpa status e carrega ranking salvo
    if (gpu_init() != 0) {
        fprintf(stderr, "ERRO: gpu_init() falhou! Execute com sudo.\n");
        return EXIT_FAILURE;
    }
    clear_status();
    carregar_ranking_arquivo();

    //inicia a thread paralela de leitura nao-bloqueante do teclado USB
    if (pthread_create(&th_teclado, NULL, thread_teclado_usb, NULL) != 0) {
        fprintf(stderr, "ERRO: Falha ao criar a thread do teclado USB.\n");
        gpu_close();
        return EXIT_FAILURE;
    }

    estado_atual = ESTADO_TELA_INICIAL;
    int contador_vitoria = 0; //indice da fase atual (0 = Fase 1, 1 = Fase 2, 2 = Fase 3)
    int fase_carregada = 0; //flag para carregar mapa e entidades apenas uma vez por tentativa
    int estado_iniciado = 0; //flag para executar o setup inicial de cada tela apenas uma vez
    int timer_frames = 0; //contador de frames para animacoes
    int opcao_pause = 2; //0 = Sair, 1 = Reiniciar, 2 = Play
    double t_inicio_fase = 0.0;
   
    EstadoTeclado botoes;

    //loop principal do jogo
    while (jogo_rodando) {
        double t_frame_inicio = obter_tempo_atual_segundos();

        //copia o estado mais recente das teclas capturadas pela thread
        ler_botoes_estado(&botoes);

        //TECLA P pra avançar para a próxima tela
        if (botoes.tecla_p) {
            limpar_direcoes_teclado();
            vel_tx = 0;
            vel_ty = 0;
            estado_iniciado = 0;
            fase_carregada = 0;

            switch (estado_atual) {
                case ESTADO_TELA_INICIAL:
                    estado_atual = ESTADO_MENU_INICIAL;
                    break;
                case ESTADO_MENU_INICIAL:
                    estado_atual = ESTADO_TELA_CAT;
                    break;
                case ESTADO_TELA_CAT:
                    contador_vitoria = 0;
                    pontos_totais = 0;
                    pontos_fase = 0;
                    mortes_fase = 0;
                    estado_atual = ESTADO_TELA_INICIO_GAME;
                    break;
                case ESTADO_TELA_INICIO_GAME:
                    estado_atual = ESTADO_TELA_INTRO_FASE;
                    break;
                case ESTADO_TELA_INTRO_FASE:
                    estado_atual = ESTADO_JOGO_RODANDO;
                    break;
                case ESTADO_JOGO_RODANDO:
                case ESTADO_TELA_PAUSE:
                    estado_atual = ESTADO_TELA_VITORIA;
                    break;
                case ESTADO_TELA_VITORIA:
                    contador_vitoria++;
                    if (contador_vitoria >= 3) {
                        estado_atual = ESTADO_TELA_RANK;
                    } else {
                        pontos_fase = 0;
                        mortes_fase = 0;
                        estado_atual = ESTADO_TELA_INTRO_FASE;
                    }
                    break;
                case ESTADO_TELA_DERROTA:
                    estado_atual = ESTADO_JOGO_RODANDO;
                    break;
                case ESTADO_TELA_RANK:
                    estado_atual = ESTADO_TELA_CREDITOS;
                    break;
                case ESTADO_TELA_CREDITOS:
                default:
                    contador_vitoria = 0;
                    estado_atual = ESTADO_MENU_INICIAL;
                    break;
            }
        }

        //maquina de estados principal das telas e da partida
        switch (estado_atual) {

            case ESTADO_TELA_INICIAL:
                if (!estado_iniciado) {
                    carregar_recursos_tela_inicial();
                    timer_frames    = 0;
                    estado_iniciado = 1;
                }
                if (atualizar_tela_inicial(timer_frames++)) {
                    limpar_direcoes_teclado();
                    estado_iniciado = 0;
                    estado_atual = ESTADO_MENU_INICIAL;
                }
                noop();
                break;

            case ESTADO_MENU_INICIAL:
                if (!estado_iniciado) {
                    carregar_menu();
                    timer_frames = 0;
                    estado_iniciado = 1;
                }
                atualizar_animacao_menu(timer_frames++);

                if (botoes.tecla_enter) {
                    limpar_direcoes_teclado();
                    estado_iniciado = 0;
                    estado_atual  = ESTADO_TELA_CAT;
                }
                noop();
                break;

            case ESTADO_TELA_CAT:
                if (!estado_iniciado) {
                    carregar_tela_cat();
                    timer_frames = 0;
                    estado_iniciado = 1;
                }
                
                //alterna entre as 3 skins disponiveis usando esquerda/direita
                if (botoes.tecla_esq) {
                    skin_selecionada = (skin_selecionada + TOTAL_SKINS_GATO - 1) % TOTAL_SKINS_GATO;
                } else if (botoes.tecla_dir || botoes.tecla_s) {
                    skin_selecionada = (skin_selecionada + 1) % TOTAL_SKINS_GATO;
                }

                atualizar_tela_cat(timer_frames++, skin_selecionada);

                if (botoes.tecla_enter) {
                    limpar_direcoes_teclado();
                    contador_vitoria = 0;
                    pontos_totais = 0;
                    pontos_fase = 0;
                    mortes_fase = 0;
                    fase_carregada = 0;
                    estado_iniciado = 0;
                    estado_atual = ESTADO_TELA_INICIO_GAME;
                }
                noop();
                break;

            case ESTADO_TELA_INICIO_GAME:
                if (!estado_iniciado) {
                    carregar_tela_inicio_game();
                    timer_frames = 0;
                    estado_iniciado = 1;
                }

                atualizar_tela_intro_fase(timer_frames++);

                if (botoes.tecla_enter) {
                    limpar_direcoes_teclado();
                    estado_iniciado = 0;
                    estado_atual = ESTADO_TELA_INTRO_FASE;
                }
                noop();
                break;

            case ESTADO_TELA_INTRO_FASE:
                if (!estado_iniciado) {
                    carregar_tela_intro_fase(contador_vitoria);
                    timer_frames = 0;
                    estado_iniciado = 1;
                }

                atualizar_tela_intro_fase(timer_frames++);

                if (botoes.tecla_enter) {
                    limpar_direcoes_teclado();
                    //inicia o cronômetro da fase
                    t_inicio_fase = obter_tempo_atual_segundos();
                    pontos_fase = 0;
                    mortes_fase = 0;
                    fase_carregada = 0;
                    estado_iniciado = 0;
                    estado_atual = ESTADO_JOGO_RODANDO;
                }
                noop();
                break;

            case ESTADO_JOGO_RODANDO:
                if (!fase_carregada) {
                    limpar_direcoes_teclado();
                    pontos_fase = 0; //zera os pontos da tentativa anterior ao renascer
                    carregar_fase(contador_vitoria, 1);
                    timer_frames = 0;
                    fase_carregada = 1;
                }

                //dispara nova direção apenas quando o gato estiver parado
                if (vel_tx == 0 && vel_ty == 0 && !botoes.espaco_segurado) {
                    if (botoes.dir_x != 0 || botoes.dir_y != 0) {
                        vel_tx = botoes.dir_x;
                        vel_ty = botoes.dir_y;
                        limpar_direcoes_teclado();
                    }
                } else {
                    limpar_direcoes_teclado();
                }

                //executa fisica, movimentacao dos inimigos e atualizacao visual da fase
                executar_estado_jogando(botoes.espaco_segurado, timer_frames++);
                
                //pausa a partida ao pressionar ESC
                if (botoes.tecla_esc) {
                    limpar_direcoes_teclado();
                    timer_frames = 0;
                    estado_iniciado = 0;
                    opcao_pause = 2;
                    estado_atual = ESTADO_TELA_PAUSE;
                }
                //vitória ao encostar no portal
                else if (posicao_jogador_no_endgame()) {
                    limpar_direcoes_teclado();
                    vel_tx = 0;
                    vel_ty = 0;

                    animar_entrada_porta(portal_px, portal_py - ((etapa_fase == 0) ? 240 : 0));
                    
                    //calcula pontuacao final da fase (bugs + bonus de tempo - penalidade por mortes)
                    double tempo_gasto = obter_tempo_atual_segundos() - t_inicio_fase;
                    if (tempo_gasto < 0.0) tempo_gasto = 0.0;

                    int pts_bugs = pontos_fase;

                    int bonus_tempo = (int)(650.0 / (1.0 + 0.04 * tempo_gasto));

                    int brutos = pts_bugs + bonus_tempo;
                    int penalidade = mortes_fase * 220;

                    int piso_minimo = (pts_bugs > 0) ? (pts_bugs - 150) : 50;
                    if (brutos - penalidade < piso_minimo) {
                        pontos_fase = piso_minimo;
                    } else {
                        pontos_fase = brutos - penalidade;
                    }

                    pontos_totais += pontos_fase;

                    fase_carregada = 0;
                    estado_iniciado = 0;
                    estado_atual = ESTADO_TELA_VITORIA;
                }

                //verifica derrota ao colidir com espinhos, inimigos ou projeteis
                else if (checar_colisoes(botoes.espaco_segurado) == -1) {
                    limpar_direcoes_teclado();
                    vel_tx = 0;
                    vel_ty = 0;

                    animar_morte_gato(jogador_px, jogador_py - ((etapa_fase == 0) ? 240 : 0));

                    mortes_fase++;

                    pontos_fase -= 220;
                    if (pontos_fase < 0) pontos_fase = 0;

                    fase_carregada = 0;
                    estado_iniciado = 0;
                    estado_atual = ESTADO_TELA_DERROTA;
                }

                noop();
                break;

            case ESTADO_TELA_PAUSE:
                if (!estado_iniciado) {
                    carregar_tela_pause();
                    timer_frames = 0;
                    estado_iniciado = 1;
                }

                if (botoes.tecla_esq) {
                    opcao_pause = (opcao_pause + 2) % 3;
                } else if (botoes.tecla_dir) {
                    opcao_pause = (opcao_pause + 1) % 3;
                }

                atualizar_tela_pause(timer_frames++, opcao_pause);

                if (botoes.tecla_esc || (botoes.tecla_enter && opcao_pause == 2)) {
                    limpar_direcoes_teclado();
                    restaurar_cena_fase_atual();
                    estado_iniciado = 0;
                    estado_atual  = ESTADO_JOGO_RODANDO;
                } else if (botoes.tecla_enter && opcao_pause == 1) {
                    limpar_direcoes_teclado();
                    fase_carregada = 0;
                    estado_iniciado = 0;
                    estado_atual = ESTADO_JOGO_RODANDO;
                } else if (botoes.tecla_enter && opcao_pause == 0) {
                    limpar_direcoes_teclado();
                    fase_carregada = 0;
                    estado_iniciado = 0;
                    estado_atual = ESTADO_MENU_INICIAL;
                }
                noop();
                break;

            case ESTADO_TELA_VITORIA:
                if (!estado_iniciado) {
                    carregar_tela_vitoria(pontos_fase);
                    estado_iniciado = 1;
                }

                if (botoes.tecla_enter) {
                    limpar_direcoes_teclado();
                    contador_vitoria++;
                    estado_iniciado = 0;
                    fase_carregada = 0;
                    
                    //se concluiu as 3 fases, registra pontuacao e exibe o ranking
                    if (contador_vitoria >= 3) {
                        registrar_no_ranking(pontos_totais, skin_selecionada);
                        estado_atual = ESTADO_TELA_RANK;
                    } else {
                        pontos_fase = 0;
                        mortes_fase = 0;
                        estado_atual = ESTADO_TELA_INTRO_FASE;
                    }
                }
                noop();
                break;

            case ESTADO_TELA_DERROTA:
                if (!estado_iniciado) {
                    carregar_tela_derrota(pontos_fase);
                    estado_iniciado = 1;
                }

                if (botoes.tecla_enter) {
                    limpar_direcoes_teclado();
                    fase_carregada = 0;
                    estado_iniciado = 0;
                    estado_atual = ESTADO_JOGO_RODANDO;
                }
                noop();
                break;

            case ESTADO_TELA_RANK:
                if (!estado_iniciado) {
                    carregar_tela_rank();
                    timer_frames = 0;
                    estado_iniciado = 1;
                }

                atualizar_tela_rank(timer_frames++);

                if (botoes.tecla_enter) {
                    limpar_direcoes_teclado();
                    estado_iniciado = 0;
                    estado_atual = ESTADO_TELA_CREDITOS;
                }
                noop();
                break;

            case ESTADO_TELA_CREDITOS:
                if (!estado_iniciado) {
                    carregar_tela_creditos();
                    estado_iniciado = 1;
                }

                if (botoes.tecla_enter) {
                    limpar_direcoes_teclado();
                    contador_vitoria = 0;
                    fase_carregada = 0;
                    estado_iniciado = 0;
                    estado_atual = ESTADO_MENU_INICIAL;
                }
                noop();
                break;
        }
        
        //registra telemetria de desempenho
        double t_frame_fim = obter_tempo_atual_segundos();
        metricas_registrar_frame(t_frame_fim - t_frame_inicio);

        usleep(16000);
    }

    //encerra recursos de video e finaliza a thread do teclado
    ocultar_todos_sprites_e_poligonos();
    noop();
    gpu_close();

    pthread_cancel(th_teclado);
    pthread_join(th_teclado, NULL);
    return EXIT_SUCCESS;
}