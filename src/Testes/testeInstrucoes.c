#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

extern int  gpu_init(void);
extern void gpu_close(void);
extern int  enviar_instrucao(uint32_t instr);
extern int  clear_status(void);
extern int  noop(void);
extern int  bscroll(int x, int y);
extern int  btile(int end, int cor);
extern int  possp(int id, int x, int y);
extern int  atsp(int id, int flip, int show);
extern int  wrsp(int end, int cor);
extern int  rgsp(int id, int slot);
extern int  atpo(int flip, int id, int show, int tipo);
extern int  pospo(int id, int x, int y);
extern int  tampo(int id, int larg, int alt);
extern int  corpo(int id, int cor);
extern int  prior(int p);

extern int  status(void);
extern int  busy(void);
extern int  fifo_full(void);
extern int  fifo_empty(void);
extern int  error(void);
extern int  warning(void);
extern int  frame_counter(void);

static void mostrar_status_gpu(const char *etapa) {
    printf("   [STATUS %s] status=0x%08X | busy=%d | fifo_full=%d | fifo_empty=%d | error=%d | warning=%d | frame_hw=%d\n",
           etapa, status(), busy(), fifo_full(), fifo_empty(), error(), warning(), frame_counter());
}

int main(void) {
    if (gpu_init() != 0) {
        fprintf(stderr, "Erro ao inicializar a GPU\n");
        return EXIT_FAILURE;
    }
    clear_status();

    int opcao;
    int x, y, id, cor, larg, alt, flip, show, end, reg, slot, tipo, prioridade, pixel;
    uint32_t instr_crua;

    printf("\033[1;33mTESTE DE ENTRADA DE INSTRUÇÕES PARA O COPROCESSADOR\033[m\n");
    printf("Para o teste funcionar, digite os números corretamente, sendo um usuário perfeito.\n");
    printf("\033[1;31mCaso digite algum número fora do intervalo, o programa pode se comportar de maneira inesperada.\033[m\n\n");

    while (1) {
        printf("\n\033[1;36mEscolha uma instrução:\033[m\n"
               "[1]  noop()\n"
               "[2]  bscroll(x, y)\n"
               "[3]  btile(end_pixel, cor)\n"
               "[4]  possp(id, x, y)\n"
               "[5]  atsp(id, flip, show)\n"
               "[6]  wrsp(end, cor)\n"
               "[7]  rgsp(id, slot)\n"
               "[8]  atpo(flip, id, show, tipo)\n"
               "[9]  pospo(id, x, y)\n"
               "[10] tampo(id, larg, alt)\n"
               "[11] corpo(id, cor)\n"
               "[12] prior(p)\n"
               "[13] Testar COMANDO INVALIDO\n"
               "[14] clear_status()\n"
               "[0]  Sair\n> ");

        scanf("%d", &opcao);
        printf("\n");


        switch (opcao) {
            case 1:
                noop();
                break;
            case 2:
                printf("X (0-63) e Y (0-31): ");
                scanf("%d %d", &x, &y);
                bscroll(x, y);
                break;
            case 3:
                printf("Endereco pixel (0-76799) e Cor RGB332 (0-255): ");
                scanf("%d %d", &pixel, &cor);
                btile(pixel, cor);
                break;
            case 4:
                printf("ID (0-31), X (0-319), Y (0-239): ");
                scanf("%d %d %d", &id, &x, &y);
                possp(id, x, y);
                break;
            case 5:
                printf("ID (0-31), Flip (0-3), Show (0 ou 1): ");
                scanf("%d %d %d", &id, &flip, &show);
                atsp(id, flip, show);
                break;
            case 6:
                printf("Endereco RAM Sprite (0-8191) e Cor (0-255): ");
                scanf("%d %d", &end, &cor);
                wrsp(end, cor);
                break;
            case 7:
                printf("Registrador ID (0-31) e Slot RAM (0-31): ");
                scanf("%d %d", &reg, &slot);
                rgsp(reg, slot);
                break;
            case 8:
                printf("Flip (0-3), ID (0-15), Show (0/1), Tipo (0-4): ");
                scanf("%d %d %d %d", &flip, &id, &show, &tipo);
                atpo(flip, id, show, tipo);
                break;
            case 9:
                printf("ID (0-15), X (0-319), Y (0-239): ");
                scanf("%d %d %d", &id, &x, &y);
                pospo(id, x, y);
                break;
            case 10:
                printf("ID (0-15), Largura (1-319), Altura (1-239): ");
                scanf("%d %d %d", &id, &larg, &alt);
                tampo(id, larg, alt);
                break;
            case 11:
                printf("ID (0-15) e Cor RGB332 (0-255): ");
                scanf("%d %d", &id, &cor);
                corpo(id, cor);
                break;
            case 12:
                printf("Prioridade (0=Sprite na frente, 1=Poligono na frente): ");
                scanf("%d", &prioridade);
                prior(prioridade);
                break;
            case 13:
                /* Opcode 0b11111 (31) não existe na ISA do coprocessador */
                instr_crua = 0xFFFFFFFF;
                printf("Enviando instrucao invalida: 0x%08X...\n", instr_crua);
                int ret = enviar_instrucao(instr_crua);
                printf("Retorno de enviar_instrucao = %d (0 = handshake concluido sem travar)\n", ret);
                break;
            case 14:
                clear_status();
                printf("Flags de status limpas.\n");
                break;
            case 0:
                gpu_close();
                return EXIT_SUCCESS;
            default:
                printf("Opcao invalida.\n");
                continue;
        }

        mostrar_status_gpu("APOS ENVIO");
    }

    gpu_close();
    return EXIT_SUCCESS;
}