CC = gcc -Wall -std=c99 -O3

LDFLAGS = -lSDL2 -lSDL2_net
EXE = u64view

all: $(EXE)

SOURCE=*.c

$(EXE): $(SOURCE)
	$(CC) $(SOURCE) -o$(EXE) $(LDFLAGS)

clean:
	rm -f $(EXE) *.o
