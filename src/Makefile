CC := g++
CX := g++
OBJS := ../bin/grid.o
OUTS := ../bin/server_main
OUTC := ../bin/client_main

all: server_main.c main_client.cpp ${OBJS}
	${CC} -Wall server_main.c ${OBJS} -lpthread -o ${OUTS}
	${CX} main_client.cpp ${OBJS} -o ${OUTC} -lncurses

${OBJS} : grid.cpp
	${CX} -Wall -c grid.cpp -o ${OBJS}

server: server_main.c ${OBJS}
	${CC} -Wall server_main.c ${OBJS} -lpthread -o ${OUTS}

client: main_client.cpp ${OBJS}
	${CX} main_client.cpp grid.o -o ${OUTC} -lncurses

clean:
	rm ../bin/*.o 
	rm ../bin/*

debug:
	${CC} -Wall server_main.c ${OBJS} -lpthread -o ${OUTS} -DDEBUG -g
	${CX} main_client.cpp ${OBJS} -o ${OUTC} -DDEBUG -g -lncurses
test:
	${CC} -Wall server_main.c ${OBJS} -lpthread -o {OUTS} -DTEST


