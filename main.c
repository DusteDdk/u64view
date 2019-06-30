/**
 * License: WTFPL
 * Copyleft 2019 DusteD
 */
#include <stdio.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_net.h>
#include "64.h"



typedef struct __attribute__((__packed__)) {
	uint16_t seq;
	uint16_t frame;
	uint16_t line;
	uint16_t pixelsInLine;
	uint8_t linexInPacket;
	uint8_t bpp;
	uint16_t encoding;
	char payload[768];
} u64msg_t;

typedef struct __attribute__((__packed__)) {
	u_int16_t seq;
	int16_t sample[192*4];
} a64msg_t;

char ipStr[64];
char* intToIp(uint32_t ip) {
	sprintf(ipStr, "%02i.%02i.%02i.%02i", (ip & 0x000000ff), (ip & 0x0000ff00)>>8, (ip & 0x00ff0000) >> 16, (ip & 0xff000000) >> 24);
	return ipStr;
}

// I found the colors here: https://gist.github.com/funkatron/758033
const uint64_t  red[]   = {0 , 255, 0x68, 0x70, 0x6f, 0x58, 0x35, 0xb8, 0x6f, 0x43, 0x9a, 0x44, 0x6c, 0x9a, 0x6c, 0x95 };
const uint64_t  green[] = {0 , 255, 0x37, 0xa4, 0x3d, 0x8d, 0x28, 0xc7, 0x4f, 0x39, 0x67, 0x44, 0x6c, 0xd2, 0x5e, 0x95 };
const uint64_t  blue[]  = {0 , 255, 0x2b, 0xb2, 0x86, 0x43, 0x79, 0x6f, 0x25, 0x00, 0x59, 0x44, 0x6c, 0x84, 0xb5, 0x95 };

void pic(SDL_Texture* tex, int width, int height, int pitch, uint32_t* pixels) {
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

int main(int argc, char** argv) {

	SDL_Event event;
	int run = 1;
	int sync = 1;

	int sawaudio = 0;
	int sawvideo = 0;
	int listen = 11000;
	int listenaudio = 11001;
	const int width=384;
	const int height=272;

	uint32_t *pixels = NULL;
	int pitch=0;

	SDL_AudioSpec want, have;
	SDL_AudioDeviceID dev;
	UDPpacket *pkg, *audpkg;
	UDPsocket udpsock,audiosock;
	SDLNet_SocketSet set;

	int scale=1;
	int renderFlag = SDL_RENDERER_ACCELERATED;
	int fullscreenFlag = 0;
	int vsyncFlag = 0;
	int fast=1;
	int audioFlag=SDL_INIT_AUDIO;

	printf("\nUltimate 64 view!\n-----------------\n  Try -h for options.\n\n");

	for(int i=1; i < argc; i++) {
		if(strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
			printf("\nUsage: u64view [-z N |-f] [-s] [-v] [-c] [-m]\n"
					"       -z N  (default 1)   Scale the window to N times size, N must be an integer.\n"
					"       -f    (default off) Fullscreen, will stretch.\n"
					"       -s    (default off) Prefer software rendering, more cpu intensive.\n"
					"       -v    (default off) Use vsync.\n"
					"       -c    (default off) Use more versatile drawing method, more cpu intensive, can't scale.\n"
					"       -m    (default off) Completely turn off audio.\n\n");
					return 0;
		} else if(strcmp(argv[i], "-z") == 0) {
			if(i+1 < argc) {
				i++;
				scale=atoi(argv[i]);
				if(scale==0) {
					printf("Scale must be an integer larger than 0.\n");
					return 1;
				}
				printf("Scaling %i.\n", scale);
			} else {
				printf("Missing the scale number, see -h");
				return 1;
			}
		} else if(strcmp(argv[i], "-f")==0) {
			fullscreenFlag = SDL_WINDOW_FULLSCREEN_DESKTOP;
			printf("Fullscreen is on.\n");
		}  else if(strcmp(argv[i], "-s")==0) {
			renderFlag = SDL_RENDERER_SOFTWARE;
		}  else if(strcmp(argv[i], "-v")==0) {
			vsyncFlag = SDL_RENDERER_PRESENTVSYNC;
			printf("Vsync is on.\n");
		} else if(strcmp(argv[i], "-c")==0) {
			fast=0;
		} else if(strcmp(argv[i], "-m")==0) {
			audioFlag=0;
			printf("Audio is off.\n");
		} else {
			printf("Unknown option '%s', try -h\n", argv[i]);
			return 1;
		}
	}

	// Build a table with colors for two pixels packed into a byte.
	// Then if we treat the framebuffer as an uint64 array we get to write two pixels in by doing one read and one write.
	u_int64_t pixMap[0x100];
	for(int i=0; i<0x100; i++) {
		int ph = (i & 0xf0) >> 4;
		int pl = i & 0x0f;
		pixMap[i] = red[ph] << (64-8) | green[ph]<< (64-16) | blue[ph] << (64-24) | (uint64_t)0xff << (64-32) | red[pl] << (32-8) | green[pl] << (32-16) | blue[pl] << (32-24) | 0xff;
	}

	pkg = SDLNet_AllocPacket(sizeof(u64msg_t));
	audpkg = SDLNet_AllocPacket(sizeof(a64msg_t));

	// Initialize SDL2
	if (SDL_Init(SDL_INIT_VIDEO|audioFlag) != 0) {
		printf("SDL_Init Error: %s\n", SDL_GetError());
		return 1;
	}

	if(SDLNet_Init()==-1) {
		printf("SDLNet_Init: %s\n", SDLNet_GetError());
		return 2;
	}

	set=SDLNet_AllocSocketSet(2);
	if(!set) {
		printf("SDLNet_AllocSocketSet: %s\n", SDLNet_GetError());
		return 4;
	}

	printf("Opening UDP socket on port %i for video...\n", listen);
	udpsock=SDLNet_UDP_Open(listen);
	if(!udpsock) {
		printf("SDLNet_UDP_Open: %s\n", SDLNet_GetError());
		return 3;
	}

	if( SDLNet_UDP_AddSocket(set,udpsock) == -1 ) {
		printf("SDLNet_UDP_AddSocket error: %s\n", SDLNet_GetError());
		return 5;
	}

	if(audioFlag) {
		printf("Opening UDP socket on port %i for audio...\n", listenaudio);
		audiosock=SDLNet_UDP_Open(listenaudio);
		if(!audiosock) {
			printf("SDLNet_UDP_Open: %s\n", SDLNet_GetError());
			return 3;
		}

		if( SDLNet_UDP_AddSocket(set,audiosock) == -1 ) {
			printf("SDLNet_UDP_AddSocket error: %s\n", SDLNet_GetError());
			return 5;
		}

		SDL_memset(&want, 0, sizeof(want));
		want.freq = 48000;
		want.format = AUDIO_S16LSB;
		want.channels = 2;
		want.samples = 192;
		dev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);

		if(dev==0) {
			printf("Failed to open audio: %s", SDL_GetError());
		}

		SDL_PauseAudioDevice(dev, 0);
	}


	// Create a window
	SDL_Window *win = SDL_CreateWindow("Ultimate 64 view!", 100, 100, width*scale, height*scale, SDL_WINDOW_SHOWN | fullscreenFlag | SDL_WINDOW_RESIZABLE);
	if (win == NULL) {
		printf("SDL_CreateWindow Error: %s\n", SDL_GetError());
		return 10;
	}
	// Set icon
	SDL_Surface *iconSurface = SDL_CreateRGBSurfaceFrom(iconPixels,32,32,32,32*4, 0x000000ff, 0x0000ff00, 0x00ff0000, 0xff000000);
	SDL_SetWindowIcon(win, iconSurface);
	SDL_FreeSurface(iconSurface);

	// Create a renderer
	SDL_Renderer *ren = SDL_CreateRenderer(win, -1, (vsyncFlag | renderFlag));
	if (ren == NULL) {
		printf("SDL_CreateRenderer Error: %s\n", SDL_GetError());
		return 11;
	}

	SDL_Texture* tex = SDL_CreateTexture(ren,
								SDL_PIXELFORMAT_RGBA8888,
								SDL_TEXTUREACCESS_STREAMING,
								width,
								height);

	if( SDL_LockTexture(tex, NULL, (void**)&pixels, &pitch) ) {
		printf("Failed to lock texture for writing");
	}

	printf("\nRunning...\nPress ESC or close window to stop.\n\n");

	pic(tex, width, height, pitch, pixels);
	int staleVideo=7;
	int r;
	while (run) {
		while (SDL_PollEvent(&event)) {
			switch (event.type) {
			case SDL_KEYDOWN:
				switch (event.key.keysym.sym) {
				case SDLK_ESCAPE:
					run = 0;
				break;
			}
			break;
			case SDL_QUIT:
				run=0;
				break;
			}
		}

		// Check for audio
		if(audioFlag) {
			r = SDLNet_UDP_Recv(audiosock, audpkg);
			if(r==1) {
				if(!sawaudio) {
					sawaudio=1;
					printf("Got data on audio port (%i) from %s:%i\n", listenaudio, intToIp(pkg->address.host),pkg->address.port );
				}

				a64msg_t *a = (a64msg_t*)audpkg->data;
				SDL_QueueAudio(dev, a->sample, 192*4 );
			} else if(r == -1) {
				printf("SDLNet_UDP_Recv error: %s\n", SDLNet_GetError());
			}
		}

		// Check for video
		r = SDLNet_UDP_Recv(udpsock, pkg);
		if(r==1) {
			if(!sawvideo) {
				sawvideo=1;
				printf("Got data on video port (%i) from %s:%i\n", listen, intToIp(pkg->address.host),pkg->address.port );
			}
			u64msg_t *p = (u64msg_t*)pkg->data;
			int y = p->line & 0b0111111111111111;
			if(fast) {
				int lpp = p->linexInPacket;
				int hppl =p->pixelsInLine/2;
				for(int l=0; l < lpp; l++) {
					for(int x=0; x < hppl; x++) {
						int idx = x+(l*hppl);
						uint8_t pc = (p->payload[idx]);
						((uint64_t*)pixels)[x + ((y+l)*pitch/8)] = pixMap[pc];
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

						SDL_SetRenderDrawColor(ren, r, g, b, 255);
						SDL_RenderDrawPoint(ren, x*2, y+l);
						r = red[ph];
						g = green[ph];
						b = blue[ph];
						SDL_SetRenderDrawColor(ren, r, g, b, 255);
						SDL_RenderDrawPoint(ren, x*2+1, y+l);
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
					pic(tex, width, height, pitch, pixels);
				} else if(staleVideo%10 == 0) {
					sync=1;
				}
			}
		}

		if(sync) {
			sync=0;
			if(fast) {
				SDL_UnlockTexture(tex);
				SDL_RenderCopy(ren, tex, NULL, NULL);
				SDL_RenderPresent(ren);
				if( SDL_LockTexture(tex, NULL, (void**)&pixels, &pitch) ) {
					printf("Error: Failed to lock texture for writing.");
				}
			} else {
				SDL_RenderPresent(ren);
			}
		}
		SDLNet_CheckSockets(set, 200);
	}

	SDL_DestroyTexture(tex);
	if(audioFlag) {
		SDL_CloseAudioDevice(dev);
	}
	SDL_DestroyRenderer(ren);
	SDL_DestroyWindow(win);
	SDL_Quit();

	printf("\n\nThanks to Jens Blidon and Markus Schneider for making my favourite tunes!\nThanks to Booze for making the best remix of Chicanes Halcyon and such beautiful visuals to go along with it!\nThanks to Gideons Logic for the U64!\n\n                                    - DusteD says hi! :-)\n\n");
	return 0;
}
