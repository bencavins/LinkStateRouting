CC = gcc
FLAGS = -g -Wall -Wextra -O2

all: routed_LS

routed_LS: routed_LS.o vector.o hashmap.o
	$(CC) $(FLAGS) $^ -o $@

routed_LS.o: routed_LS.c 
	$(CC) $(FLAGS) -c $< 

vector.o: vector.c vector.h
	$(CC) $(FLAGS) -c $<

hashmap.o: hashmap.c hashmap.h
	$(CC) $(FLAGS) -c $<

clean:
	rm -f routed_LS
	rm -f *.o
	rm -f *~
	rm -f A-log.txt
	rm -f B-log.txt
	rm -f C-log.txt
	rm -f D-log.txt
	rm -f E-log.txt
	rm -f F-log.txt