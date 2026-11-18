#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <pthread.h>
#include <linux/input.h>
#include "jogo.h"

//Declaracao das estruturas 
static EstadoTeclado teclado = {0, 0, 0, 0, 0, 0, 0, 0, 0};
volatile int jogo_rodando = 1;
static pthread_mutex_t mutex_teclado = PTHREAD_MUTEX_INITIALIZER;

//Zera a ocorrencia de eventos para as teclas
void limpar_direcoes_teclado(void) {
    pthread_mutex_lock(&mutex_teclado);
    teclado.dir_x = 0;
    teclado.dir_y = 0;
    teclado.tecla_enter = 0;
    teclado.tecla_esc = 0;
    teclado.tecla_esq = 0;
    teclado.tecla_dir = 0;
    teclado.tecla_s = 0;
    teclado.tecla_p = 0;
    pthread_mutex_unlock(&mutex_teclado);
}

//Declaracao da thread do teclado
void *thread_teclado_usb(void *arg) {
    (void)arg;
    int fds[8];
    int num_fds = 0;
    char caminho[64];

    //Obtem os descritres de arquivo
    //O teclado pode ser mapeado em um dos oito
    for (int i = 0; i < 8; i++) {
        snprintf(caminho, sizeof(caminho), "/dev/input/event%d", i);
        int fd = open(caminho, O_RDONLY | O_NONBLOCK);
        if (fd >= 0) {
            fds[num_fds++] = fd;
        }
    }

    struct input_event ev;

    while (jogo_rodando) {
        int leu_algo = 0;

        for (int i = 0; i < num_fds; i++) {
            while (read(fds[i], &ev, sizeof(struct input_event)) == (ssize_t)sizeof(struct input_event)) {
                //Identifica a ocorrencia de um evento no teclado
                if (ev.type == EV_KEY) {
                    leu_algo = 1;
                    pthread_mutex_lock(&mutex_teclado);

                    //Tecla espace 
                    if (ev.code == KEY_SPACE) {
                        if (ev.value == 1 || ev.value == 2) {
                            teclado.espaco_segurado = 1;
                        } else if (ev.value == 0) {
                            teclado.espaco_segurado = 0;
                        }
                    }

                    //Demais teclas
                    if (ev.value == 1) {
                        switch (ev.code) {
                            case KEY_ENTER:
                            case KEY_KPENTER:
                                teclado.tecla_enter = 1;
                                break;

                            case KEY_ESC:
                                teclado.tecla_esc = 1;
                                break;

                            case KEY_P:
                                teclado.tecla_p = 1;
                                break;

                            case KEY_A:
                            case KEY_LEFT:
                                teclado.tecla_esq = 1;
                                if (estado_atual == ESTADO_JOGO_RODANDO && vel_tx == 0 && vel_ty == 0) {
                                    teclado.dir_x = -1;
                                    teclado.dir_y =  0;
                                }
                                break;

                            case KEY_D:
                            case KEY_RIGHT:
                                teclado.tecla_dir = 1;
                                if (estado_atual == ESTADO_JOGO_RODANDO && vel_tx == 0 && vel_ty == 0) {
                                    teclado.dir_x =  1;
                                    teclado.dir_y =  0;
                                }
                                break;

                            case KEY_W:
                            case KEY_UP:
                                if (estado_atual == ESTADO_JOGO_RODANDO && vel_tx == 0 && vel_ty == 0) {
                                    teclado.dir_x =  0;
                                    teclado.dir_y = -1;
                                }
                                break;

                            case KEY_S:
                            case KEY_DOWN:
                                teclado.tecla_s = 1;
                                if (estado_atual == ESTADO_JOGO_RODANDO && vel_tx == 0 && vel_ty == 0) {
                                    teclado.dir_x =  0;
                                    teclado.dir_y =  1;
                                }
                                break;

                            default:
                                break;
                        }
                    }

                    pthread_mutex_unlock(&mutex_teclado);
                }
            }
        }

        //Pausa a execucao da thread caso eventos nao sejam capturados por um período
        if (!leu_algo) {
            usleep(2000);
        }
    }

    //Fecha os descritores de arquivo
    for (int i = 0; i < num_fds; i++) {
        close(fds[i]);
    }
    return NULL;
}

//Ler os estados das teclas
void ler_botoes_estado(EstadoTeclado *copia) {
    pthread_mutex_lock(&mutex_teclado);
    *copia = teclado;
    teclado.tecla_enter = 0;
    teclado.tecla_esc = 0;
    teclado.tecla_esq = 0;
    teclado.tecla_dir = 0;
    teclado.tecla_s = 0;
    teclado.tecla_p = 0;

    //Garante que o personagem nao se mova fora da tela de jogo
    if (estado_atual != ESTADO_JOGO_RODANDO) {
        teclado.dir_x = 0;
        teclado.dir_y = 0;
    }
    pthread_mutex_unlock(&mutex_teclado);
}