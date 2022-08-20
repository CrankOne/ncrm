a.out: main.c \
       src/ncrm_journalEntries.c \
	   src/ncrm_queue.c \
	   src/ncrm_model.c
	g++ -Wall -g -ggdb -Iinclude/ \
		-x c main.c \
		-x c src/ncrm_journalEntries.c \
		-x c src/ncrm_queue.c \
		-x c src/ncrm_model.c \
		-lncurses -lpanel -lpthread -lzmq -lmsgpackc
