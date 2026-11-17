.syntax unified @instrui o montador a usar a sintaxe moderna unificada do ARM
.arm @força a geração de instruções no modo ARM nativo de 32 bits

.global gpu_init
.global gpu_close
.global enviar_instrucao

.global status
.global busy
.global fifo_full
.global fifo_empty
.global error
.global warning
.global frame_counter

.global obter_e_zerar_instrucoes
.global obter_mascara_sprites_ativos
.global obter_mascara_poligonos_ativos

.global clear_status
.global noop
.global bscroll
.global btile
.global possp
.global atsp
.global wrsp
.global rgsp
.global atpo
.global pospo
.global tampo
.global corpo
.global prior

@informa explicitamente ao linker que esses rótulos representam funções executáveis
.type gpu_init, %function
.type gpu_close, %function
.type enviar_instrucao, %function

.type status, %function
.type busy, %function
.type fifo_full, %function
.type fifo_empty, %function
.type error, %function
.type warning, %function
.type frame_counter, %function

.type obter_e_zerar_instrucoes, %function
.type obter_mascara_sprites_ativos, %function
.type obter_mascara_poligonos_ativos, %function

.type clear_status, %function
.type noop, %function
.type bscroll, %function
.type btile, %function
.type possp, %function
.type atsp, %function
.type wrsp, %function
.type rgsp, %function
.type atpo, %function
.type pospo, %function
.type tampo, %function
.type corpo, %function
.type prior, %function

.equ CLOSE, 6 @syscall para fechar arquivo
.equ OPEN, 5 @syscall para abrir arquivo
.equ MUNMAP, 91 @syscall para desmapear memoria
.equ MMAP2, 192 @syscall para mapear memoria

.equ O_RDWR_SYNC, 0x101002 @garante que qualquer escrita seja despachada diretamente para o barramento físico
.equ PROT_RW, 3 @permitir leitura e escrita na memoria mapeada
.equ MAP_SHARED,  1 @permitir compartilhamento de memoria

.equ LW_BRIDGE_PAGE, 0xFF200 @ 0xFF200000 >> 12 (mmap2 usa páginas de 4 KB)
.equ LW_SPAN, 0x200000 @ 2 MB (cobre até o offset 0x6000F)

.equ PIO_INST,   0x00000
.equ PIO_BUSY,   0x30000
.equ PIO_DONE,   0x40000
.equ PIO_VAL,    0x50000
.equ PIO_STATUS, 0x60000

.equ TIMEOUT, 0x1000000 @iterações de laço para evitar que o processador congele caso a FPGA não responda

.data @declarar variáveis globais dinâmicas
.align 2 @ptr_base precisa estar alinhado em 4 bytes
 dev_mem: .asciz "/dev/mem" @nome do arquivo de memoria
.align 2 @sem isso ptr_base ficava no offset 9 (.data)
 ptr_base: .word 0 @variavel para armazenar o ponteiro base do mapeamento de memoria

 contador_instrucoes: .word 0
 mascara_sprites_ativos: .word 0
 mascara_poligonos_ativos: .word 0

.text @onde o código executável será escrito
.align 2

gpu_init:
 push {r4-r7, lr} @salva os registradores e lr na pilha

 ldr r1, =ptr_base @carregar o endereço da variável ptr_base
 ldr r0, [r1] @armazenar o ponteiro base do mapeamento de memoria na variável ptr_base
 cmp r0, #0 @se r0 for != 0, significa que a gpu já foi inicializada
 bne init_ok

 mov r7, #OPEN @carregar o numero da syscall para abrir arquivo
 ldr r0, =dev_mem @carregar o endereço do nome do arquivo
 ldr r1, =O_RDWR_SYNC @carregar a flag para abrir arquivo em leitura e escrita
 mov r2, #0 @Como o arquivo `/dev/mem` já existe no Linux e só estamis abrindo, o valor é irrelevante
 svc 0

 cmp r0, #0
 blt erro_init @se for menor que 0, houve erro na abertura do arquivo
 mov r4, r0 @armazenar o descritor do arquivo em r4

 mov r7, #MMAP2 @carregar o numero da syscall para mapear memoria
 mov r0, #0 @o linux escolhe o endereço
 ldr r1, =LW_SPAN @2 MB
 mov r2, #PROT_RW @carregar a flag para permitir leitura e escrita na memoria mapeada
 mov r3, #MAP_SHARED @carregar a flag para permitir compartilhamento de memoria
 ldr r5, =LW_BRIDGE_PAGE @carregar o endereço base do bridge
 svc 0

 cmn r0, #4096 @se r0 < 0, o resultado da soma será negativo
 bhi erro_mmap @se for menor que 0, houve erro no mmap

 ldr r1, =ptr_base @carregar o endereço da variável ptr_base
 str r0, [r1] @armazenar o ponteiro base do mapeamento de memoria na variável ptr_base

 mov r7, #CLOSE @fecha o descritor do arquivo
 mov r0, r4 @descritor do arquivo
 svc 0

init_ok:
 @valid = 0
 ldr r0, =ptr_base
 ldr r0, [r0] @carregar o ponteiro base do mapeamento de memoria
 mov r3, #0 @inicializar valid como 0
 add r2, r0, #PIO_VAL @carregar o endereço do registrador valid
 str r3, [r2]

 mov r0, #0 @operação bem sucedida
 pop {r4-r7, pc} @pega os valores dos registradores e retorna para o endereço de retorno

erro_mmap:
 mov r7, #CLOSE @não vaza o descritor se o mmap falhar
 mov r0, r4 @descritor do arquivo
 svc 0
erro_init:
 mvn r0, #0 @-1
 pop {r4-r7, pc}

gpu_close:
 push {r7, lr} @salva os registradores e lr na pilha
 ldr r1, =ptr_base
 ldr r0, [r1]
 cmp r0, #0 @verifica se gpu_init foi chamada
 beq close_fim @se não foi chamada, não faz nada

 mov r2, #0 @zera a variável ptr_base
 str r2, [r1]
 ldr r1, =LW_SPAN
 mov r7, #MUNMAP @carregar o numero da syscall para desmapear memoria
 svc 0

close_fim:
 pop {r7, pc} @pega os valores dos registradores e retorna para o endereço de retorno


status:
 ldr r1, =ptr_base
 ldr r1, [r1]
 cmp r1, #0
 beq erro_envio
 add r2, r1, #PIO_STATUS
 ldr r0, [r2]
 bx lr

busy:
 ldr r1, =ptr_base
 ldr r1, [r1]
 cmp r1, #0
 beq erro_envio
 add r2, r1, #PIO_BUSY
 ldr r0, [r2]
 ubfx r0, r0, #0, #1
 bx lr

fifo_full:
 ldr r1, =ptr_base
 ldr r1, [r1]
 cmp r1, #0
 beq erro_envio
 add r2, r1, #PIO_STATUS
 ldr r0, [r2]
 ubfx r0, r0, #0, #1
 bx lr

fifo_empty:
 ldr r1, =ptr_base
 ldr r1, [r1]
 cmp r1, #0
 beq erro_envio
 add r2, r1, #PIO_STATUS
 ldr r0, [r2]
 ubfx r0, r0, #1, #1
 bx lr

error:
 ldr r1, =ptr_base
 ldr r1, [r1]
 cmp r1, #0
 beq erro_envio
 add r2, r1, #PIO_STATUS
 ldr r0, [r2]
 ubfx r0, r0, #2, #1
 bx lr
 
warning:
 ldr r1, =ptr_base
 ldr r1, [r1]
 cmp r1, #0
 beq erro_envio
 add r2, r1, #PIO_STATUS
 ldr r0, [r2]
 ubfx r0, r0, #3, #1
 bx lr
 
frame_counter:
 ldr r1, =ptr_base
 ldr r1, [r1]
 cmp r1, #0
 beq erro_envio
 add r2, r1, #PIO_STATUS
 ldr r0, [r2]
 ubfx r0, r0, #16, #16
 bx lr

@quantidade de instruções enviadas desde a última leitura e zera o contador
obter_e_zerar_instrucoes:
 ldr r1, =contador_instrucoes
 ldr r0, [r1]
 mov r2, #0
 str r2, [r1]
 bx lr

@máscara de 32 bits dos sprites ativos (cada bit 1 = sprite visível)
obter_mascara_sprites_ativos:
 ldr r1, =mascara_sprites_ativos
 ldr r0, [r1]
 bx lr

@máscara de 16 bits dos polígonos ativos (cada bit 1 = polígono visível)
obter_mascara_poligonos_ativos:
 ldr r1, =mascara_poligonos_ativos
 ldr r0, [r1]
 bx lr


clear_status:
  mov r0, #12
  b enviar_instrucao

noop:
  mov r0, #0
  b enviar_instrucao

bscroll:
  ubfx r0, r0, #0, #6
  ubfx r1, r1, #0, #5

  lsl r0, r0, #10
  orr r0, r0, r1, lsl #5
  orr r0, r0, #0b00001

  b enviar_instrucao

btile:
  ubfx r0, r0, #0, #17 
  ubfx r1, r1, #0, #8

  lsl r0, r0, #13 
  orr r0, r0, r1, lsl #5 
  orr r0, r0, #0b00010

  b enviar_instrucao

possp:
  ubfx r0, r0, #0, #5 @5 bits
  ubfx r1, r1, #0, #9 @9 bits
  ubfx r2, r2, #0, #8 @8 bits

  lsl r0, r0, #22 @[26:22]
  orr r0, r0, r1, lsl #13 @[21:13]
  orr r0, r0, r2, lsl #5 @[12:5]
  orr r0, r0, #0b00011 @[4:0]

  b enviar_instrucao

atsp:
  ubfx r0, r0, #0, #5 @ id (0..31)
  ubfx r1, r1, #0, #2 @ flip
  ubfx r2, r2, #0, #1 @ show (0 ou 1)

  @máscara de sprites ativos na tela
  ldr r3, =mascara_sprites_ativos
  ldr r12, [r3]
  mov r1, r1 @ preserva r1
  mov ip, #1
  lsl ip, ip, r0 @ ip = (1 << id)
  ldr r3, [r3]
  cmp r2, #0
  orrne r3, r3, ip @ se show == 1, seta o bit
  biceq r3, r3, ip @ se show == 0, limpa o bit
  ldr ip, =mascara_sprites_ativos
  str r3, [ip]

  lsl r0, r0, #8
  orr r0, r0, r1, lsl #6
  orr r0, r0, r2, lsl #5
  orr r0, r0, #0b00100

  b enviar_instrucao

wrsp:
  ubfx r0, r0, #0, #13
  ubfx r1, r1, #0, #8

  lsl r0, r0, #13
  orr r0, r0, r1, lsl #5
  orr r0, r0, #0b00101

  b enviar_instrucao

rgsp:
  ubfx r0, r0, #0, #5
  ubfx r1, r1, #0, #5

  lsl r0, r0, #10
  orr r0, r0, r1, lsl #5
  orr r0, r0, #0b00110

  b enviar_instrucao
 
atpo:
  ubfx r0, r0, #0, #2 @flip
  ubfx r1, r1, #0, #4 @id
  ubfx r2, r2, #0, #1 @show
  ubfx r3, r3, #0, #3 @tipo

  @máscara de polígonos ativos na tela
  push {r4, r5}
  mov r4, #1
  lsl r4, r4, r1
  ldr r5, =mascara_poligonos_ativos
  ldr r12, [r5]
  cmp r2, #0
  orrne r12, r12, r4  @ se show == 1, seta o bit
  biceq r12, r12, r4  @ se show == 0, limpa o bit
  str r12, [r5]
  pop {r4, r5}

  lsl r0, r0, #13
  orr r0, r0, r1, lsl #9
  orr r0, r0, r2, lsl #8
  orr r0, r0, r3, lsl #5
  orr r0, r0, #0b00111

  b enviar_instrucao

pospo:
  ubfx r0, r0, #0, #4
  ubfx r1, r1, #0, #9
  ubfx r2, r2, #0, #8

  lsl r0, r0, #22
  orr r0, r0, r1, lsl #13
  orr r0, r0, r2, lsl #5
  orr r0, r0, #0b01000

  b enviar_instrucao

tampo:
  ubfx r0, r0, #0, #4
  ubfx r1, r1, #0, #9
  ubfx r2, r2, #0, #8

  lsl r0, r0, #22
  orr r0, r0, r1, lsl #13
  orr r0, r0, r2, lsl #5
  orr r0, r0, #0b01001

  b enviar_instrucao

corpo:
  ubfx r0, r0, #0, #4
  ubfx r1, r1, #0, #8

  lsl r0, r0, #13
  orr r0, r0, r1, lsl #5
  orr r0, r0, #0b01010

  b enviar_instrucao

prior:
  ubfx r0, r0, #0, #1
  lsl r0, r0, #5
  orr r0, r0, #0b01011

  b enviar_instrucao


enviar_instrucao:
 ldr r1, =ptr_base
 ldr r1, [r1]
 cmp r1, #0
 beq erro_envio @se gpu_init não foi chamada, não faz nada

 add r2, r1, #PIO_BUSY @carrega o endereço do registrador busy
 ldr r12, =TIMEOUT @carrega o valor do timeout
espera_busy_baixo:
 ldr r3, [r2] @lê o valor do registrador busy
 tst r3, #1 @verifica se o bit 0 está setado (busy = 1)
 beq busy_livre @se busy = 0, o registrador está livre para receber instruções
 subs r12, r12, #1 @decrementa o contador de timeout
 bne espera_busy_baixo @se o contador não chegou a 0, continua esperando
 b erro_envio @se o contador chegou a 0, houve timeout e não é possível enviar a instrução

busy_livre:
 add r2, r1, #PIO_VAL @carrega o endereço do registrador valid
 add r3, r1, #PIO_DONE @carrega o endereço do registrador done

 str r0, [r1, #PIO_INST] @escreve a instrução no registrador de instruções
 dmb @garante que a escrita da instrução seja feita antes de escrever o valid
 mov r0, #1 @seta o bit de validade
 str r0, [r2] @valid = 1

 ldr r12, =TIMEOUT

espera_done_alto:
 ldr r0, [r3]
 tst r0, #1
 bne done_alto
 subs r12, r12, #1
 bne espera_done_alto
 b timeout

done_alto:
 mov r0, #0
 str r0, [r2] @valid = 0
 ldr r12, =TIMEOUT

espera_done_baixo:
 ldr r0, [r3]
 tst r0, #1
 beq envio_ok
 subs r12, r12, #1
 bne espera_done_baixo

timeout:
 mov r0, #0
 str r0, [r2]

erro_envio:
 mvn r0, #0 @-1
 bx lr

envio_ok:
 @incrementa o contador de instruções enviadas
 ldr r1, =contador_instrucoes
 ldr r2, [r1]
 add r2, r2, #1
 str r2, [r1]

 mov r0, #0
 bx lr

.ltorg
.section .note.GNU-stack,"",%progbits