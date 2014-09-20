#ifndef _GRID_H
#define _GRID_H
#endif

#define GRID_EMPTY     0
#define GRID_TREASURE -1

#define TRUE 1
#define FALSE 0

extern int N;
extern int M;
extern char **grid;

extern int allocate_grid(void);
extern void free_grid();
extern int place_grid( int, int, int );
extern void reset_grid();
extern void populate_grid();
extern int grid_send(int);
extern int grid_play( int, char );
extern int grid_recv( int );
extern void grid_boot( int );
extern char convert( char );
extern void show_grid();
