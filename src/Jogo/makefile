CC = gcc
CFLAGS = -Wall -std=gnu99 -O2 -Iinclude
LDFLAGS = -lpthread -lrt
TARGET = jogo

SRCS_C = main.c threads.c metricas.c telas.c mapa.c logica_jogo.c
SRCS_S = libAssembly.S

OBJS = $(SRCS_C:.c=.o) $(SRCS_S:.S=.o)

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS) $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -MMD -MP -c $< -o $@

%.o: %.S
	$(CC) $(CFLAGS) -c $< -o $@

-include $(OBJS:.o=.d)

clean:
	rm -f $(OBJS) $(OBJS:.o=.d) $(TARGET)