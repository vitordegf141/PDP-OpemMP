FLAGS=-O3 -Wall

RM=rm -f

EXEC=sokoban
CC=gcc

all: $(EXEC)

$(EXEC): sokoban.c
	$(CC) $(FLAGS) $(EXEC).c -o $(EXEC)2  -fopenmp

clean:
	$(RM) $(EXEC)2
