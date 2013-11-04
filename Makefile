CC = gcc
FLAGS = -g -Wall -Wextra -O2

all: routed_LS

routed_LS: routed_LS.o
	$(CC) $(FLAGS) $^ -o $@

routed_LS.o: routed_LS.c
	$(CC) $(FLAGS) -c $< 

clean:
	rm -f routed_LS
	rm -f *.o
	rm -f *~
	rm -f A-log.txt
	rm -f B-log.txt