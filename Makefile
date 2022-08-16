a.out:
	g++ -Wall -g -ggdb -x c -Iinclude/ src/ncrm_journalEntries.c -x c main.c -lncurses -lpanel
