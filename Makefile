CC = gcc -Wall -std=c99

LDFLAGS = -lSDL2 -lSDL2_net
EXE = u64view

all: $(EXE)

SOURCE=*.c
HEADER=*.h

$(EXE): $(SOURCE) $(HEADER)
	$(CC) -O2 $(SOURCE) -o$(EXE) $(LDFLAGS)

clean:
	rm -f $(EXE) *.o
