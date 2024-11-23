FLAGS=-O3 -Wall

RM=rm -f

EXEC=sokoban
CC=gcc

all: $(EXEC)

$(EXEC): sokoban.c
	$(CC) $(FLAGS) $(EXEC).c -o $(EXEC)  -fopenmp

clean:
	$(RM) $(EXEC)
