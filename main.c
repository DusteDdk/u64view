/**
 * License: WTFPL
 * Copyleft 2019 DusteD
 * 
 * Half past 2 AM on a Friday disclaimer:
 * I was so tired when I got home from visiting a good friend, but I couldn't get it out of my mind..
 * I was so excited to see that the Ultimate64 can now stream over network.
 * I had to do it! I NEEDED to do it!
 * I'm going to use this to stream my C64 sessions on twitch..
 * It's so terribly ugly! Because I literally got home 2 hours ago...
 * Thanks to Gideon for making this so easy and documenting it so well,
 * for making such an awesome product!
 * 
 * Thanks to all who may find this useful or interesting.
 * There is LOTS of room for optimizing, I didn't even look! Haha! I've never written such ugly SDL before (honest).
 * The only optimization I made was to at least wait for new data on the socket, so CPU usage went from 100% to 42% on my i7 HAHA!
 * 
 * If you fix something and do a pull-request on github, I'll be sure to merge it (if it passes my ever-so-strict standards, lol)
 * 
 *  - DST out.
 */
#include <stdio.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_net.h>


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

// I found the colors here: https://gist.github.com/funkatron/758033
const int red[]   = {0 , 255, 0x68, 0x70, 0x6f, 0x58, 0x35, 0xb8, 0x6f, 0x43, 0x9a, 0x44, 0x6c, 0x9a, 0x6c, 0x95 };
const int blue[]  = {0 , 255, 0x2b, 0xb2, 0x86, 0x43, 0x79, 0x6f, 0x25, 0x00, 0x59, 0x44, 0x6c, 0x84, 0xb5, 0x95 };
const int green[] = {0 , 255, 0x37, 0xa4, 0x3d, 0x8d, 0x28, 0xc7, 0x4f, 0x39, 0x67, 0x44, 0x6c, 0xd2, 0x5e, 0x95 };

int main(int argc, char** argv) {

	int listen = 11000;
	int listenaudio = 11001;
	const int width=384;
	const int height=272;

	int sawAudio=0;
	int sawVideo=0;

	printf("\nUltimate 64 view!\n\n");
	UDPpacket pkg, audpkg;
	u64msg_t u64pkg;
	a64msg_t a64pkg;

//	memset(&pkg, 0, sizeof(UDPpacket));
//	memset(&audpkg, 0, sizeof(UDPpacket));

	pkg.data = (uint8_t*)&u64pkg;
	audpkg.data = (uint8_t*)&a64pkg;

	// Initialize SDL2
	if (SDL_Init(SDL_INIT_VIDEO|SDL_INIT_AUDIO) != 0) {
		printf("SDL_Init Error: %s\n", SDL_GetError());
		return 1;
	}

	if(SDLNet_Init()==-1) {
		printf("SDLNet_Init: %s\n", SDLNet_GetError());
		return 2;
	}


	UDPsocket udpsock,audiosock;

	printf("Opening UDP socket on port %i for video...\n", listen);
	udpsock=SDLNet_UDP_Open(listen);
	if(!udpsock) {
		printf("SDLNet_UDP_Open: %s\n", SDLNet_GetError());
		return 3;
	}

	printf("Opening UDP socket on port %i for audio...\n", listenaudio);
	audiosock=SDLNet_UDP_Open(listenaudio);
	if(!audiosock) {
		printf("SDLNet_UDP_Open: %s\n", SDLNet_GetError());
		return 3;
	}
	printf("Started. Press ESC or close window to stop.\n");

	SDLNet_SocketSet set;

	set=SDLNet_AllocSocketSet(2);
	if(!set) {
		printf("SDLNet_AllocSocketSet: %s\n", SDLNet_GetError());
		return 4;
	}

	if( SDLNet_UDP_AddSocket(set,udpsock) == -1 ) {
		printf("SDLNet_UDP_AddSocket error: %s\n", SDLNet_GetError());
		return 5;
	}

	if( SDLNet_UDP_AddSocket(set,audiosock) == -1 ) {
		printf("SDLNet_UDP_AddSocket error: %s\n", SDLNet_GetError());
		return 5;
	}


	// Create a window
	SDL_Window *win = SDL_CreateWindow("Ultimate 64 view!", 100, 100, width, height, SDL_WINDOW_SHOWN);
	if (win == NULL) {
		printf("SDL_CreateWindow Error: %s\n", SDL_GetError());
		return 10;
	}

	// Create a renderer
	SDL_Renderer *ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED /*| SDL_RENDERER_PRESENTVSYNC*/ );
	if (ren == NULL) {
		printf("SDL_CreateRenderer Error: %s\n", SDL_GetError());
		return 11;
	}

	// Copypaste we can, from the SDL docs
	SDL_AudioSpec want, have;
	SDL_AudioDeviceID dev;

	SDL_memset(&want, 0, sizeof(want));
	want.freq = 48000;
	want.format = AUDIO_S16LSB;
	want.channels = 2;
	want.samples = 192;
	dev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0 /*SDL_AUDIO_ALLOW_FORMAT_CHANGE */);

	if(dev==0) {
		printf("Failed to open audio: %s", SDL_GetError());
	}
	SDL_PauseAudioDevice(dev, 0);


	SDL_Event event;

	// This is the main loop
	int run = 1;
	int sync = 0;

	while (run) {
		// Handle events
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

		int r = SDLNet_UDP_Recv(udpsock, &pkg);
		if(r==1) {
			u64msg_t *p = (u64msg_t*)pkg.data;

			if(!sawVideo) {
				sawVideo=1;
				printf("Saw data on video port: %i (won't repeat message)\n", listen);
			}
			
			int y = p->line & 0b0111111111111111;
			
			int r;
			int g;
			int b;

			for(int l=0; l < p->linexInPacket; l++) {
				for(int x=0; x < p->pixelsInLine/2; x++) {
					int idx = x+(l*p->pixelsInLine/2);
					int pl = (p->payload[idx] & 0x0f);
					int ph = (p->payload[idx] & 0xf0) >> 4;
 					r = red[pl];
					g = green[pl];
					b = blue[pl];

					SDL_SetRenderDrawColor(ren, r, g, b, 255);
					SDL_RenderDrawPoint(ren, x*2, y+l);
					r = red[ph];
					g = green[ph];
					b = blue[ph];
					SDL_SetRenderDrawColor(ren, r, g, b, 255);
					SDL_RenderDrawPoint(ren, x*2+1, y+l);
				}
			}
			if(p->line & 0b1000000000000000) {
				sync=1;
			}
		} else if(r == -1) {
			printf("SDLNet_UDP_Recv error: %s\n", SDLNet_GetError());
		}

		r = SDLNet_UDP_Recv(audiosock, &audpkg);
		if(r==1) {
			if(!sawAudio) {
				sawAudio=1;
				printf("Saw data on audio port: %i (won't repeat message)\n", listenaudio);
			}

			a64msg_t *a = (a64msg_t*)audpkg.data;
			SDL_QueueAudio(dev, a->sample, 192*4 );
		} else if(r == -1) {
			printf("SDLNet_UDP_Recv error: %s\n", SDLNet_GetError());
		}

		if(sync) {
			SDL_RenderPresent(ren);
			sync=0;
		}
		SDLNet_CheckSockets(set, 2);
	}

	SDL_CloseAudioDevice(dev);
	SDL_DestroyRenderer(ren);
	SDL_DestroyWindow(win);
	SDL_Quit();

	printf("\nThanks to Jens Blidon and Markus Schneider for making my favourite tunes!\nThanks to Booze for making the best remix of Chicanes Halcyon and such beautiful visuals to go along with it!\nThanks to Gideons Logic for the U64!\n\n         - DusteD says hi! :-)\n\n");
	return 0;
}
