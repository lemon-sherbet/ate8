CFLAGS?=-O2 -g

all: ate8

ate8:
	$(CC) $(CFLAGS) -lSDL2 -lm ate8.c -o ate8

clean:
	$(RM) ate8
