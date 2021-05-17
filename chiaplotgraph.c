// chiaplotgraph.c
// 
// (c)2021 by Abraham Stolk.
// XCH Donations: xch1zfgqfqfdse3e2x2z9lscm6dx9cvd5j2jjc7pdemxjqp0xp05xzps602592


// Starting phase 1/4: Forward Propagation into tmp files... Sat May 15 19:14:40 2021

// Time for phase 1 = 8297.374 seconds. CPU (170.770%) Sat May 15 15:35:41 2021

// Starting phase 2/4: Backpropagation into tmp files... Sat May 15 15:35:41 2021

// Starting phase 3/4: Compression from tmp files into "/mnt/scratch1/plot-k32-2021-05-15-13-17-6f4bafcffbfc23e6031b4408e72175994be47153df8ae29ac01e51944fec214d.plot.2.tmp" ... Sat May 15 16:42:52 2021

// Time for phase 3 = 8058.557 seconds. CPU (98.350%) Sat May 15 18:57:11 2021

// Starting phase 4/4: Write Checkpoint tables into "/mnt/scratch1/plot-k32-2021-05-15-13-17-6f4bafcffbfc23e6031b4408e72175994be47153df8ae29ac01e51944fec214d.plot.2.tmp" ... Sat May 15 18:57:11 2021

// Time for phase 4 = 604.377 seconds. CPU (99.150%) Sat May 15 19:07:15 2021

// Copy time = 439.956 seconds. CPU (36.830%) Sat May 15 19:14:36 2021

// Starting plotting progress into temporary dirs: /mnt/scratch0 and /mnt/scratch0

// Renamed final file from "/mnt/farm2/plots/plot-k32-2021-05-16-21-46-c6690ea28d9f8ef79e10b7d4e826bfd3431532220c0b97737f7ce27f302be925.plot.2.tmp" to "/mnt/farm2/plots/plot-k32-2021-05-16-21-46-c6690ea28d9f8ef79e10b7d4e826bfd3431532220c0b97737f7ce27f302be925.plot"


#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <assert.h>
#include <math.h>
#include <fcntl.h>
#include <sys/select.h>
#include <sys/ioctl.h>
#include <sys/statvfs.h>
#include <time.h>
#include <limits.h>
#include <errno.h>
#include <error.h>
#include <string.h>
#include <termios.h>
#include <inttypes.h>

#include "grapher.h"


#define MAXLINESZ		1024

#define WAIT_BETWEEN_SELECT_US	500000L

#define	MAXHIST			512		// Record up to 512 plots from the logs.

enum stage_t
{
	ST_FORW=0,
	ST_BACK,
	ST_COMP,
	ST_WRIT,
	ST_COPY,
	NUMSTAGES
};

#define NUMWATCH		(NUMSTAGES+1 + 2)
static const char* watchphrases[NUMWATCH] =
{
	// Watch the phrases that mark a stage transition.
	"Starting phase 1/4:",
	"Starting phase 2/4:",
	"Starting phase 3/4:",
	"Starting phase 4/4:",
	"Time for phase 4 = ",
	"Copy time = ",
	// Watch the phrases that tell us where the drives are.
	"Starting plotting ",
	"Renamed final file from ",
};

static char dirname_temp[256];
static char dirname_farm[256];


typedef struct run
{
	time_t	stamps[6];
} run_t;


typedef struct hist
{
	run_t runs[MAXHIST];
	int head;
	int tail;
} hist_t;


static uint8_t stagecolours[ NUMSTAGES ][ 3 ] =
{
	0xe9,0xeb,0x2e,
	0x50,0xb4,0x90,
	0x2e,0x5e,0xc0,
	0xf7,0x47,0x0e,
	0xaa,0xaa,0xaa,
};


static int numl;		// The number of logs that we are monitoring, given on the command line.

static const char** lognames=0;	// The names of the log files that we are monitoring.

static FILE** logfiles;		// The logfiles that we are reading.

static hist_t* hist;		// History, per log file.


static time_t newest_stamp=0;	// The stamp of the latest entry.

static time_t refresh_stamp=0;	// When did we update the image, last?

static struct termios orig_termios;



static void init_for_logcount( int logcount, const char* names[] )
{
	numl = logcount;
	assert(numl);

	lognames	= (const char**) calloc( numl, sizeof(const char*) );
	logfiles	= (FILE**) calloc( numl, sizeof(FILE*) );
	hist		= (hist_t*) calloc( numl, sizeof(hist_t) );

	for ( int i=0; i<logcount; ++i )
		lognames[ i ] = names[ i ];

	for ( int i=0; i<logcount; ++i )
	{
		logfiles[ i ] = fopen( lognames[ i ], "rb" );
		if ( !logfiles[ i ] )
		{
			fprintf( stderr, "Failed to open '%s' for reading.\n", lognames[i] );
			error( EXIT_FAILURE, errno, "fopen() failed" );
		}
#if 0		// No need for non blocking IO.
		const int fd = fileno( f_log );
		assert( fd );
		int flags = fcntl( fd, F_GETFL, 0 );
		fcntl( fd, F_SETFL, flags | O_NONBLOCK );
#endif
		fprintf( stderr,"Opened %s\n", lognames[i] );
	}
}


static run_t* current_run( int lognr )
{
	assert(lognr>=0 && lognr<numl);
	hist_t* h = hist+lognr;
	if ( h->head == h->tail )
		return 0;
	int cur = h->tail - 1;
	cur =  cur < 0 ? cur+MAXHIST : cur;
	return h->runs + cur;
}


static void add_run( int lognr, time_t t )
{
	assert(lognr>=0 && lognr<numl);
	hist_t* h = hist+lognr;
	h->tail = (h->tail+1) % MAXHIST;
	if ( h->tail == h->head )
	{
		// drop oldest
		h->head = (h->head+1) % MAXHIST;
	}
	run_t* r = current_run( lognr );
	memset( r, 0, sizeof(run_t) );
	r->stamps[0] = t;
}


int num_entries( int lognr )
{
	assert(lognr>=0 && lognr<numl);
	hist_t* h = hist+lognr;
	int cnt = h->tail - h->head;
	cnt = cnt < 0 ? cnt+MAXHIST : cnt;
	return cnt;
}


// Parses log entries that look like this:
// 2021-05-13T09:14:35.538 harvester chia.harvester.harvester: INFO     0 plots were eligible for farming c1c8456f7a... Found 0 proofs. Time: 0.00201 s. Total 36 plots

static void analyze_line(int lognr, const char* line, ssize_t length)
{
	int match=-1;
	if ( length > 30 )
	{
		for ( int i=0; i<NUMWATCH; ++i )
		{
			if ( !strncmp( line, watchphrases[i], strlen(watchphrases[i] ) ) )
			{
				if ( i<NUMSTAGES+1 )
					match = i;
				else if ( i==6 && !dirname_temp[0])
				{
					const char* s0 = strstr( line, "dirs: " );
					assert( s0 );
					s0 += 6;
					const char* s1 = strstr( s0, " and " );
					assert( s1 );
					strncpy(dirname_temp, s0, s1-s0);
					fprintf(stderr,"tmp dirname: %s\n", dirname_temp);
				}
				else if ( i==7 && !dirname_farm[0])
				{
					const char* s0 = strchr( line, '"' );
					assert( s0 );
					s0 += 1;
					const char* s1 = strstr( s0, "/plot-" );
					assert( s1 );
					strncpy(dirname_farm, s0, s1-s0);
					fprintf(stderr,"frm dirname: %s\n", dirname_farm);
				}
			}
		}
	}
	if ( match >= 0 )
	{
		const char* datestr = line + strlen(line) - 21;

		struct tm tim;
		memset(&tim, 0, sizeof(tim));

		// string like: Sat May 15 19:14:36 2021
		const char* s = strptime( datestr, "%b %d %H:%M:%S %Y", &tim );
		if ( !s || (*s != 0 && *s != '\n') )
		{
			fprintf(stderr,"Failed to parse date string '%s'\n", datestr);
			exit(2);
		}

		tim.tm_isdst = -1; // Don't know if we have daylight saving time!
		time_t t = mktime(&tim);
		if ( t == (time_t) -1 )
			error( EXIT_FAILURE, errno, "mktime() failed" );
	
		if ( match == 0 )
		{
			add_run(lognr, t);
		}
		else
		{
			run_t* r = current_run( lognr );
			if ( r )
			{
				r->stamps[match] = t;
				//fprintf(stderr, "Set timestamp %d/%d to %zd\n", lognr, match, t);
			}
		}
		newest_stamp = t;
	}
}


static int read_log_file(int lognr)
{
	assert( lognr>=0 && lognr<numl );
	FILE* f_log = logfiles[lognr];
	assert( f_log );
	static char* line = 0;
	static size_t linesz=MAXLINESZ;
	if ( !line )
		line = (char*)malloc(MAXLINESZ);

	int linesread = 0;
	do
	{
		struct timeval tv = { 0L, WAIT_BETWEEN_SELECT_US };
		fd_set rdset;
		FD_ZERO(&rdset);
		int log_fds = fileno( f_log );
		FD_SET( log_fds, &rdset );
		const int ready = select( log_fds+1, &rdset, NULL, NULL, &tv);

		if ( ready < 0 )
			error( EXIT_FAILURE, errno, "select() failed" );

		if ( ready == 0 )
		{
			//fprintf( stderr, "No descriptors ready for reading.\n" );
			return linesread;
		}

		const ssize_t ll = getline( &line, &linesz, f_log );
		if ( ll <= 0 )
		{
			//fprintf( stderr, "getline() returned %zd\n", ll );
			clearerr( f_log );
			return linesread;
		}

		analyze_line( lognr, line, ll );
		linesread++;
	} while(1);
}


static void setup_postscript(void)
{
	const uint8_t* c0 = stagecolours[0];
	const uint8_t* c1 = stagecolours[1];
	const uint8_t* c2 = stagecolours[2];
	const uint8_t* c3 = stagecolours[3];
	snprintf
	(
		postscript,
		sizeof(postscript),

		SETBG "%d;%d;%dm"
		SETFG "%d;%d;%dm" "%s"
		SETFG "%d;%d;%dm" "%s"
		SETFG "%d;%d;%dm" "%s"
		SETFG "%d;%d;%dm" "%s"
		SETFG "255;255;255m",

		0x00,0x00,0x00,
		c0[0],c0[1],c0[2], "STAGE1  ",
		c1[0],c1[1],c1[2], "STAGE2  ",
		c2[0],c2[1],c2[2], "STAGE3  ",
		c3[0],c3[1],c3[2], "STAGE4  "
	);
}


static void setup_scale(void)
{
	strncpy( overlay + imw - 4, "NOW", 4 );

	int x = imw - 8;
	int h = 0;
	while( x >= imw/2 )
	{
		char lab[8] = {0,0,0,0, 0,0,0,0};

		if ( h<12 )
			snprintf( overlay+x, sizeof(lab), "%2dh",  h+1);
		else if ( h%24==0 )
			snprintf( overlay+x, sizeof(lab), "%dDAY", h/24);

		x -= 4;
		h += 1;
	}
}


static void update_drivespace_info(void)
{
	if ( dirname_temp[0] && dirname_farm[0] )
	{
		struct statvfs stat_temp;
		struct statvfs stat_farm;
		memset( &stat_temp, 0, sizeof(struct statvfs) );
		memset( &stat_farm, 0, sizeof(struct statvfs) );
		int rv0 = statvfs(dirname_temp, &stat_temp );
		if ( rv0 ) error( EXIT_FAILURE, errno, "statvfs() on temp dir failed" );
		int rv1 = statvfs(dirname_farm, &stat_farm );
		if ( rv1 ) error( EXIT_FAILURE, errno, "statvfs() on farm dir failed" );
		unsigned long free_temp = stat_temp.f_bsize * stat_temp.f_bfree;
		unsigned long free_farm = stat_farm.f_bsize * stat_farm.f_bfree;
		free_temp = free_temp / ( 1UL << 30 );
		free_farm = free_farm / ( 1UL << 30 );
		snprintf( overlay+0, imw/2-1, "Avail %s:%luGiB  %s:%luGiB", dirname_temp, free_temp, dirname_farm, free_farm );
	}
}


static int get_stage( int lognr, time_t t )
{
	hist_t* h = hist+lognr;
	int cur = h->head;
	while ( cur != h->tail )
	{
		const run_t* r = h->runs + cur;
		for ( int j=4; j>=0; --j )
		{
			const time_t t0 = r->stamps[j+0];
			const time_t t1 = r->stamps[j+1];
			if ( t0 && t >= t0 )
				if ( !t1 || t < t1 )
					return j;
		}
		cur++;
	}
	return -1;
}


static void draw_row( int lognr, int rownr, time_t now )
{
	time_t t = now;
	uint32_t* writer = im + rownr * imw + imw - 2;
	for ( int i=0; i<imw-2; ++i )
	{
		const int band = ( t / 900 / 4 ) & 1;
		uint8_t r = 0x36;
		uint8_t g = 0x36;
		uint8_t b = 0x36;
		if ( lognr >= 0 )
		{
			const int st = get_stage( lognr, t );
			if ( st >= 0 )
			{
				r = stagecolours[st][0];
				g = stagecolours[st][1];
				b = stagecolours[st][2];
			}
		}
		if ( band )
		{
			r = r * 200 / 255;
			g = g * 200 / 255;
			b = b * 200 / 255;
		}
		uint32_t c = (0xff<<24) | (b<<16) | (g<<8) | (r<<0);
		*writer-- = c;
		t -= 450;
	}
}


static int update_image(void)
{
	int redraw=0;
	const char* skipdraw = getenv("SKIPDRAW");

	if ( grapher_resized )
	{
		grapher_adapt_to_new_size();
		setup_scale();
		redraw=1;
	}

	// Compose the image.
	if ( newest_stamp > refresh_stamp )
		redraw=1;

	if (redraw)
	{
		update_drivespace_info();
		time_t now = time(0);
		int rowmap[imh];
		memset(rowmap,-1,sizeof(rowmap));
		for (int l=0; l<numl; ++l)
		{
			int y = 3+l*2;
			if ( y < imh )
				rowmap[y] = l;
		}
		for (int y=2; y<imh; ++y)
		{
			int r = rowmap[y];
			draw_row( r, y, now );
		}
		if (!skipdraw)
			grapher_update();
		refresh_stamp = newest_stamp;
	}

	return 0;
}


static void disableRawMode()
{
	tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
}


static void enableRawMode()
{
	tcgetattr(STDIN_FILENO, &orig_termios);
	atexit(disableRawMode);
	struct termios raw = orig_termios;
	raw.c_lflag &= ~(ECHO);				// Don't echo key presses.
	raw.c_lflag &= ~(ICANON);			// Read by char, not by line.
	raw.c_cc[VMIN] = 0;				// No minimum nr of chars.
	raw.c_cc[VTIME] = 0;				// No waiting time.
	tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
}


int main(int argc, char *argv[])
{
	if (argc < 2)
	{
		fprintf( stderr, "Usage: %s logfile1 logfile2 ... logfileN\n", argv[0] );
		exit( 1 );
	}

	init_for_logcount( argc-1, (const char**) (argv+1) );

	setup_postscript();

#if 1
	const int use_double_col = 1;
	int result = grapher_init( use_double_col );
	if ( result < 0 )
	{
		fprintf( stderr, "Failed to intialize grapher(), maybe we are not running in a terminal?\n" );
		exit(2);
	}
#endif

	enableRawMode();

	int done=0;
	do
	{
		// read files....
		for ( int i=0; i<numl; ++i )
			read_log_file(i);

		update_image();

		char c=0;
		const int numr = read( STDIN_FILENO, &c, 1 );
		if ( numr == 1 && ( c == 27 || c == 'q' || c == 'Q' ) )
			done=1;

		// sleep a little.
                const int NS_PER_MS = 1000000;
		struct timespec ts  = { 0, 400 * NS_PER_MS };
		nanosleep( &ts, 0 );

	} while (!done);

	grapher_exit();
	exit(0);
}

