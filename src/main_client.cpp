/*
 * Client for playing treasure hunt game in a multiplayer scenario...
 * (C) Rahul Ghose 2010-11
 * License: GNU Public License v3
 */
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cerrno>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <curses.h>
#include <signal.h>
#include "grid.h"

using namespace std;

#define LOCALHOST "localhost"
#define MAX_SIZE 20

int readline( int,char *,int );
void show_grid();
void refresh_grid();
void show_score(char[],int);
char translate_move( int move );
static void finish(int sig);
void show_grid_extended();

int P;	// number of Players that have joined
int	sd;
char *score;
int mNum;	// move number

int main( int argc, char *argv[] ) {
	
	char hostname[100];
	char msg[MAX_SIZE];
	struct sockaddr_in pin;
	struct hostent *hp;
	int id, x,y, port;
	
	strcpy(hostname, LOCALHOST);
	    
    char c;
	char *op;
	while ((c = getopt (argc, argv, "P:S:")) != -1)
    switch (c) {
		case 'P':
      		op = optarg;
      		port = strtol( op, NULL, 0 );
        	break;
        case 'S':
			op = optarg;
			strcpy( hostname, optarg );
			break;
      	case '?':
        	fprintf (stderr, "Unknown option `\\x%x'.\n", optopt);
        	return 1;
      	default: break;
    }
    
	/* go find out about the desired host machine */
	if ((hp = gethostbyname(hostname)) == 0) {
		fprintf( stderr,"could not reolve %s\n", hostname);
		exit(1);
	}

	/* fill in the socket structure with host information */
	memset(&pin, 0, sizeof(pin));
	pin.sin_family = AF_INET;
	pin.sin_addr.s_addr = ((struct in_addr *)(hp->h_addr))->s_addr;
	pin.sin_port = htons(port);

	/* grab an Internet domain socket */
	if ((sd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
		perror("socket");
		exit(1);
	}

	/* connect to PORT on HOST */
	if (connect(sd,(struct sockaddr *)  &pin, sizeof(pin)) == -1) {
		perror("connect");
		exit(1);
	}
	
	if( readline( sd, msg, MAX_SIZE ) < 0 ) {
		perror( "reading failed" );
		exit(1);
	}
	
	sscanf( msg, "%d%d%d%d", &id, &N, &x, &y );
	
	printf( "I am player no. %d on (%d, %d)\nGrid Size= %d \n", id+1,x,y,N );
	
	if( !allocate_grid()) {
		perror( "Grid allocation failed" );
		exit(1);
	}
	
	{
		char buff[MAX_SIZE];
		grid_recv(sd);
		readline( sd, buff, MAX_SIZE );
		P = strtol( buff,NULL, 0 );
	}
	
	char quit = 0;
	
	score = new char[P];
	memset( score, 0, sizeof P );
	
	(void) signal(SIGINT, finish);      /* arrange interrupts to terminate */
	initscr(); cbreak();
    keypad(stdscr, TRUE);  /* enable keyboard mapping */
    (void) nonl();         /* tell curses not to do NL->CR/NL on output */
    noecho();
    
    refresh_grid();

	while(!quit) {
		// read
		char move;
		
		
		printw( "Move: \n" );
		
		fd_set rfds;
		FD_ZERO(&rfds);
		FD_SET(0, &rfds);
		FD_SET(sd, &rfds);
		
		struct timeval tv;
		tv.tv_sec = 19;		// Not 20 secs. allow a bit of latency
		tv.tv_usec = 0;
		
		int ready = select(sd+1, &rfds, NULL, NULL, &tv);
		
		if( ready < 0 ) {	// Error...
				perror( "?" );
				break;
		} else if ( ready == 0 ) { // timeout
			char data[] = { 'X', 0 };
			printw( "No move\n"  );
			if( send( sd, data, 1, 0 ) < 0 ) {
				perror( "Server disconnected" );
				break;
			}
		} else {
			if(FD_ISSET(0, &rfds)) {
				move = translate_move( getch() );
//				while( (move=fgetc(stdin)) == '\n' );
				char data[] = { move, 0 };
				printw( "Sending %c\n", move );
				if( send( sd, data, 1, 0 ) < 0 ) {
					perror( "Server disconnected" );
					break;
				}
				if( move == 'Q' ) quit = 1;
			}
			if(FD_ISSET(sd, &rfds) || FD_ISSET(0, &rfds)) {
				recv( sd, msg, 1, 0 );				// recv. control word
				switch(msg[0]) {
					case 'G':
						if( grid_recv(sd) < 0 ) {
							perror( "Recving Grid" );
							quit=1;
						}
						if( recv( sd, score, P, 0 ) < 0 ) {
							perror( "Recvng Score" );
							quit=1;
						}
					break;
					case 'Z':
						quit = 1;
					break;
					default:
					break;
				} 
			}
		}
		
		mNum++;
		refresh_grid();
	}
	
	
	return 0;
}

int readline( int fd, char *buf, int nbytes ) {
	int numread = 0;
	int returnval;
	
	while( numread < nbytes - 1 ) {
		returnval = recv( fd, buf+numread, 1, 0 );
		if((returnval == -1 ) && (errno == EINTR) )
			continue;
		if((returnval == 0 ) && (numread == 0 ))
			continue;
		if(returnval == 0 )
			break;
		if( returnval == -1 )
			return -1;
		numread++;
		if( buf[numread-1] == '\n' ) {
			buf[numread-1] = '\0';
			return numread;
		}
	}
	errno = EINVAL;
	return -1;
}

void refresh_grid() {
	move( 0, 0 );
	printw( "%d. ", mNum );
	show_score(score,P);
	printw( "\n\r" );
	show_grid_extended();
	refresh();
}


void show_grid_extended() {
	int i,j;
	for( i=0; i<N; i++ ) {
		for( j=0; j<N; j++ )
			printw( "%c", convert(grid[i][j]) );
		printw("\n" );
	}
}

void show_score(char score[], int N) {
	for( int i=0; i<N; i++ ) {
		printw( "%d ", score[i] );
	}
}

char translate_move( int move ) {
	switch( move ) {
		case KEY_UP:
		//case 'W':
		//case 'w':
			return 'N';
		case KEY_LEFT:
		//case 'A':
		//case 'a':
			return 'W';
		case KEY_RIGHT:
		//case 'd':
		//case 'D':
			return 'E';
		case KEY_DOWN:
		//case 'S':
		//case 's':
			return 'S';
		case KEY_CANCEL:
			return 'Q';
		default:
			return 0;
	}
}

static void finish(int sig) {
    endwin();
	
	free_grid();
	close(sd);

    exit(0);
}
