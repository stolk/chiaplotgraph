// grapher.c
//
// by Abraham Stolk.

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <math.h>
#include <time.h>
#include <unistd.h>
#include <string.h>
#include <inttypes.h>
#include <signal.h>
#include <termios.h>
#include <errno.h>
#include <sys/ioctl.h>

#include "grapher.h"


#define HALFBLOCK "▀"		// Uses Unicode char U+2580
#define HALFBLOCKV "▌"		// Uses Unicode char U+258C

static int termw = 0, termh = 0;

int imw = 0;
int imh = 0;
uint32_t* im = 0;
char* overlay = 0;

char postscript[256];

int grapher_resized = 1;

static int double_col = 0;


static void get_terminal_size(void)
{
	struct winsize tmp;
	ioctl(STDOUT_FILENO, TIOCGWINSZ, &tmp);
	termw = tmp.ws_col;
	termh = tmp.ws_row;
}


static void setup_image_double_row(void)
{
	if (im) free(im);
	if (overlay) free(overlay);

	imw = termw;
	imh = 2 * (termh-1);
	const size_t sz = imw * imh * 4;
	im = (uint32_t*) malloc(sz);
	memset( im, 0x00, sz );

	overlay = (char*) malloc( imw * (imh/2) );
	memset( overlay, 0x00, imw * (imh/2) );
}


static void setup_image_double_col(void)
{
	if (im) free(im);
	if (overlay) free(overlay);

	imw = 2 * termw;
	imh = (termh-1);
	const size_t sz = imw * imh * 4;
	im = (uint32_t*) malloc(sz);
	memset( im, 0x00, sz );

	overlay = (char*) malloc( imw * imh );
	memset( overlay, 0x00, imw * imh );
}


static void sigwinchHandler(int sig)
{
	grapher_resized = 1;
}


static void print_image_double_row( int w, int h, unsigned char* data, char* overlay )
{
	if ( h & 1 )
		h--;
	const int linesz = 32768;
	char line[ linesz ];

	for ( int y = 0; y<h; y += 2 )
	{
		const unsigned char* row0 = data + (y + 0) * w * 4;
		const unsigned char* row1 = data + (y + 1) * w * 4;
		line[0] = 0;
		for ( int x = 0; x<w; ++x )
		{
			char overlaychar = overlay ? *overlay++ : 0;
			// foreground colour.
			strncat( line, "\x1b[38;2;", sizeof(line) - strlen(line) - 1 );
			char tripl[80];
			unsigned char r = *row0++;
			unsigned char g = *row0++;
			unsigned char b = *row0++;
			unsigned char a = *row0++;
			if ( overlaychar ) r = g = b = a = 0xff;
			snprintf( tripl, sizeof(tripl), "%d;%d;%dm", r,g,b );
			strncat( line, tripl, sizeof(line) - strlen(line) - 1 );
			// background colour.
			strncat( line, "\x1b[48;2;", sizeof(line) - strlen(line) - 1 );
			r = *row1++;
			g = *row1++;
			b = *row1++;
			a = *row1++;
			if ( overlaychar ) r = g = b = a = 0x00;
			if ( overlaychar )
				snprintf( tripl, sizeof(tripl), "%d;%d;%dm%c", r,g,b,overlaychar );
			else
				snprintf( tripl, sizeof(tripl), "%d;%d;%dm" HALFBLOCK, r,g,b );
			strncat( line, tripl, sizeof(line) - strlen(line) - 1 );
		}
		strncat( line, RESETALL, sizeof(line) - strlen(line) - 1 );
		if ( y == h - 1 )
			printf( "%s", line );
		else
			puts( line );
	}
}


static void print_image_double_col( int w, int h, unsigned char* data, char* overlay )
{
	if ( w & 1 )
		w--;
	const int linesz = 32768;
	char line[ linesz ];

	for ( int y = 0; y<h; y += 1 )
	{
		const unsigned char* row = data + y*w*4;
		line[0] = 0;
		for ( int x = 0; x<w; x+=2 )
		{
			char overlaychar = overlay ? *overlay++ : 0;
			// foreground colour.
			strncat( line, "\x1b[38;2;", sizeof(line) - strlen(line) - 1 );
			char tripl[80];
			unsigned char r = *row++;
			unsigned char g = *row++;
			unsigned char b = *row++;
			unsigned char a = *row++;
			if ( overlaychar ) r = g = b = a = 0xff;
			snprintf( tripl, sizeof(tripl), "%d;%d;%dm", r,g,b );
			strncat( line, tripl, sizeof(line) - strlen(line) - 1 );
			// background colour.
			strncat( line, "\x1b[48;2;", sizeof(line) - strlen(line) - 1 );
			r = *row++;
			g = *row++;
			b = *row++;
			a = *row++;
			if ( overlaychar ) r = g = b = a = 0x00;
			if ( overlaychar )
				snprintf( tripl, sizeof(tripl), "%d;%d;%dm%c", r,g,b,overlaychar );
			else
				snprintf( tripl, sizeof(tripl), "%d;%d;%dm" HALFBLOCKV, r,g,b );
			strncat( line, tripl, sizeof(line) - strlen(line) - 1 );
		}
		strncat( line, RESETALL, sizeof(line) - strlen(line) - 1 );
		if ( y == h - 1 )
			printf( "%s", line );
		else
			puts( line );
	}
}



int grapher_init( int use_double_columns )
{
	double_col = use_double_columns;

	if ( system("tty -s 1> /dev/null 2> /dev/null") )
		return -1;

	// Listen to changes in terminal size
	struct sigaction sa;
	sigemptyset( &sa.sa_mask );
	sa.sa_flags = 0;
	sa.sa_handler = sigwinchHandler;
	if ( sigaction( SIGWINCH, &sa, 0 ) == -1 )
		perror( "sigaction" );

	return 0;
}


void grapher_adapt_to_new_size(void)
{
	printf(CLEARSCREEN);
	get_terminal_size();
	if ( double_col )
		setup_image_double_col();
	else
		setup_image_double_row();
	grapher_resized = 0;
}


void grapher_update( void )
{
	printf( CURSORHOME );
	if ( double_col )
		print_image_double_col( imw, imh, (unsigned char*) im, overlay );
	else
		print_image_double_row( imw, imh, (unsigned char*) im, overlay );

	printf( "%s", postscript );
	fflush( stdout );
}


void grapher_exit(void)
{
	free(im);
	printf( CLEARSCREEN );
}


