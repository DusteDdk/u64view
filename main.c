/**
 * License: WTFPL
 * Copyleft 2019 DusteD
 */
#include <stdio.h>
#include <ctype.h>
#include <stdlib.h>
#include <unistd.h>
#include <inttypes.h>
#include <getopt.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_net.h>
#include "64.h"

#define MAX_STRING_SIZE 4096
#define UDP_PAYLOAD_SIZE 768
#define SAMPLE_SIZE 192*4
#define IP_ADDR_SIZE 64
#define DEFAULT_LISTEN_PORT 11000
#define DEFAULT_LISTENAUDIO_PORT 11001
#define DEFAULT_WIDTH 384
#define DEFAULT_HEIGHT 272
#define TCP_BUFFER_SIZE 1024
#define TELNET_PORT 23
#define SDLNET_TIMEOUT 30
#define SDLNET_STREAM_TIMEOUT 200
#define USER_COLORS 16*6 + 15 // 16 6 byte values + the 15 commas between them

typedef struct __attribute__((__packed__)) {
	uint16_t seq;
	uint16_t frame;
	uint16_t line;
	uint16_t pixelsInLine;
	uint8_t linexInPacket;
	uint8_t bpp;
	uint16_t encoding;
	char payload[UDP_PAYLOAD_SIZE];
} u64msg_t;

typedef struct __attribute__((__packed__)) {
	uint16_t seq;
	int16_t sample[SAMPLE_SIZE];
} a64msg_t;

typedef enum {
	SCOLORS,
	DCOLORS,
	UCOLORS,
	NUM_OF_COLORSCHEMES
} colorScheme;

uint64_t totalVdataBytes=0;
uint64_t totalAdataBytes=0;
int isStreaming=0;

char ipStr[IP_ADDR_SIZE];

typedef struct {
	int scale;
	int fullscreenFlag;
	int renderFlag;
	int vsyncFlag;
	int verbose;
	int fast;
	int audioFlag;
	colorScheme curColors;
	FILE *vfp;
	FILE *afp;
	char fnbuf[MAX_STRING_SIZE];
	char hostName[MAX_STRING_SIZE];
	int stopStreamOnExit;
	int startStreamOnStart;
	int showHelp;
	UDPpacket *pkg;
	UDPpacket *audpkg;
	SDLNet_SocketSet set;
	UDPsocket udpsock;
	UDPsocket audiosock;
	int listen;
	int listenaudio;
	SDL_AudioSpec want;
	SDL_AudioSpec have;
	SDL_AudioDeviceID dev;
	SDL_Window *win;
	int width;
	int height;
	SDL_Renderer *ren;
	SDL_Texture *tex;
	uint32_t *pixels;
	int pitch;
} programData;

void setDefaults(programData *data)
{
	memset(data, 0, sizeof(programData));
	data->scale = 1;
	data->renderFlag = SDL_RENDERER_ACCELERATED;
	data->fast = 1;
	data->audioFlag = SDL_INIT_AUDIO;
	data->curColors = SCOLORS;
	data->stopStreamOnExit = 1;
	data->startStreamOnStart = 1;
	data->listen = DEFAULT_LISTEN_PORT;
	data->listenaudio = DEFAULT_LISTENAUDIO_PORT;
	data->width = DEFAULT_WIDTH;
	data->height = DEFAULT_HEIGHT;
}

char* intToIp(uint32_t ip)
{
	sprintf(ipStr, "%02i.%02i.%02i.%02i", (ip & 0x000000ff), (ip & 0x0000ff00)>>8, (ip & 0x00ff0000) >> 16, (ip & 0xff000000) >> 24);
	return ipStr;
}

// I found the colors here: https://gist.github.com/funkatron/758033
const uint64_t  sred[]   = {0 , 255, 0x68, 0x70, 0x6f, 0x58, 0x35, 0xb8, 0x6f, 0x43, 0x9a, 0x44, 0x6c, 0x9a, 0x6c, 0x95 };
const uint64_t  sgreen[] = {0 , 255, 0x37, 0xa4, 0x3d, 0x8d, 0x28, 0xc7, 0x4f, 0x39, 0x67, 0x44, 0x6c, 0xd2, 0x5e, 0x95 };
const uint64_t  sblue[]  = {0 , 255, 0x2b, 0xb2, 0x86, 0x43, 0x79, 0x6f, 0x25, 0x00, 0x59, 0x44, 0x6c, 0x84, 0xb5, 0x95 };

// I found these colors by showing them on my CRT monitor and taking a picture with my dslr, doing white correction on the raw and averaging the pixels
// They're not mean to be faithful, just thought it'd be kinda fun to see
const uint64_t dred[]   = { 0x06, 0xf2, 0xb6, 0xa2, 0xaf, 0x86, 0x00, 0xf8, 0xd0, 0x79, 0xfb, 0x5e, 0xa3, 0xd1, 0x6e, 0xdc };
const uint64_t dgreen[] = { 0x0a, 0xf1, 0x3c, 0xf7, 0x45, 0xf9, 0x3a, 0xfe, 0x6e, 0x4e, 0x91, 0x6e, 0xb6, 0xfc, 0xb3, 0xe2 };
const uint64_t dblue[]  = { 0x0b, 0xf1, 0x47, 0xed, 0xd7, 0x64, 0xf2, 0x8a, 0x28, 0x00, 0x8f, 0x69, 0xad, 0xc5, 0xff, 0xdb };

uint64_t ured[] =   { 10,255,30,40,50,60,70,80,90,0xa0,0xb0,0xc0,0xd0,0xc0,0xd0,0xe0 };
uint64_t ugreen[] = { 10,255,30,40,50,60,70,80,90,0xa0,0xb0,0xc0,0xd0,0xc0,0xd0,0xe0 };
uint64_t ublue[] =  { 10,255,30,40,50,60,70,80,90,0xa0,0xb0,0xc0,0xd0,0xc0,0xd0,0xe0 };

const uint64_t *red = sred, *green =sgreen, *blue=sblue;
uint64_t pixMap[0x100];

void setColors(colorScheme colors)
{
	switch(colors) {
		case DCOLORS:
			red = dred;
			green = dgreen;
			blue = dblue;
			break;
		case UCOLORS:
			red = ured;
			green = ugreen;
			blue = ublue;
			break;
		/* fall through */
		case SCOLORS:
		default:
			red = sred;
			green = sgreen;
			blue = sblue;
			break;
	}

	// Build a table with colors for two pixels packed into a byte.
	// Then if we treat the framebuffer as an uint64 array we get to write two pixels in by doing one read and one write
	for(int i=0; i<0x100; i++) {
		int ph = (i & 0xf0) >> 4;
		int pl = i & 0x0f;
		pixMap[i] = red[ph] << (64-8) | green[ph]<< (64-16) | blue[ph] << (64-24) | (uint64_t)0xff << (64-32) | red[pl] << (32-8) | green[pl] << (32-16) | blue[pl] << (32-24) | 0xff;


	}
}

void chkSeq(const char* msg, uint16_t *lseq, uint16_t cseq)
{
	if((uint16_t)(*lseq+1) != cseq && (totalAdataBytes>1024*10 && totalVdataBytes > 1024*1024) ) {
		printf(msg, *lseq, cseq);
	}
	*lseq=cseq;
}

void pic(SDL_Texture* tex, int width, int height, int pitch, uint32_t* pixels)
{
	union {
		uint8_t p[4];
		uint32_t c;
	} pcol;
	int p=0;
	pcol.p[0]=0xff;
	for(int y=0; y < height; y++) {
		for(int x=0; x < width; x++) {
			pcol.p[3]=header_data_cmap[header_data[p]][0];
			pcol.p[2]=header_data_cmap[header_data[p]][1];
			pcol.p[1]=header_data_cmap[header_data[p]][2];
			p++;
			pixels[x + (y*pitch/4)] = pcol.c;
		}
	}
}

void sendSequence(char *hostName, const uint8_t *data, int len)
{
	IPaddress ip;
	TCPsocket sock;
	uint8_t buf[TCP_BUFFER_SIZE];
	SDLNet_SocketSet set;
	set=SDLNet_AllocSocketSet(1);
	int result =0;

	if(SDLNet_ResolveHost(&ip, hostName, TELNET_PORT)) {
		printf("Error resolving '%s' : %s\n", hostName, SDLNet_GetError());
		SDLNet_FreeSocketSet(set);
		return;
	}

	sock = SDLNet_TCP_Open(&ip);
	if(!sock) {
		printf("Error connecting to '%s' : %s\n", hostName, SDLNet_GetError());
		SDLNet_FreeSocketSet(set);
		return;
	}

	SDLNet_TCP_AddSocket(set, sock);

	SDL_Delay(10);
	for(int i=0; i < len; i++) {
		SDL_Delay(1);
		if(SDLNet_TCP_Send(sock, &data[i], 1) <1 ) {
			printf("Error sending command data: %s\n", SDLNet_GetError());
			SDLNet_TCP_Close(sock);
			SDLNet_FreeSocketSet(set);
			return;
		}
		// Empty u64 send buffer
		while( SDLNet_CheckSockets(set, SDLNET_TIMEOUT) == 1 ) {
			result = SDLNet_TCP_Recv(sock, &buf, TCP_BUFFER_SIZE - 1);
			buf[result]=0;
			//puts(buf); // debug, messes up terminal.
		}
	}

	SDLNet_TCP_Close(sock);
	SDLNet_FreeSocketSet(set);
}

// Yeye, these are fragile, they're good enough for now.
void startStream(char *hostName)
{
	const uint8_t data[] = {
		// Video stream
		0x1b, 0x5b, 0x31, 0x35, 0x7e, // f5
		0x1b, 0x5b, 0x42, // Arrow down
		0x1b, 0x5b, 0x42,
		0x1b, 0x5b, 0x42,
		0x1b, 0x5b, 0x42,
		0x1b, 0x5b, 0x42,
		0x1b, 0x5b, 0x42,
		0x1b, 0x5b, 0x42,
		0x1b, 0x5b, 0x42,
		0xd, 0x00, //enter
		0xd, 0x00,
		0xd, 0x00,
		// Audio stream
		0x1b, 0x5b, 0x31, 0x35, 0x7e, // f5
		0x1b, 0x5b, 0x42, // Arrow down
		0x1b, 0x5b, 0x42,
		0x1b, 0x5b, 0x42,
		0x1b, 0x5b, 0x42,
		0x1b, 0x5b, 0x42,
		0x1b, 0x5b, 0x42,
		0x1b, 0x5b, 0x42,
		0x1b, 0x5b, 0x42,
		0xd, 0x00, //enter
		0x1b, 0x5b, 0x42, // Arrow down
		0xd, 0x00,
		0xd, 0x00
	};

	printf("Sending start stream sequence to Ultimate64...\n");
	sendSequence(hostName, data, sizeof(data));
	printf("  * done.\n");
	isStreaming=1;
}

void stopStream(char* hostName)
{
	const uint8_t data[] = {
		// Video stream
		0x1b, 0x5b, 0x31, 0x35, 0x7e, // f5
		0x1b, 0x5b, 0x42, // Arrow down
		0x1b, 0x5b, 0x42,
		0x1b, 0x5b, 0x42,
		0x1b, 0x5b, 0x42,
		0x1b, 0x5b, 0x42,
		0x1b, 0x5b, 0x42,
		0x1b, 0x5b, 0x42,
		0x1b, 0x5b, 0x42,
		0xd, 0x00, //enter
		0xd, 0x00,
		// Audio stream
		0x1b, 0x5b, 0x31, 0x35, 0x7e, // f5
		0x1b, 0x5b, 0x42, // Arrow down
		0x1b, 0x5b, 0x42,
		0x1b, 0x5b, 0x42,
		0x1b, 0x5b, 0x42,
		0x1b, 0x5b, 0x42,
		0x1b, 0x5b, 0x42,
		0x1b, 0x5b, 0x42,
		0x1b, 0x5b, 0x42,
		0xd, 0x00, //enter
		0x1b, 0x5b, 0x42, // Arrow down
		0xd, 0x00
	};

	printf("Sending stop stream sequence to Ultimate64...\n");
	sendSequence(hostName, data, sizeof(data));
	printf("  * done.\n");
	isStreaming=0;
}


void powerOff(char* hostName)
{
	const uint8_t data[] = {
		0x1b, 0x5b, 0x31, 0x35, 0x7e, // f5
		0x1b, 0x5b, 0x42, // Arrow down
		0xd, 0x00, //enter
		0x1b, 0x5b, 0x42,
		0x1b, 0x5b, 0x42,
		0xd, 0x00, //enter
	};

	printf("Sending power-off sequence to Ultimate64...\n");
	sendSequence(hostName, data, sizeof(data));
	printf("  * done.\n");
	isStreaming=0;
}

void reset(char* hostName)
{
	const uint8_t data[] = {
		0x1b, 0x5b, 0x31, 0x35, 0x7e, // f5
		0x1b, 0x5b, 0x42, // Arrow down
		0xd, 0x00,
		0xd, 0x00, //enter
	};

	printf("Sending reset sequence to Ultimate64...\n");
	sendSequence(hostName, data, sizeof(data));
	printf("  * done.\n");
	isStreaming=0;
}

void printColors(const uint64_t *red, const uint64_t *green, const uint64_t *blue)
{
	for(int i=0; i < 16; i++) {
		printf("%02x%02x%02x%c", (int)red[i], (int)green[i],(int)blue[i], ((i==15)?' ':',') );
	}

}

void printHelp(void)
{
	printf("\nUsage: u64view [-l N] [-a N] [-z N |-f] [-s] [-v] [-V] [-c] [-m] [-t] [-T [RGB,...]] [-u IP | -U IP -I IP] [-o FN]\n"
			"       -l N  (default 11000) Video port number.\n"
			"       -a N  (default 11001) Audio port number.\n"
			"       -z N  (default 1)     Scale the window to N times size, N must be an integer.\n"
			"       -f    (default off)   Fullscreen, will stretch.\n"
			"       -s    (default off)   Prefer software rendering, more cpu intensive.\n"
			"       -v    (default off)   Use vsync.\n"
			"       -V    (default off)   Verbose output, tell when packets are dropped, how much data was transferred.\n"
			"       -c    (default off)   Use more versatile drawing method, more cpu intensive, can't scale.\n"
			"       -m    (default off)   Completely turn off audio.\n"
			"       -t    (default off)   Use colors that look more like DusteDs TV instead of the 'real' colors.\n"
			"       -T [] (default off)   No argument: Show color values and help for -T\n"
			"       -u IP (default off)   Connect to Ultimate64 at IP and command it to start streaming Video and Audio.\n"
			"       -U IP (default off)   Same as -u but don't stop the streaming when u64view exits.\n"
			"       -I IP (default off)   Just know the IP, do nothing, so keys can be used for starting/stopping stream.\n"
			"       -o FN (default off)   Output raw ARGB to FN.rgb and PCM to FN.pcm (20 MiB/s, you disk must keep up or packets are dropped).\n\n");
}

void setUserColors(char *ucol)
{
	printf("Using user-provided colors: ");
	int pos=0;
	char colbyte[3] = {0,0,0 };
	for(int i=0; i < 16; i++) {
		colbyte[0] = ucol[pos];
		pos++;
		colbyte[1] = ucol[pos];
		pos++;
		ured[i] = strtol(colbyte, NULL, 16);

		colbyte[0] = ucol[pos];
		pos++;
		colbyte[1] = ucol[pos];
		pos++;
		ugreen[i] = strtol(colbyte, NULL, 16);

		colbyte[0] = ucol[pos];
		pos++;
		colbyte[1] = ucol[pos];
		pos++;
		ublue[i] = strtol(colbyte, NULL, 16);

		pos++; // Skip character after 6 bytes
	}

	printColors(ured, ugreen, ublue);
	printf("\n");
}

int parseArguments(int argc, char **argv, programData *data)
{
	opterr = 0;
	int c;

	while ((c = getopt (argc, argv, "l:a:z:fsvVcmtT:u:U:I:o:")) != -1) {
		switch(c) {
			case 'l':
				data->listen = atoi(optarg);
				if (data->listen == 0) {
					printf("Video port must be an integer larger than 0.\n");
					return EXIT_FAILURE;
				}
				break;
			case 'a':
				data->listenaudio = atoi(optarg);
				if (data->listenaudio == 0) {
					printf("Audio port must be an integer larger than 0.\n");
					return EXIT_FAILURE;
				}
				break;
			case 'z':
				data->scale = atoi(optarg);
				if (data->scale == 0) {
					printf("Scale must be an integer larger than 0.\n");
					return EXIT_FAILURE;
				}
				break;
			case 'f':
				data->fullscreenFlag = SDL_WINDOW_FULLSCREEN_DESKTOP;
				printf("Fullscreen is on.\n");
				break;
			case 's':
				data->renderFlag = SDL_RENDERER_SOFTWARE;
				break;
			case 'v':
				data->vsyncFlag = SDL_RENDERER_PRESENTVSYNC;
				printf("Vsync is on.\n");
				break;
			case 'V':
				data->verbose=1;
				printf("Verbose is on.\n");
				break;
			case 'c':
				data->fast = 0;
				break;
			case 'm':
				data->audioFlag=0;
				printf("Audio is off.\n");
				break;
			case 't':
				data->curColors = DCOLORS;
				printf("Using DusteDs CRT colors.\n");
				break;
			case 'T':
				if (strlen(optarg) != USER_COLORS) {
					printf("Error: Expected a string of exactly %i characters (see  -T without parameter to see examples)\n", USER_COLORS);
					return EXIT_FAILURE;
				}
				setUserColors(optarg);
				data->curColors = UCOLORS;
				break;
			case 'o':
				data->verbose=1;
				printf("Turning on verbose mode, so you can see if you miss any data!\n");
				printf("Outputting video to %s.rgb and audio to %s.pcm ...\n", optarg, optarg);
				printf( "\nTry encoding with:\n"
						"ffmpeg -vcodec rawvideo -pix_fmt abgr -s 384x272 -r 50\\\n"
						"  -i %s.rgb -f s16le -ar 47983 -ac 2 -i %s.pcm\\\n"
						"  -vf scale=w=1920:h=1080:force_original_aspect_ratio=decrease\\\n"
						"  -sws_flags neighbor -crf 15 -vcodec libx264 %s.avi\n\n", optarg, optarg, optarg);

				sprintf(data->fnbuf, "%s.rgb", optarg);
				data->vfp=fopen(data->fnbuf,"w");
				if(!data->vfp) {
					printf("Error opening %s for writing.\n", data->fnbuf);
					return EXIT_FAILURE;
				}
				sprintf(data->fnbuf, "%s.pcm", optarg);
				data->afp=fopen(data->fnbuf,"w");
				if(!data->afp) {
					printf("Error opening %s for writing.\n", data->fnbuf);
					fclose(data->vfp);
					return EXIT_FAILURE;
				}
				break;
			case 'u':
				strncpy(data->hostName, optarg, MAX_STRING_SIZE - 1);
				break;
			case 'U':
				strncpy(data->hostName, optarg, MAX_STRING_SIZE - 1);
				data->stopStreamOnExit=0;
				break;
			case 'I':
				strncpy(data->hostName, optarg, MAX_STRING_SIZE - 1);
				data->stopStreamOnExit=0;
				data->startStreamOnStart=0;
				break;
			case '?':
				if (optopt == 'l' || optopt == 'a' || optopt == 'z' ||
				    optopt == 'u' || optopt  == 'U' || optopt == 'I') {
					printf("Option -%c requires an argument.\n", optopt);
					return EXIT_FAILURE;
				} else if (optopt == 'T') {
					printf("User-defined color option (-T):\n\n    Default colors: ");
					printColors(sred, sgreen, sblue);
					printf("\n    DusteDs colors: ");
					printColors(dred, dgreen, dblue);
					printf("\n\n    If you want to use your own color values, just type them after -T in the format shown above (RGB24 in hex, like HTML, and comma between each color).\n"
							"    The colors are, in order: black, white, red, cyan, purple, green, blue, yellow, orange, brown, pink, dark-grey, grey, light-green, light-blue, light-grey.\n"
							"    Example: DusteDs colors, with a slightly darker blue: -T 060a0b,f2f1f1,b63c47,a2f7ed,af45d7,86f964,0030Ef,f8fe8a,d06e28,794e00,fb918f,5e6e69,a3b6ad,d1fcc5,6eb3ff,dce2db\n\n\n");
					return EXIT_FAILURE;
				}
			case 'h':
				/* fall through */
			default:
				printHelp();
				return EXIT_FAILURE;
		}
	}

	return EXIT_SUCCESS;
}

int setupStream(programData *data)
{
	setColors(data->curColors);

	data->pkg = SDLNet_AllocPacket(sizeof(u64msg_t));
	data->audpkg = SDLNet_AllocPacket(sizeof(a64msg_t));

	// Initialize SDL2
	if (SDL_Init(SDL_INIT_VIDEO|data->audioFlag) != 0) {
		printf("SDL_Init Error: %s\n", SDL_GetError());
		return 1;
	}

	if(SDLNet_Init()==-1) {
		printf("SDLNet_Init: %s\n", SDLNet_GetError());
		return 2;
	}

	if(strlen(data->hostName) && data->startStreamOnStart) {
		startStream(data->hostName);
	}

	data->set=SDLNet_AllocSocketSet(2);
	if(!data->set) {
		printf("SDLNet_AllocSocketSet: %s\n", SDLNet_GetError());
		return 4;
	}

	printf("Opening UDP socket on port %i for video...\n", data->listen);
	data->udpsock=SDLNet_UDP_Open(data->listen);
	if(!data->udpsock) {
		printf("SDLNet_UDP_Open: %s\n", SDLNet_GetError());
		return 3;
	}

	if( SDLNet_UDP_AddSocket(data->set, data->udpsock) == -1 ) {
		printf("SDLNet_UDP_AddSocket error: %s\n", SDLNet_GetError());
		return 5;
	}

	if(data->audioFlag) {
		printf("Opening UDP socket on port %i for audio...\n", data->listenaudio);
		data->audiosock=SDLNet_UDP_Open(data->listenaudio);
		if(!data->audiosock) {
			printf("SDLNet_UDP_Open: %s\n", SDLNet_GetError());
			return 3;
		}

		if( SDLNet_UDP_AddSocket(data->set, data->audiosock) == -1 ) {
			printf("SDLNet_UDP_AddSocket error: %s\n", SDLNet_GetError());
			return 5;
		}

		SDL_memset(&data->want, 0, sizeof(data->want));
		data->want.freq = 48000;
		data->want.format = AUDIO_S16LSB;
		data->want.channels = 2;
		data->want.samples = 192;
		data->dev = SDL_OpenAudioDevice(NULL, 0, &data->want, &data->have, 0);

		if(data->dev==0) {
			printf("Failed to open audio: %s", SDL_GetError());
		}

		SDL_PauseAudioDevice(data->dev, 0);
	}

	// Create a window
	data->win = SDL_CreateWindow("Ultimate 64 view!", 100, 100, data->width*data->scale,
				data->height*data->scale, SDL_WINDOW_SHOWN | data->fullscreenFlag | SDL_WINDOW_RESIZABLE);
	if (data->win == NULL) {
		printf("SDL_CreateWindow Error: %s\n", SDL_GetError());
		return 10;
	}
	// Set icon
	SDL_Surface *iconSurface = SDL_CreateRGBSurfaceFrom(iconPixels,32,32,32,32*4, 0x000000ff, 0x0000ff00, 0x00ff0000, 0xff000000);
	SDL_SetWindowIcon(data->win, iconSurface);
	SDL_FreeSurface(iconSurface);

	// Create a renderer
	data->ren = SDL_CreateRenderer(data->win, -1, (data->vsyncFlag | data->renderFlag));
	if (data->ren == NULL) {
		printf("SDL_CreateRenderer Error: %s\n", SDL_GetError());
		return 11;
	}

	data->tex = SDL_CreateTexture(data->ren,
				SDL_PIXELFORMAT_RGBA8888,
				SDL_TEXTUREACCESS_STREAMING,
				data->width,
				data->height);

	if( SDL_LockTexture(data->tex, NULL, (void**)&data->pixels, &data->pitch) ) {
		printf("Failed to lock texture for writing");
	}

	return 0;
}

void runStream(programData *data)
{
	SDL_Event event;
	int run = 1;
	int sync = 1;
	int staleVideo=7;
	int r = 0;
	uint16_t lastAseq=0;
	uint16_t lastVseq=0;

	pic(data->tex, data->width, data->height, data->pitch, data->pixels);
	while (run) {
		while (SDL_PollEvent(&event)) {
			switch (event.type) {
			case SDL_KEYDOWN:
				switch (event.key.keysym.sym) {
				case SDLK_ESCAPE:
					run = 0;
				break;
				case SDLK_c:
					data->showHelp=0;
					data->curColors++;
					if(data->curColors == NUM_OF_COLORSCHEMES) {
						data->curColors=SCOLORS;
					}
					setColors(data->curColors);
				break;
				case SDLK_s:
					data->showHelp=0;
					if(!strlen(data->hostName)) {
						printf("Can only start/stop stream when started with -u, -U or -I.\n");
					} else {
						if(isStreaming) {
							stopStream(data->hostName);
						} else {
							startStream(data->hostName);
						}
					}
				break;
				case SDLK_h:
					data->showHelp=!data->showHelp;
				break;
				case SDLK_p:
					data->stopStreamOnExit=0;
					powerOff(data->hostName);
					memset(data->hostName, 0, sizeof data->hostName);
				break;
				case SDLK_r:
					if(strlen(data->hostName)) {
						reset(data->hostName);
					} else {
						printf("Can only reset when start with -u, -U or -I.\n");
					}
			}
			break;
			case SDL_QUIT:
				run=0;
				break;
			}
		}

		// Check for audio
		if(data->audioFlag) {
			r = SDLNet_UDP_Recv(data->audiosock, data->audpkg);
			if(r==1) {

				if(totalAdataBytes==0) {
					printf("Got data on audio port (%i) from %s:%i\n", data->listenaudio,
						intToIp(data->audpkg->address.host), data->audpkg->address.port );
				}
				totalAdataBytes += sizeof(a64msg_t);

				a64msg_t *a = (a64msg_t*)data->audpkg->data;
				if(data->verbose)
					chkSeq("UDP audio packet missed or out of order, last received: %i current %i\n", &lastAseq, a->seq);

				if(data->afp && totalVdataBytes != 0 && totalAdataBytes != 0) {
					fwrite(a->sample, SAMPLE_SIZE, 1, data->afp);
				}

				SDL_QueueAudio(data->dev, a->sample, SAMPLE_SIZE );
			} else if(r == -1) {
				printf("SDLNet_UDP_Recv error: %s\n", SDLNet_GetError());
			}
		}

		// Check for video
		r = SDLNet_UDP_Recv(data->udpsock, data->pkg);
		if(r==1 && !data->showHelp) {
			if(totalVdataBytes==0) {
				printf("Got data on video port (%i) from %s:%i\n", data->listen,
				       intToIp(data->pkg->address.host), data->pkg->address.port );
			}
			totalVdataBytes += sizeof(u64msg_t);

			u64msg_t *p = (u64msg_t*)data->pkg->data;
			if(data->verbose)
				chkSeq("UDP video packet missed or out of order, last received: %i current %i\n", &lastVseq, p->seq);

			int y = p->line & 0b0111111111111111;
			if(data->fast) {
				int lpp = p->linexInPacket;
				int hppl =p->pixelsInLine/2;
				for(int l=0; l < lpp; l++) {
					for(int x=0; x < hppl; x++) {
						int idx = x+(l*hppl);
						uint8_t pc = (p->payload[idx]);
						((uint64_t*)data->pixels)[x + ((y+l)*data->pitch/8)] = pixMap[pc];
					}
				}
			} else {
				for(int l=0; l < p->linexInPacket; l++) {
					for(int x=0; x < p->pixelsInLine/2; x++) {
						int idx = x+(l*p->pixelsInLine/2);
						int pl = (p->payload[idx] & 0x0f);
						int ph = (p->payload[idx] & 0xf0) >> 4;
						int r = red[pl];
						int g = green[pl];
						int b = blue[pl];

						SDL_SetRenderDrawColor(data->ren, r, g, b, 255);
						SDL_RenderDrawPoint(data->ren, x*2, y+l);
						r = red[ph];
						g = green[ph];
						b = blue[ph];
						SDL_SetRenderDrawColor(data->ren, r, g, b, 255);
						SDL_RenderDrawPoint(data->ren, x*2+1, y+l);
					}
				}
			}
			if(p->line & 0b1000000000000000) {
				sync=1;
				staleVideo=0;
			}
		} else if(r == -1) {
			printf("SDLNet_UDP_Recv error: %s\n", SDLNet_GetError());
		} else {
			staleVideo++;
			if(staleVideo > 5) {
				if(staleVideo == 6) {
					pic(data->tex, data->width, data->height, data->pitch, data->pixels);
				} else if(staleVideo%10 == 0) {
					sync=1;
				}
			}
		}

		if(sync) {
			sync=0;
			if(data->fast) {
				if(data->vfp && totalVdataBytes != 0 && totalAdataBytes != 0) {
					fwrite(data->pixels, sizeof(uint32_t)*data->width*data->height, 1, data->vfp);
				}
				SDL_UnlockTexture(data->tex);
				SDL_RenderCopy(data->ren, data->tex, NULL, NULL);
				SDL_RenderPresent(data->ren);
				if( SDL_LockTexture(data->tex, NULL, (void**)data->pixels, &data->pitch) ) {
					printf("Error: Failed to lock texture for writing.");
				}
			} else {
				SDL_RenderPresent(data->ren);
			}
		}
		SDLNet_CheckSockets(data->set, SDLNET_STREAM_TIMEOUT);
	}

	if(strlen(data->hostName) && data->stopStreamOnExit) {
		stopStream(data->hostName);
	}

	SDL_DestroyTexture(data->tex);
	if(data->audioFlag) {
		SDL_CloseAudioDevice(data->dev);
	}

	// The logic being that if opening either went south, we already exited.
	if(data->vfp) {
		fclose(data->vfp);
		fclose(data->afp);
	}

	if (data->pkg) {
		SDLNet_FreePacket(data->pkg);
	}

	if (data->audpkg) {
		SDLNet_FreePacket(data->audpkg);
	}

	if (data->set) {
		SDLNet_FreeSocketSet(data->set);
	}

	if (data->udpsock) {
		SDLNet_UDP_Close(data->udpsock);
	}

	if (data->audiosock) {
		SDLNet_UDP_Close(data->audiosock);
	}

	SDL_DestroyRenderer(data->ren);
	SDL_DestroyWindow(data->win);
	SDL_Quit();
}

int main(int argc, char** argv)
{
	programData data;

	setDefaults(&data);
	printf("\nUltimate 64 view!\n-----------------\n  Try -h for options.\n\n");

	if (parseArguments(argc, argv, &data) == EXIT_FAILURE) {
		return EXIT_FAILURE;
	}

	printf("Ultimate64 telnet interface at %s\n", data.hostName);

	if (setupStream(&data) != 0) {
		return EXIT_FAILURE;
	}

	printf("\nRunning...\nPress ESC or close window to stop.\n\n");
	runStream(&data);

	if(data.verbose) {
		printf("\nReceived video data: %"PRIu64" bytes.\nReceived audio data: %"PRIu64" bytes.\n", totalVdataBytes, totalAdataBytes);
	}

	printf("\n\nThanks to Jens Blidon and Markus Schneider for making my favourite tunes!\nThanks to Booze for making the best remix of Chicanes Halcyon and such beautiful visuals to go along with it!\nThanks to Gideons Logic for the U64!\n\n                                    - DusteD says hi! :-)\n\n");

	return EXIT_SUCCESS;
}
