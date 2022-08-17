a.out: main.c \
       src/ncrm_journalEntries.c \
	   src/ncrm_queue.c
	g++ -Wall -g -ggdb -Iinclude/ \
		-x c main.c \
		-x c src/ncrm_journalEntries.c \
		-x c src/ncrm_queue.c \
		-lncurses -lpanel -lpthread
