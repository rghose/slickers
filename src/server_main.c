/*
 * Server for playing treasure hunt game in a multiplayer scenario...
 * (C) Rahul Ghose 2010-11
 * License: GNU Public License v3
 */
 
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <signal.h>
#include <pthread.h>    /* POSIX Threads */
#include <sys/select.h>
#include <sys/time.h>
#include <time.h>
#include <sys/stat.h>
#include "grid.h"

#define BACKLOG 10	 // how many pending connections queue will hold
#define MAX_CLIENTS 20
#define WAIT_THRESHOLD 20	// Seconds
#define MAX_MSG 20		// max. chars for messages between client and server...

#ifdef DEBUG
#define _DEBUG
#endif

typedef struct str_thdata {
    int thread_no;
    int sock;
    int x,y;
} thdata;

int sockfd;			// listen on sock_fd
int num_threads;	// same as number of games...

pthread_mutex_t mut[MAX_CLIENTS];// = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t gridd = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t m_rem = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t cond[MAX_CLIENTS];// = PTHREAD_COND_INITIALIZER;
pthread_cond_t grid_done = PTHREAD_COND_INITIALIZER;
char begin[MAX_CLIENTS];
char end[MAX_CLIENTS];
char force[MAX_CLIENTS];
char score[MAX_CLIENTS];

thdata tdata[MAX_CLIENTS];

char grid_generated;
int Mrem;					// Number of treasure remaining...

typedef void (*fx)(void *);
void exit_main();
void *service_client( void * );
void usage();
void score_send( int );
void broadcast();
void quit_send( int );
void signal_mrem( int );


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

// get sockaddr, IPv4 or IPv6:
void *get_in_addr(struct sockaddr *sa) {
	if (sa->sa_family == AF_INET) return &(((struct sockaddr_in*)sa)->sin_addr);
	return &(((struct sockaddr_in6*)sa)->sin6_addr);
}

int main( int argc, char *argv[] ) {
	
	int new_fd;		// new connection on new_fd		
	struct addrinfo hints, *servinfo, *p;
	struct sockaddr_storage their_addr; // connector's address information
	int yes=1;
	char s[INET6_ADDRSTRLEN];
	int rv;
	char port[6];
	pthread_t thread[MAX_CLIENTS];
	
	int t_thresh;
	char err;
	
	if( argc < 5 ) {
		usage();
		exit(0);
	}
	
	char c;
	char *op;
	while ((c = getopt (argc, argv, "N:M:P:")) != -1)
    switch (c) {
		case 'P':
			op = optarg;
			strcpy( port, op );
			break;
		case 'N':
      		op = optarg;
      		N = strtol( op, NULL, 0 );
        	break;
      	case 'M':
      		op = optarg;
      		M = strtol( op, NULL, 0 );
        	break;
      	case '?':
        	fprintf (stderr, "Unknown option character `\\x%x'.\n", optopt);
        	return 1;
      	default:
      		perror( "No values of grid size mentioned" );
        	return 1;
    }
    
    if( M >= N*N ) {
    	fprintf( stderr, "Invalid number of treasures %d for grid of size %d x %d\n",M,N,N );
    	return 2;
	}
	if( !allocate_grid() ) {
		#ifdef _DEBUG
		perror( "Could not allocate grid" );
		#endif
		return 3;
	}
	
	atexit( exit_main );

	memset(&hints, 0, sizeof hints);
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_PASSIVE; // use my IP

	if ((rv = getaddrinfo(NULL, port, &hints, &servinfo)) != 0) {
		#ifdef _DEBUG
		fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
		#endif
		exit(1);
	}

	// loop through all the results and bind to the first we can
	for(p = servinfo; p != NULL; p = p->ai_next) {
		if ((sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1) {
			#ifdef _DEBUG
			perror("server: socket");
			#endif			
			continue;
		}
		if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)) == -1) {
			#ifdef _DEBUG
			perror("setsockopt");
			#endif
			exit(1);
		}
		if (bind(sockfd, p->ai_addr, p->ai_addrlen) == -1) {
			close(sockfd);
			#ifdef _DEBUG
			perror("server: bind");
			#endif
			continue;
		}
		break;
	}

	if (p == NULL)  {
		#ifdef _DEBUG
		fprintf(stderr, "server: failed to bind\n");
		#endif
		exit(2);
	}

	freeaddrinfo(servinfo); // all done with this structure

	if (listen(sockfd, BACKLOG) == -1) {
		#ifdef _DEBUG
		perror("listen");
		#endif
		exit(1);
	}
	
	while(1) {
		
		printf("server: waiting for connections...\n");

		Mrem = M;
		grid_generated = 0;
		num_threads = 0;
		memset( begin, 0, sizeof begin );
		memset( end, 0, sizeof end );
		memset( force, 0, sizeof force );
		memset( score, 0, sizeof score );
		reset_grid();
	
		time_t tbefore, tafter;
		
		err = 0;
		t_thresh = WAIT_THRESHOLD;
		
		fd_set rfds;
        struct timeval tv;
        int ready;
        
		FD_ZERO(&rfds);
		FD_SET(sockfd, &rfds);

		while( t_thresh > 0 && num_threads < MAX_CLIENTS ) {  // main accept() loop
		
			tv.tv_sec = t_thresh;
			tv.tv_usec = 0;
			
			if( num_threads == 0 ) ready = select(sockfd+1, &rfds, NULL, NULL, NULL);
			else {
				#ifdef _DEBUG
				printf( "Time left: %d\n", t_thresh );
				#endif
				ready = select(sockfd+1, &rfds, NULL, NULL, &tv);
			}
       		
			if( num_threads == 0 ) { tbefore = time(NULL); }
       	
			if( ready < 0 ) {	// Error...
				perror( "?" );
				break;
			} else if ( ready == 0 ) { // timeout
				if( num_threads > 0 ) break;
				else {
					printf( "No clients have connected" );
					exit(0);
				}
			}
			else {
				if(FD_ISSET(sockfd, &rfds)) {
					int sin_size;
					sin_size = sizeof their_addr;
					new_fd = accept(sockfd, (struct sockaddr *)&their_addr, (socklen_t *)&sin_size);
				}
		
				if (new_fd == -1) {
					#ifdef _DEBUG
					perror("poll file descriptor was invalid.");
					#endif
					continue;
				}

				inet_ntop(their_addr.ss_family, get_in_addr((struct sockaddr *)&their_addr), s, sizeof s);
				#ifdef _DEBUG
				printf("server: got connection from %s\n", s);
				#endif
				
				// Generate and Send client info...
				char msg[MAX_MSG];
				int x,y;
				srandom(time(NULL));
				do{
					x = random()%N;
					y = random()%N;
				} while( !place_grid( x,y, num_threads+1 ));
				int t= sprintf( msg, "%d %d %d %d\n", num_threads,N,x,y );
				send(new_fd, msg, t, 0);	// send... ID and pos.
				tdata[num_threads].thread_no = num_threads;
				tdata[num_threads].sock = new_fd;
				tdata[num_threads].x = x;
				tdata[num_threads].y = y;
				pthread_mutex_init( &(mut[num_threads]), NULL);
				pthread_cond_init( &(cond[num_threads]), NULL);
				pthread_create( &thread[num_threads], NULL, (&service_client), &tdata[num_threads]);
				num_threads++;
			}
		
			tafter = time(NULL);
			t_thresh -= difftime(tafter,tbefore);
		}// end of while...
		
		// Connections made... 20 clients or time expired... so start game...
		{
			populate_grid();
			int i;
			char msg[2];
			int l = sprintf( msg, "%d\n", num_threads );
			for( i=0; i<num_threads; i++ ) {
				if( grid_send( tdata[i].sock ) ) {
					send( tdata[i].sock, msg, l, 0 );	// send total players
				} else {								// Could not send data... client disconnected...
					pthread_mutex_lock(&mut[i]);
					begin[i]= 1;
					force[i] = 2;						// Special Force applied
					end[i] = 1;
					pthread_mutex_unlock(&mut[i]);
					grid_boot(i+1);						// grid players are 1-offset
				}
			}
			grid_generated = 1;
			pthread_cond_broadcast( &grid_done );
		}
		
		#ifdef _DEBUG
		printf( "Now play game with %d clients!\n", num_threads );
		#endif
		
		//if( !err ) 
		{
		#ifdef TEST
			// TEST
			{
				int i;
				for( i=0; i<num_threads; i++ ) {
					pthread_mutex_lock(&mut[i]);
					begin[i] = 1;
					end[i] = 1;
					pthread_mutex_unlock(&mut[i]);
					pthread_cond_broadcast(&cond[i]);
				}
			}
		#else
	
			// Now monitor all sockets... and wake up corresponding thread when data is available...
			fd_set rfds;
       		struct timeval tv;
	       	int ready;
	        
			while(1) {
				/*
				 * Listen to all the active sockets.. this is actually all
				 * the active clients that have to be checked for I/O on
				 * their corresponding socket...
				 * Might as well check for any one left
				 */
				int i;
				int ac_thread = 0;
				int max_sock = 0;
				FD_ZERO(&rfds);
				for( i=0; i<num_threads; i++ ) { 
					pthread_mutex_lock(&mut[i]);
					if( !end[i] )	{
						if( tdata[i].sock > max_sock ) max_sock = tdata[i].sock;
						FD_SET( tdata[i].sock, &rfds );
						ac_thread++;
					}
					pthread_mutex_unlock(&mut[i]);
				}
				if( ac_thread == 0 ) {	// no more active clients...
					for( i=0; i<num_threads; i++ ) {
						pthread_mutex_lock(&mut[i]);
						begin[i] = 1;
						end[i] = 1;							// kill all threads...
						force[i] = 1;
						pthread_mutex_unlock(&mut[i]);
						pthread_cond_broadcast(&cond[i]);	// wake up...
					}
					break;
				}
				
				tv.tv_sec = WAIT_THRESHOLD;
				tv.tv_usec = 0;
				
	       		ready = select(max_sock+1, &rfds, NULL, NULL, &tv);
	       		if( ready <= 0 ) {								// Error...
	       			perror( (ready==0) ? "timeout" : "??");
	       			for( i=0; i<num_threads; i++ ) {
						pthread_mutex_lock(&mut[i]);
						begin[i] = 1;
						end[i] = 1;							// kill all threads...
						force[i] = 2 - (ready==0);
						pthread_mutex_unlock(&mut[i]);
						pthread_cond_broadcast(&cond[i]);	// wake up...
					}
					break;
				} else {						// no prob.
					for( i=0; i<num_threads; i++ ) {
						if(FD_ISSET(tdata[i].sock, &rfds)) { // All file descriptors must be checked...
							pthread_mutex_lock(&mut[i]);
							begin[i]=1;						// process once...
							pthread_mutex_unlock(&mut[i]);
							pthread_cond_broadcast(&cond[i]);	// wake up...
						}
					}
				}
			}
		#endif
		}
	
		#ifdef _DEBUG
		printf( "Ending all clients... \n" );
		#endif
		// Ending all threads...
		{
			int i, n = num_threads;
			for( i=0; i<n; i++,num_threads-- ) {
				pthread_join( thread[i], NULL );
				pthread_mutex_destroy( &mut[i] );
				pthread_cond_destroy( &(cond[i]));
			}
		}
		
	} // One game session ended...
	
	#ifdef _DEBUG
	printf( "ERROR: This should never happen... \nLine : %d\n", __LINE__ );
	#endif
	exit(0);
}

void exit_main() {
	close(sockfd);
	free_grid();
}


/*
 * Acts as the thread function.
 */
void* service_client( void *data ) {
	
	thdata *d;
	d = (thdata * )data;
	char rcv_msg[MAX_MSG];
	int mNum = 0;
	
	#ifdef _DEBUG
	printf( "[SC %d] Started... \n", d->thread_no );
	#endif
	
	pthread_mutex_lock(&gridd);
	if( !grid_generated )
		pthread_cond_wait(&grid_done,&gridd );
	pthread_mutex_unlock(&gridd);
	
	do {
		pthread_mutex_lock(&mut[d->thread_no]);
		while( begin[d->thread_no] == 0 ) {
			#ifdef _DEBUG
			printf( "[SC %d] About to process request\n", d->thread_no );
			#endif
			/*
			 * Handle player crashes :-->
			 * Use a timed wait on the socket... if the main thread does not wake up this thread
			 * for 20 seconds, the thread which was waiting for the condition (set by main thread)
			 * will time out. Timeout will result in aborting of game by this user. The user will 
			 * not be able to play again.
			 */
			struct timeval now;
			struct timespec timeout;
			gettimeofday(&now,NULL);
			timeout.tv_sec = now.tv_sec + 20;
			timeout.tv_nsec = now.tv_usec * 1000;
			if( ETIMEDOUT == pthread_cond_timedwait(&cond[d->thread_no], &mut[d->thread_no], &timeout))  {
				begin[d->thread_no] = 1;
				force[d->thread_no] = 1;
				end[d->thread_no] = 1;
				/*
				 * Throw out player... player is not sending any data...
				 * must be disconnected... OR faulty client
				 */
				quit_send( d->sock );
				#ifdef _DEBUG
				printf( "[SC %d] Client dc\n", d->thread_no );
				#endif
				break;
			}
		}
		pthread_mutex_unlock(&mut[d->thread_no]);
		
		#ifdef _DEBUG
		printf( "[SC %d] Service...\n", d->thread_no );
		#endif
		if( !force[d->thread_no] ) {
			char bcast = 1;
			
			#ifdef _DEBUG
			printf( "[SC %d] About to read data...\n", d->thread_no );
			#endif
			
			if( recv( d->sock, rcv_msg, 1, 0 ) > 0 ) {
				switch( rcv_msg[0] ) {
					case 'N':
					case 'S':
					case 'E':
					case 'W':
						pthread_mutex_lock(&mut[d->thread_no]);
						if( grid_play( d->thread_no+1, rcv_msg[0] ) == 1 ) {
							score[d->thread_no]++;
							pthread_mutex_lock(&m_rem);
							Mrem--;
							if( Mrem == 0 ) {
								signal_mrem(d->thread_no);
							}
							pthread_mutex_unlock(&m_rem );
						}
						mNum++;
						#ifdef _DEBUG
						printf( "[SC %d] %d. \n", d->thread_no ,mNum );
						#endif
						pthread_mutex_unlock(&mut[d->thread_no]);
					break;
					case 'Q':
						pthread_mutex_lock(&mut[d->thread_no]);
						end[d->thread_no] = 1;
						grid_boot( d->thread_no+1 );
						pthread_mutex_unlock(&mut[d->thread_no]);
					break;
					default:
						bcast = 0;
						//send( d->sock, "P", 1, 0 );
					break;
				}
			
				#ifdef _DEBUG
				printf( "[SC %d] Recieved message and now sending %c...\n", d->thread_no, rcv_msg[0] );
				show_grid();
				#endif
			
				if( bcast )
					broadcast();
			} else {
				pthread_mutex_lock(&mut[d->thread_no]);
				end[d->thread_no] = 1;
				grid_boot( d->thread_no+1 );
				pthread_mutex_unlock(&mut[d->thread_no]);
			}// end of recv 
		}// end of unforced block...
		
		pthread_mutex_lock(&mut[d->thread_no]);
		begin[d->thread_no]=0;
		if( end[d->thread_no] == 1 ) {				 // Some one wants me dead...
			if( force[d->thread_no] < 2 )
				quit_send( tdata[d->thread_no].sock );
			pthread_mutex_unlock(&mut[d->thread_no]);
			break;
		}
		pthread_mutex_unlock(&mut[d->thread_no]);
	}while(1);
	
	#ifdef _DEBUG
	printf( "[SC %d] Ending...\n", d->thread_no );
	#endif
	
	close(d->sock);
	return 0;
}

void usage() {
	printf( "Play game with -N x -M y as parameters\n" );
	printf( "In this case the grid is 'x' by 'x' units in size\n" );
	printf( "The 'y' is the total number of treasure spots\n" );
}

void score_send( int sock ) {
	send(sock, score, num_threads, 0);
}

void broadcast() {
	int i;
	for( i=0; i<num_threads; i++ ) {
		send( tdata[i].sock, "G", 1, 0 );
		grid_send( tdata[i].sock );
		score_send( tdata[i].sock );
	}
}

void signal_mrem( int cp ) {
	int i;
	end[cp] = 1;
	for( i=0; i<num_threads; i++ ) if( i != cp ) {
		pthread_mutex_lock(&mut[i]);
		end[i] = 1;
		force[i] = 1;
		pthread_mutex_unlock(&mut[i]);
	}
}

void quit_send( int sock ) {
	send( sock, "Z", 1, 0 );
}
