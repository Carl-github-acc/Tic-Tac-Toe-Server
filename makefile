FLAGS = -Wall -pedantic -std=gnu99

all: $(OBJECTS)
	gcc $(FLAGS) trivial.c -o trivial -lpthread
	gcc $(FLAGS) scores.c -o scores -lpthread
	gcc $(FLAGS) serv.c -o serv -lpthread
debug: $(OBJECTS)
	gcc $(FLAGS) -g trivial.c -o trivial -lpthread
	gcc $(FLAGS) -g scores.c -o scores -lpthread
	gcc $(FLAGS) -g serv.c -o serv -lpthread
