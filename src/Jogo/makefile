CC      = gcc
CFLAGS  = -Wall -std=gnu99 -O2 -Iinclude
LDFLAGS = -lpthread -lrt
TARGET  = jogo

# Arquivos fonte
SRCS_C  = main.c threads.c metricas.c telas.c mapa.c logica_jogo.c
SRCS_S  = libAssembly.S

# Arquivos objeto intermediários (.o)
OBJS    = $(SRCS_C:.c=.o) $(SRCS_S:.S=.o)

all: $(TARGET)

# Etapa de linkagem final (junta os .o no executável 'jogo')
$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS) $(LDFLAGS)

# Compilação individual de cada arquivo .c (com geração automática de dependências .d)
%.o: %.c
	$(CC) $(CFLAGS) -MMD -MP -c $< -o $@

# Compilação do driver Assembly
%.o: %.S
	$(CC) $(CFLAGS) -c $< -o $@

# Inclui as dependências automáticas dos headers (.h)
-include $(OBJS:.o=.d)

# Limpa os arquivos compilados caso precise refazer do zero
clean:
	rm -f $(OBJS) $(OBJS:.o=.d) $(TARGET)