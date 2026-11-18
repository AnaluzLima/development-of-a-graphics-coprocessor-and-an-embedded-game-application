#include <stdlib.h>
#include "jogo.h"

//Declaracao das variaveis
int skin_selecionada = SKIN_GATO_PRETO;
int fase_atual = 0;
int pontos_fase = 0;
int pontos_totais = 0;
int bugs_coletados = 0;
int jogador_px = 136;
int jogador_py = 448;
int vel_tx = 0;
int vel_ty = 0;
int flip_jogador = 0;
int mortes_fase = 0;

static int bateu_patrulha = 0;

//Identifica a colisao entre retangulos
static int retangulos_colidem(int x1, int y1, int w1, int h1, int x2, int y2, int w2, int h2) {
    return (x1 < x2 + w2) && (x1 + w1 > x2) && (y1 < y2 + h2) && (y1 + h1 > y2);
}

//Identifica a colisao com paredes/aves/bolhas
static int colisao_parede(int px, int py) {

    //Define quais tiles sao ocupados pelo personagem
    int tx_esq = px / 8;
    int tx_dir = (px + 15) / 8;
    int ty_sup = py / 8;
    int ty_inf = (py + 15) / 8;

    if (tx_esq < 0 || tx_dir >= TILES_X || ty_sup < 0 || ty_inf >= MAP_TILES_Y) {
        return 1;
    }

    if (mapa_atual[ty_sup][tx_esq] == TILE_PAREDE ||
        mapa_atual[ty_sup][tx_dir] == TILE_PAREDE ||
        mapa_atual[ty_inf][tx_esq] == TILE_PAREDE ||
        mapa_atual[ty_inf][tx_dir] == TILE_PAREDE) {
        return 1;
    }

    //Colisao com aves
    for (int i = 0; i < num_aves; i++) {
        if (aves[i].ativo) {
            if (retangulos_colidem(px, py, 16, 16, aves[i].x, aves[i].y, 16, 16)) {
                return 1;
            }
        }
    }

    //Colisao com bolhas/patrulhas
    for (int i = 0; i < num_patrulhas; i++) {
        if (patrulhas[i].ativo) {
            if (retangulos_colidem(px, py, 16, 16, patrulhas[i].px, patrulhas[i].py, 16, 16)) {
                bateu_patrulha = 1;
                return 1;
            }
        }
    }

    return 0;
}

//Identifica colisao com um ponto
int colisao_ponto(int px, int py, int w, int h) {
    for (int y = py; y < py + h; y++)
        for (int x = px; x < px + w; x++)
            if (mapa_parede_pixel(x, y)) return 1;
    return 0;
}

void executar_estado_jogando(int espaco_segurado, int frame_atual) {
    const int PASSO_PX = 8;
    bateu_patrulha = 0;

    //Movimentação contínua
    if (!espaco_segurado && (vel_tx != 0 || vel_ty != 0)) {
        int prox_x = jogador_px + vel_tx * PASSO_PX;
        int prox_y = jogador_py + vel_ty * PASSO_PX;

        if (!colisao_parede(prox_x, prox_y)) {
            jogador_px = prox_x;
            jogador_py = prox_y;
        } else {
            while (!colisao_parede(jogador_px + vel_tx, jogador_py + vel_ty)) {
                jogador_px += vel_tx;
                jogador_py += vel_ty;
            }
            vel_tx = 0;
            vel_ty = 0;
        }
    }

    if (vel_ty < 0) flip_jogador = 2; //cabeça para baixo
    else if (vel_ty > 0) flip_jogador = 0; //normal para baixo
    else if (vel_tx < 0) flip_jogador = 0; //para a esquerda
    else if (vel_tx > 0) flip_jogador = 1; //Para a direita

    //Transicao de etapa
    atualizar_etapa_camera(jogador_py);

    //Coordenada Y relativa à metade ativa da tela
    int sy = jogador_py - ((etapa_fase == 0) ? 240 : 0);
    if (sy < 0) sy = 0;
    if (sy > 224) sy = 224;

    rgsp(SPRITE_JOGADOR, espaco_segurado ? 1 : 0);
    possp(SPRITE_JOGADOR, jogador_px, sy);
    atsp(SPRITE_JOGADOR, flip_jogador, 1);

    //Rastro amarelo
    if (vel_tx != 0 || vel_ty != 0) {
        if (vel_ty != 0) {
            int trail_y1 = (vel_ty < 0) ? (sy + 16) : (sy - 6);
            int trail_y2 = (vel_ty < 0) ? (sy + 16) : (sy - 9);

            pospo(12, jogador_px + 6, trail_y1);
            tampo(12, 1, 6);
            corpo(12, 0xFC);
            atpo(0, 12, 1, POL_RETANGULO);

            pospo(13, jogador_px + 9, trail_y2);
            tampo(13, 1, 9);
            corpo(13, 0xFC);
            atpo(0, 13, 1, POL_RETANGULO);
        } else {
            int trail_x1 = (vel_tx < 0) ? (jogador_px + 16) : (jogador_px - 6);
            int trail_x2 = (vel_tx < 0) ? (jogador_px + 16) : (jogador_px - 9);

            pospo(12, trail_x1, sy + 6);
            tampo(12, 6, 1);
            corpo(12, 0xFC);
            atpo(0, 12, 1, POL_RETANGULO);

            pospo(13, trail_x2, sy + 9);
            tampo(13, 9, 1);
            corpo(13, 0xFC);
            atpo(0, 13, 1, POL_RETANGULO);
        }
    } else {
        atpo(0, 12, 0, POL_RETANGULO);
        atpo(0, 13, 0, POL_RETANGULO);
    }

    //Espinhos Temporarios
    for (int i = 0; i < num_espinhos_temp; i++) {
        EspinhoTemporario *e = &espinhos_temp[i];
        int horiz = (e->tipo_flip == POL_TRI_CIMA || e->tipo_flip == POL_TRI_BAIXO);
        int mw = horiz ? 9 : 2;
        int mh = horiz ? 2 : 9;

        int trigger_x = e->marcador_x - 2;
        int trigger_y = e->marcador_y - 2;
        int trigger_w = mw + 4;
        int trigger_h = mh + 4;

        if (!e->ativo) {
            if (!e->armado) {
                if (retangulos_colidem(jogador_px, jogador_py, 16, 16, trigger_x, trigger_y, trigger_w, trigger_h)) {
                    e->armado = 1;
                    e->timer_armar = 60;
                }
            } else {
                e->timer_armar--;
                if (e->timer_armar <= 0) {
                    e->armado = 0;
                    e->ativo = 1;
                    e->timer_ativo = 180;
                }
            }
        } else {
            e->timer_ativo--;
            if (e->timer_ativo <= 0) {
                e->ativo = 0;
                e->armado = 0;
            }
        }
    }

    // Movimentação dos Inimigos
    for (int i = 0; i < num_patrulhas; i++) {
        InimigoPatrulha *p = &patrulhas[i];
        if (!p->ativo) continue;

        int min_x = (p->x1 < p->x2) ? p->x1 : p->x2;
        int max_x = (p->x1 > p->x2) ? p->x1 : p->x2;
        int min_y = (p->y1 < p->y2) ? p->y1 : p->y2;
        int max_y = (p->y1 > p->y2) ? p->y1 : p->y2;

        //move 1px por vez na direção atual para encostar no gato sem sobrepor o sprite
        int passos = (p->vx != 0) ? abs(p->vx) : abs(p->vy);
        int sx_step = (p->vx > 0) ? 1 : ((p->vx < 0) ? -1 : 0);
        int sy_step = (p->vy > 0) ? 1 : ((p->vy < 0) ? -1 : 0);

        for (int s = 0; s < passos; s++) {
            int nx = p->px + sx_step;
            int ny = p->py + sy_step;
            //se o passo da patrulha for sobrepor o 16x16 exato do gato, para antes e marca colisão */
            if (retangulos_colidem(jogador_px, jogador_py, 16, 16, nx, ny, 16, 16)) {
                bateu_patrulha = 1;
                break;
            }
            p->px = nx;
            p->py = ny;
        }

        if (p->circular) {
            if (p->circular == 1) {
                //sentido anti-horario
                int vel = (p->vx != 0) ? abs(p->vx) : abs(p->vy);

                if (p->vy > 0 && p->py >= max_y) {
                    p->py = max_y; p->vy = 0; p->vx = vel;
                } else if (p->vx > 0 && p->px >= max_x) {
                    p->px = max_x; p->vx = 0; p->vy = -vel;
                } else if (p->vy < 0 && p->py <= min_y) {
                    p->py = min_y; p->vy = 0; p->vx = -vel;
                } else if (p->vx < 0 && p->px <= min_x) {
                    p->px = min_x; p->vx = 0; p->vy = vel;
                }
            } else if (p->circular == 2) {
                //sentido horario
                int vel = (p->vx != 0) ? abs(p->vx) : abs(p->vy);

                if (p->vx > 0 && p->px >= max_x) {
                    p->px = max_x; p->vx = 0; p->vy = vel;
                } else if (p->vy > 0 && p->py >= max_y) {
                    p->py = max_y; p->vy = 0; p->vx = -vel;
                } else if (p->vx < 0 && p->px <= min_x) {
                    p->px = min_x; p->vx = 0; p->vy = -vel;
                } else if (p->vy < 0 && p->py <= min_y) {
                    p->py = min_y; p->vy = 0; p->vx = vel;
                }
            } else {
                //linear
                if (p->vx != 0 && (p->px <= min_x || p->px >= max_x)) p->vx = -p->vx;
                if (p->vy != 0 && (p->py <= min_y || p->py >= max_y)) p->vy = -p->vy;
            }
        }
    }

    //cada bug vale +700 pontos
    for (int i = 0; i < num_bugs; i++) {
        if (bugs[i].vivo) {
            if (retangulos_colidem(jogador_px, jogador_py, 16, 16, bugs[i].px, bugs[i].py, 16, 16)) {
                bugs[i].vivo = 0;
                bugs[i].frame_morte = 1;
                bugs_coletados++;
                pontos_fase += 700;
            }
        }
    }

    //lógica dos BotDogs
    if (!botdog_ativa) {
        for (int i = 0; i < num_botdogs; i++) {
            BotDogConfig *bd = &botdogs[i];
            if (bd->ativo && !bd->disparado && jogador_py <= bd->y_gatilho) {
                bd->disparado = 1;
                botdog_ativa = 1;
                botdog_dir = bd->dir;
                botdog_x = (bd->dir > 0) ? -80 : SCREEN_W;
                botdog_y = bd->y_pos;
                botdog_frames = 0;
                tremor_tela = 1;
                break;
            }
        }
    } else {
        botdog_x += (4 * botdog_dir);
        botdog_frames++;
        if ((botdog_dir > 0 && botdog_x > SCREEN_W + 16) ||
            (botdog_dir < 0 && botdog_x < -80)) {
            botdog_ativa = 0;
            tremor_tela  = 0;
        }
    }

    //portal da fase 3 desaparecendo
    if (fase_atual == 2 && !portal_fugiu) {
        if (retangulos_colidem(jogador_px, jogador_py, 16, 16, 144, 57, 16, 8)) {
            portal_px = 235;
            portal_py = 427;
            portal_fugiu = 1;
            
            apagar_porta_topo_fase3();
        }
    }

    atualizar_ave_robo();
    atualizar_visuais_objetos_fase(frame_atual);
}

//Realiza a animacao das aves
void atualizar_ave_robo(void) {
    for (int a = 0; a < num_aves; a++) {
        AveRobo *c = &aves[a];
        if (!c->ativo) continue;

        if (c->anim_frames > 0) c->anim_frames--;
        if (c->explosao_frames > 0) c->explosao_frames--;

        //jogador na outra metade do mapa
        //a ave dessa metade fica em espera
        if (etapa_fase != c->etapa) {
            c->tiro_ativo = 0;
            c->timer = 0;
            continue;
        }

        if (++c->timer >= DOG_INTERVALO_TIRO) {
            c->timer = 0;
            c->anim_frames = 15;
            c->tiro_ativo  = 1;
            c->tiro_x = c->tiro_x0;
            c->tiro_y = c->tiro_y0;
        }

        if (c->tiro_ativo) {
            int meio = (c->etapa == 0) ? 240 : 0;
            for (int i = 0; i < DOG_VEL_TIRO; i++) {
                int nx = c->tiro_x + c->dir;
                if (colisao_ponto(nx, c->tiro_y, 3, 3) ||
                    c->tiro_y < meio || c->tiro_y >= meio + SCREEN_H) {
                    c->tiro_ativo = 0;
                    c->explosao_frames = 8;
                    c->explosao_x = c->tiro_x;
                    c->explosao_y = c->tiro_y;
                    break;
                }
                c->tiro_x = nx;
            }
        }
    }
}

//Identifica se o jogador terminou a fase
int posicao_jogador_no_endgame(void) {
    return retangulos_colidem(jogador_px, jogador_py, 16, 16, portal_px, portal_py, 14, 14);
}

//Identifica colisoes com componentes fatais
int checar_colisoes(int espaco_segurado) {
    //colisao com espinhos basicos
    for (int i = 0; i < num_espinhos; i++) {
        EspinhoFase *e = &espinhos[i];
        int w = (e->tipo_flip == POL_TRI_DIR || e->tipo_flip == POL_TRI_ESQ) ? 7 : 9;
        int h = (w == 7) ? 9 : 7;
        if (retangulos_colidem(jogador_px + 2, jogador_py + 2, 12, 12, e->px, e->py, w, h)) {
            return -1;
        }
    }

    //colisao com espinhos temporarios
    for (int i = 0; i < num_espinhos_temp; i++) {
        EspinhoTemporario *e = &espinhos_temp[i];
        if (e->ativo) {
            int horiz = (e->tipo_flip == POL_TRI_CIMA || e->tipo_flip == POL_TRI_BAIXO);
            int sw = horiz ? 9 : 7;
            int sh = horiz ? 7 : 9;

            int sx = e->marcador_x;
            int sy = e->marcador_y;

            if (e->tipo_flip == POL_TRI_CIMA) {
                sy = e->marcador_y - 5;
            } else if (e->tipo_flip == POL_TRI_ESQ) {
                sx = e->marcador_x - 5;
            }

            if (retangulos_colidem(jogador_px + 2, jogador_py + 2, 12, 12, sx, sy, sw, sh)) {
                return -1;
            }
        }
    }

    //colisão com a botdog
    if (botdog_ativa) {
        if (retangulos_colidem(jogador_px, jogador_py, 16, 16, botdog_x, botdog_y, 80, 16)) {
            return -1;
        }
    }

    //colisão com Inimigos
    if (bateu_patrulha) {
        return -1;
    }
    for (int i = 0; i < num_patrulhas; i++) {
        InimigoPatrulha *p = &patrulhas[i];
        if (p->ativo) {
            if (retangulos_colidem(jogador_px, jogador_py, 16, 16, p->px, p->py, 16, 16)) {
                return -1;
            }
        }
    }

    //colisão com o projetil de ave
    for (int a = 0; a < num_aves; a++) {
        AveRobo *c = &aves[a];
        if (c->ativo && c->tiro_ativo) {
            if (retangulos_colidem(jogador_px, jogador_py, 16, 16, c->tiro_x, c->tiro_y, 3, 3)) {
                if (espaco_segurado) {
                    c->tiro_ativo = 0;
                    c->explosao_frames = 8;
                    c->explosao_x = c->tiro_x;
                    c->explosao_y = c->tiro_y;
                } else {
                    return -1;
                }
            }
        }
    }

    return 0;
}