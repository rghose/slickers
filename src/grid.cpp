#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <unistd.h>
#include <vector>
#include <time.h>
#include <sys/types.h>
#include <sys/socket.h>

using namespace std;

#define GRID_EMPTY     0
#define GRID_TREASURE -1

#define TRUE 1
#define FALSE 0

int N;				// Grid size is NxN
int M;				// total number of treasures...
char **grid;
vector<pair<int,pair<int,int> > > Players;
//vector<pair<int,int> > Treasures;

char convert( char x ) {
	switch( x ) {
		case GRID_TREASURE:
		return '*';
		case GRID_EMPTY:
		return '-';
		default:
		return '0'+x;
	}
}

void show_grid() {
	int i,j;
	for( i=0; i<N; i++ ) {
		for( j=0; j<N; j++ )
			printf( "%c", convert(grid[i][j]) );
		printf("\n" );
	}
}


int allocate_grid() {
	if( N == 0 ) return 0;
	if( (grid = (char **) malloc( sizeof(char *) * N )) == NULL ) return 0;
	for( int i = 0; i<N; i++ ) if( (grid[i] = (char *) malloc( sizeof(char) * N )) == NULL ) return 0;
	return 1;
}

void free_grid() {
	for( int i = 0; i<N; i++ ) free( grid[i] );
	free( grid );
}

int place_grid( int x,int y, int g ) {
	if( grid[x][y] != GRID_EMPTY ) return FALSE;
	grid[x][y] = g;
	switch( g ) {
	case GRID_TREASURE:
	{
//		pair<int,int> a(x,y);
//		Treasures.push_back(a);
	}
	break;
	case GRID_EMPTY:
	break;
	default:
	{
		pair<int,int> a(x,y);
		pair <int,pair<int,int> > b(g,a);
		Players.push_back( b );
		break;
	}
	}
	return TRUE;
}

void reset_grid() {
	for( int i=0; i<N; i++ ) memset( grid[i], GRID_EMPTY, N );
	while( !Players.empty() ) Players.pop_back();
	//while( !Treasures.empty() ) Treasures.pop_back();
}

void populate_grid() {
	srandom(time(NULL));
	for( int i=0; i<M; i++ ) {
		int x,y;
		do {
			x = random()%N;
			y = random()%N;
		} while(!place_grid( x,y, GRID_TREASURE ));
	}
}

int grid_recv( int sock ) {
	for( int i=0; i<N; i++ ) {
		if( recv(sock, grid[i], N, 0) <= 0 ) 
			return FALSE;
	}
	return TRUE;
}

int grid_send( int sock ) {
	for( int i=0; i<N; i++ ) {
		if( send(sock, grid[i], N, MSG_NOSIGNAL) < 0 ) 
			return FALSE;
	}
	return TRUE;
}

int grid_play( int player, char move ) {
	
	int x = Players[player-1].second.first;
	int y = Players[player-1].second.second;
	int retVal = 0;
	int xp,yp;
	
	xp = x;						// save previous position for future
	yp = y;
	grid[x][y] = GRID_EMPTY;
	switch( move ) {
		case 'W': 
		if( y > 0 ) y--;
		break;
		case 'E':
		if( y < N-1 ) y++;
		break;
		case 'N':
		if( x > 0 ) x--;
		break;
		case 'S':
		if( x < N-1 ) x++;
		break;
	}
	if( grid[x][y] == GRID_TREASURE ) {
		retVal = 1;
	}
	else if( grid[x][y] != GRID_EMPTY ) {
		grid[xp][yp] = player;		// restore player to previous position
		return 0;
	}
	grid[x][y] = player;
	Players[player-1].second.first = x;
	Players[player-1].second.second = y;
	return retVal;
}

void grid_boot( int player ) {
	int x = Players[player-1].second.first;
	int y = Players[player-1].second.second;
	
	grid[x][y] = GRID_EMPTY;
	
	Players[player-1].second.first = -1;
	Players[player-1].second.second = -1;
}
