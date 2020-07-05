/**
 * License: WTFPL
 * Copyleft 2019 DusteD
 */
#include <stdio.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_net.h>
#include <inttypes.h>
#include "64.h"

#include <sys/socket.h>             //socket
#include <arpa/inet.h>              //inet_addr
#include <unistd.h>

#define ERROR_SEND_FAILED       -1
#define ERROR_RECIEVE_FAILED    -2
#define ERROR_CONNECT_FAILED    -3
#define ERROR_SOCKET_FAILED     -4
#define ERROR_WINSOCK_FAILED    -5

#define ERROR_VALUE_MISSING     -42

#define SOCKET_CMD_DMA          0xFF01 /* dma-load .prg file */
#define SOCKET_CMD_DMARUN       0xFF02 /* dma-load .prg file and run it */
#define SOCKET_CMD_KEYB         0xFF03 /* simulate keyboard input */
#define SOCKET_CMD_RESET        0xFF04 /* reset the c64 */
#define SOCKET_CMD_WAIT         0xFF05 /* wait n ticks */
#define SOCKET_CMD_DMAWRITE     0xFF06 /* write c64 memory */
#define SOCKET_CMD_REUWRITE     0xFF07
#define SOCKET_CMD_KERNALWRITE  0xFF08
#define SOCKET_CMD_DMAJUMP      0xFF09 /* dma-load .prg file and jump to addr */
#define SOCKET_CMD_MOUNT_IMG    0xFF0A /* mount image */
#define SOCKET_CMD_RUN_IMG      0xFF0B /* mount and run image */

// Only available on U64
#define SOCKET_CMD_VICSTREAM_ENABLE    0xFF20
#define SOCKET_CMD_SIDSTREAM_ENABLE    0xFF21
#define SOCKET_CMD_DEBUGSTREAM_ENABLE  0xFF22

#define SOCKET_CMD_VICSTREAM_DISABLE   0xFF30
#define SOCKET_CMD_SIDSTREAM_DISABLE   0xFF31
#define SOCKET_CMD_DEBUGSTREAM_DISABLE 0xFF32

// Undocumented, shall only be used by developers
#define SOCKET_CMD_LOADSIDCRT   0xFF71 /* send 8k SID cartridge image? not working :/ */
#define SOCKET_CMD_LOADBOOTCRT  0xFF72 /* send 8k cartridge image? not working :/ */
#define SOCKET_CMD_SAMPLE       0xFF73 /* removed */
#define SOCKET_CMD_READMEM      0xFF74 /* read memory? not working :( */
#define SOCKET_CMD_READFLASH    0xFF75
#define SOCKET_CMD_READDBGREG   0xFF76 /* read and write the debug register d7ff */

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
    uint16_t seq;
    int16_t sample[192*4];
} a64msg_t;

uint64_t totalVdataBytes=0, totalAdataBytes=0;

char *hostName = NULL;
in_addr_t u64_addr = 0;

char ipStr[64];
char* intToIp(uint32_t ip) {
    sprintf(ipStr, "%02i.%02i.%02i.%02i", (ip & 0x000000ff), (ip & 0x0000ff00)>>8, (ip & 0x00ff0000) >> 16, (ip & 0xff000000) >> 24);
    return ipStr;
}

// I found the colors here: https://gist.github.com/funkatron/758033
const uint64_t sred[]   = {0 , 255, 0x68, 0x70, 0x6f, 0x58, 0x35, 0xb8, 0x6f, 0x43, 0x9a, 0x44, 0x6c, 0x9a, 0x6c, 0x95 };
const uint64_t sgreen[] = {0 , 255, 0x37, 0xa4, 0x3d, 0x8d, 0x28, 0xc7, 0x4f, 0x39, 0x67, 0x44, 0x6c, 0xd2, 0x5e, 0x95 };
const uint64_t sblue[]  = {0 , 255, 0x2b, 0xb2, 0x86, 0x43, 0x79, 0x6f, 0x25, 0x00, 0x59, 0x44, 0x6c, 0x84, 0xb5, 0x95 };

// I found these colors by showing them on my CRT monitor and taking a picture with my dslr, doing white correction on the raw and averaging the pixels
// They're not mean to be faithful, just thought it'd be kinda fun to see
const uint64_t dred[]   = { 0x06, 0xf2, 0xb6, 0xa2, 0xaf, 0x86, 0x00, 0xf8, 0xd0, 0x79, 0xfb, 0x5e, 0xa3, 0xd1, 0x6e, 0xdc };
const uint64_t dgreen[] = { 0x0a, 0xf1, 0x3c, 0xf7, 0x45, 0xf9, 0x3a, 0xfe, 0x6e, 0x4e, 0x91, 0x6e, 0xb6, 0xfc, 0xb3, 0xe2 };
const uint64_t dblue[]  = { 0x0b, 0xf1, 0x47, 0xed, 0xd7, 0x64, 0xf2, 0x8a, 0x28, 0x00, 0x8f, 0x69, 0xad, 0xc5, 0xff, 0xdb };

uint64_t ured[] =   { 10,255,30,40,50,60,70,80,90,0xa0,0xb0,0xc0,0xd0,0xc0,0xd0,0xe0 };
uint64_t ugreen[] = { 10,255,30,40,50,60,70,80,90,0xa0,0xb0,0xc0,0xd0,0xc0,0xd0,0xe0 };
uint64_t ublue[] =  { 10,255,30,40,50,60,70,80,90,0xa0,0xb0,0xc0,0xd0,0xc0,0xd0,0xe0 };

int curColors=0;
const uint64_t *red = sred, *green =sgreen, *blue=sblue;;
uint64_t pixMap[0x100];

void setColors(int colors) {

    switch(colors) {
        case 0:
            red = sred;
            green = sgreen;
            blue = sblue;
            break;
        case 1:
            red = dred;
            green = dgreen;
            blue = dblue;
            break;
        case 2:
            red = ured;
            green = ugreen;
            blue = ublue;
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

int verbose=0;
void chkSeq(const char* msg, uint16_t *lseq, uint16_t cseq) {
    if((uint16_t)(*lseq+1) != cseq && (totalAdataBytes>1024*10 && totalVdataBytes > 1024*1024) ) {
        printf(msg, *lseq, cseq);
    }
    *lseq=cseq;
}

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

int u64_command(int command, unsigned char *data, int len) {
    int sock;
    struct sockaddr_in server;
	unsigned char cmd[4];

	cmd[0] = command & 0xff;
    cmd[1] = command >> 8;
	cmd[2] = len & 0xff;
	cmd[3] = len >> 8;

    if ((sock = socket(AF_INET , SOCK_STREAM , 0)) < 0) {
	    printf("Failed to create socket.\n");
        return ERROR_SOCKET_FAILED;
    }

    server.sin_addr.s_addr = u64_addr;
    server.sin_family = AF_INET;
    server.sin_port = htons( 64 );

    if (connect(sock, (struct sockaddr *)&server, sizeof(server)) < 0) {
	    printf("Failed to connect socket to %x.\n", u64_addr);
        return ERROR_CONNECT_FAILED;
    }

    int rv = ERROR_SEND_FAILED;

    if (send(sock, &cmd[0], 4, 0) >= 0) {
	    if (len > 0 && send(sock, data, len, 0) >= 0) {
		    rv = 0;
		}
    }

	close(sock);
    return rv;
}

void u64_reset() {
    int ret;
    ret = u64_command(SOCKET_CMD_RESET, NULL, 0);
	if (ret == 0) {
	    printf("u64_reset sent.\n");
	} else {
	    printf("u64_reset failed with code %d.\n", ret);
	}
}

void u64_key(int petscii) {
    int ret;
    unsigned char key[1];
	key[0] = petscii & 0xff;
    ret = u64_command(SOCKET_CMD_KEYB, (unsigned char *)&key, 1);
	if (ret == 0) {
	    printf("u64_key(%d) sent.\n", petscii);
	} else {
	    printf("u64_key(%d) failed with code %d.\n", petscii, ret);
	}
}

void sendSequence(char *hostName, const uint8_t *data, int len) {
    IPaddress ip;
    TCPsocket sock;
    uint8_t buf[1024];
    SDLNet_SocketSet set;
    set=SDLNet_AllocSocketSet(1);
    int result =0;

    if(SDLNet_ResolveHost(&ip, hostName, 23)) {
        printf("Error resolving '%s' : %s\n", hostName, SDLNet_GetError());
        return;
    }

    sock = SDLNet_TCP_Open(&ip);
    if(!sock) {
        printf("Error connecting to '%s' : %s\n", hostName, SDLNet_GetError());
        return;
    }

    SDLNet_TCP_AddSocket(set, sock);

    SDL_Delay(10);
    for(int i=0; i < len; i++) {
        SDL_Delay(1);
        if(SDLNet_TCP_Send(sock, &data[i], 1) <1 ) {
            printf("Error sending command data: %s\n", SDLNet_GetError());
        }
        // Empty u64 send buffer
        while( SDLNet_CheckSockets(set, 30) == 1 ) {
            result = SDLNet_TCP_Recv(sock, &buf, 1023);
            buf[result]=0;
            //puts(buf); // debug, messes up terminal.
        }
    }

    SDLNet_TCP_Close(sock);
}

int isStreaming=0;
// Yeye, these are fragile, they're good enough for now.
void startStream(char *hostName) {
    const uint8_t data[] = {
        0x1b, 0x5b, 0x31, 0x35, 0x7e, // f5
        0x1b, 0x5b, 0x42, // Arrow down
        0x1b, 0x5b, 0x42,
        0x1b, 0x5b, 0x42,
        0x1b, 0x5b, 0x42,
        0xd, 0x00, //enter
        0xd, 0x00,
        0x1b, 0x5b, 0x31, 0x35, 0x7e, // f5
        0x1b, 0x5b, 0x42, // arrow down
        0x1b, 0x5b, 0x42,
        0x1b, 0x5b, 0x42,
        0x1b, 0x5b, 0x42,
        0x1b, 0x5b, 0x42,
        0x1b, 0x5b, 0x42,
        0xd, 0x00, //enter
        0xd, 0x00
    };
    printf("Sending start stream sequence to Ultimate64...\n");
    sendSequence(hostName, data, sizeof(data));
    printf("  * done.\n");
    isStreaming=1;
}

void stopStream(char* hostName) {
    const uint8_t data[] = {
        0x1b, 0x5b, 0x31, 0x35, 0x7e, // f5
        0x1b, 0x5b, 0x42, // Arrow down
        0x1b, 0x5b, 0x42,
        0x1b, 0x5b, 0x42,
        0x1b, 0x5b, 0x42,
        0x1b, 0x5b, 0x42,
        0xd, 0x00, //enter
        0x1b, 0x5b, 0x31, 0x35, 0x7e, // f5
        0x1b, 0x5b, 0x42, // arrow down
        0x1b, 0x5b, 0x42,
        0x1b, 0x5b, 0x42,
        0x1b, 0x5b, 0x42,
        0x1b, 0x5b, 0x42,
        0x1b, 0x5b, 0x42,
        0x1b, 0x5b, 0x42,
        0xd, 0x00, //enter
    };
    printf("Sending stop stream sequence to Ultimate64...\n");
    sendSequence(hostName, data, sizeof(data));
    printf("  * done.\n");
    isStreaming=0;
}

/* void reset(int sock) { */
/*     int ret; */
/*     printf("Sending RESET command.\n"); */
/*     ret = ultimate_reset(sock); */
/*     if (ret < 0) { */
/*         printf("Reset failed with return code %d.\n", ret); */
/*     } */
/* } */

void printColors(const uint64_t *red, const uint64_t *green, const uint64_t *blue) {
    for(int i=0; i < 16; i++) {
        printf("%02x%02x%02x%c", (int)red[i], (int)green[i],(int)blue[i], ((i==15)?' ':',') );
    }

}

int main(int argc, char** argv) {

    SDL_Event event;
    int run = 1;
    int sync = 1;

    int listen = 11000;
    int listenaudio = 11001;
    const int width=384;
    const int height=272;

    uint32_t *pixels = NULL;
    int pitch=0;

    SDL_AudioSpec want, have;
    SDL_AudioDeviceID dev=0;
    UDPpacket *pkg, *audpkg;
    UDPsocket udpsock=NULL, audiosock=NULL;
    SDLNet_SocketSet set;

    int scale=1;
    int renderFlag = SDL_RENDERER_ACCELERATED;
    int fullscreenFlag = 0;
    int vsyncFlag = 0;
    int fast=1;
    int audioFlag=SDL_INIT_AUDIO;
    FILE *vfp=NULL, *afp=NULL;
    char fnbuf[4096], *hostName=NULL;
    uint16_t lastAseq=0, lastVseq=0;
    int stopStreamOnExit=1, showHelp=0, startStreamOnStart=1;

    printf("\nUltimate 64 view!\n-----------------\n  Try -h for options.\n\n");

    for(int i=1; i < argc; i++) {
        if(strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            printf("\nUsage: u64view [-z N |-f] [-s] [-v] [-V] [-c] [-m] [-t] [-T [RGB,...]] [-u IP | -U IP -I IP] [-o FN]\n"
                    "       -z N  (default 1)   Scale the window to N times size, N must be an integer.\n"
                    "       -f    (default off) Fullscreen, will stretch.\n"
                    "       -s    (default off) Prefer software rendering, more cpu intensive.\n"
                    "       -v    (default off) Use vsync.\n"
                    "       -V    (default off) Verbose output, tell when packets are dropped, how much data was transferred.\n"
                    "       -c    (default off) Use more versatile drawing method, more cpu intensive, can't scale.\n"
                    "       -m    (default off) Completely turn off audio.\n"
                    "       -t    (default off) Use colors that look more like DusteDs TV instead of the 'real' colors.\n"
                    "       -T [] (default off) No argument: Show color values and help for -T\n"
                    "       -u IP (default off) Connect to Ultimate64 at IP and command it to start streaming Video and Audio.\n"
                    "       -U IP (default off) Same as -u but don't stop the streaming when u64view exits.\n"
                    "       -I IP (default off) Just know the IP, do nothing, so keys can be used for starting/stopping stream.\n"
                    "       -o FN (default off) Output raw ARGB to FN.rgb and PCM to FN.pcm (20 MiB/s, you disk must keep up or packets are dropped).\n\n");
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
        } else if(strcmp(argv[i], "-V")==0) {
            verbose=1;
            printf("Verbose is on.\n");
        } else if(strcmp(argv[i], "-c")==0) {
            fast=0;
        } else if(strcmp(argv[i], "-m")==0) {
            audioFlag=0;
            printf("Audio is off.\n");
        } else if(strcmp(argv[i], "-t")==0) {
            curColors = 1;
            printf("Using DusteDs CRT colors.\n");
        } else if(strcmp(argv[i], "-T") == 0) {
            if(i+1 >= argc || argv[i+1][0] == '-') {
                printf("User-defined color option (-T):\n\n    Default colors: ");
                printColors(sred, sgreen, sblue);
                printf("\n    DusteDs colors: ");
                printColors(dred, dgreen, dblue);
                printf("\n\n    If you want to use your own color values, just type them after -T in the format shown above (RGB24 in hex, like HTML, and comma between each color).\n"
                        "    The colors are, in order: black, white, red, cyan, purple, green, blue, yellow, orange, brown, pink, dark-grey, grey, light-green, light-blue, light-grey.\n"
                        "    Example: DusteDs colors, with a slightly darker blue: -T 060a0b,f2f1f1,b63c47,a2f7ed,af45d7,86f964,0030Ef,f8fe8a,d06e28,794e00,fb918f,5e6e69,a3b6ad,d1fcc5,6eb3ff,dce2db\n\n\n");
                return 0;
            } else {
                i++;
                char* ucol = argv[i];
                printf("Using user-provided colors: ");
                const int ucbytes = 16*6 + 15; // 16 6 byte values + the 15 commas between them
                if(strlen(ucol) != ucbytes)
                {
                    printf("Error: Expected a string of exactly %i characters (see  -T without parameter to see examples)\n", ucbytes);
                }
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

                curColors = 2;
                printColors(ured, ugreen, ublue);
                printf("\n");
            }
        } else if(strcmp(argv[i], "-o") == 0) {
            if(i+1 < argc) {
                i++;
                verbose=1;
                printf("Turning on verbose mode, so you can see if you miss any data!\n");
                printf("Outputting video to %s.rgb and audio to %s.pcm ...\n", argv[i], argv[i]);
                printf( "\nTry encoding with:\n"
                        "ffmpeg -vcodec rawvideo -pix_fmt abgr -s 384x272 -r 50\\\n"
                        "  -i %s.rgb -f s16le -ar 47983 -ac 2 -i %s.pcm\\\n"
                        "  -vf scale=w=1920:h=1080:force_original_aspect_ratio=decrease\\\n"
                        "  -sws_flags neighbor -crf 15 -vcodec libx264 %s.avi\n\n" ,argv[i],argv[i],argv[i]);

                sprintf(fnbuf, "%s.rgb", argv[i]);
                vfp=fopen(fnbuf,"w");
                if(!vfp) {
                    printf("Error opening %s for writing.\n", fnbuf);
                    return 1;
                }
                sprintf(fnbuf, "%s.pcm", argv[i]);
                afp=fopen(fnbuf,"w");
                if(!afp) {
                    printf("Error opening %s for writing.\n", fnbuf);
                    return 1;
                }
            } else {
                printf("Missing filename.\n");
                return 1;
            }
        } else if(strcmp(argv[i], "-u") == 0 || strcmp(argv[i], "-U") == 0 || strcmp(argv[i], "-I") == 0) {
            if(strcmp(argv[i], "-U") == 0) {
                stopStreamOnExit=0;
            }
            if(strcmp(argv[i], "-I") == 0) {
                stopStreamOnExit=0;
                startStreamOnStart=0;
            }
            if(i+1 < argc) {
                i++;
                hostName=argv[i];
				u64_addr=inet_addr(hostName);
            } else {
                printf("Missing IP address.\n");
                return 1;
            }
            printf("Ultimate64 telnet interface at %s\n", hostName);
        } else {
            printf("Unknown option '%s', try -h\n", argv[i]);
            return 1;
        }
    }

    setColors(curColors);

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

    if(hostName && startStreamOnStart) {
        startStream(hostName);
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
    int mod;
    int sym;
    int lowercase = 0;
    int num_shift[10] = {41, 33, 64, 35, 36, 37, 94, 38, 42, 40};

    while (run) {
        while (SDL_PollEvent(&event)) {
            switch (event.type) {
            case SDL_KEYDOWN:
	        mod = event.key.keysym.mod;
	        sym = event.key.keysym.sym;
		printf("KEYDOWN: mod=%d, sym=%d\n", mod, sym);
	        switch (mod) {
		case KMOD_NONE:
		    // No modifiers pressed
		    switch (sym) {
		    case SDLK_ESCAPE:
		        run = 0; break;
		    case SDLK_F12:
		        u64_reset(); break;
		    case SDLK_RETURN:
		        u64_key(13); break;
		    case SDLK_F1:
		        u64_key(133); break;
		    case SDLK_F2:
		        u64_key(137); break;
		    case SDLK_F3:
		        u64_key(134); break;
		    case SDLK_F4:
		        u64_key(138); break;
		    case SDLK_F5:
		        u64_key(135); break;
		    case SDLK_F6:
		        u64_key(139); break;
		    case SDLK_F7:
		        u64_key(136); break;
		    case SDLK_F8:
		        u64_key(140); break;
		    case SDLK_LEFT:
		        u64_key(157); break;
		    case SDLK_RIGHT:
		        u64_key(29); break;
		    case SDLK_UP:
		        u64_key(145); break;
		    case SDLK_DOWN:
		        u64_key(17); break;
		    case SDLK_HOME:
		        u64_key(19); break;
		    case SDLK_LEFTBRACKET:
		        u64_key(91); break;
		    case SDLK_RIGHTBRACKET:
		        u64_key(93); break;
		    case SDLK_BACKQUOTE:
		        u64_key(95); break; // Arrow left
		    case SDLK_BACKSLASH:
		        u64_key(255); break; // PI symbol
		    case SDLK_BACKSPACE:
		        u64_key(20); break;
		    case SDLK_INSERT:
		        u64_key(148); break;
		    case SDLK_DELETE:
		        u64_key(29); u64_key(20); break;
		    default:
		        if (sym >= SDLK_SPACE && sym <= SDLK_AT)
			    u64_key(sym);
			else if (sym >= SDLK_a && sym <= SDLK_z)
			    u64_key(sym - 32);
		    }
		    break;
		case KMOD_LSHIFT:
		case KMOD_RSHIFT:
		    // Shift pressed
		    switch (sym) {
		    case SDLK_RETURN:
		        u64_key(141); break;
		    case SDLK_SPACE:
		        u64_key(160); break;
		    case SDLK_HOME:
		        u64_key(147); break;
		    case SDLK_EQUALS:
		        u64_key(43); break;
		    case SDLK_SEMICOLON:
		        u64_key(58); break;
		    case SDLK_QUOTE:
		        u64_key(34); break;
		    case SDLK_COMMA:
		        u64_key(60); break;
		    case SDLK_PERIOD:
		        u64_key(62); break;
		    case SDLK_SLASH:
		        u64_key(63); break;
		    case SDLK_MINUS:
		        u64_key(92); break; // Pound symbol
		    case SDLK_PAGEUP:
		        u64_key(18); break; // Reverse on
		    case SDLK_PAGEDOWN:
		        u64_key(146); break; // Reverse off
		    default:
		        if (sym >= SDLK_0 && sym <= SDLK_9)
		            u64_key(num_shift[sym - SDLK_0]);
		        else if (sym >= SDLK_a && sym <= SDLK_z)
		            u64_key(sym);
		    }
		    break;
		case KMOD_LCTRL:
		case KMOD_RCTRL:
		    // Control pressed
		    switch (event.key.keysym.sym) {
		    case SDLK_c:
		        u64_key(3); break; // Stop
		    }
		    break;
		case KMOD_LALT:
		case KMOD_RALT:
		    // Alt pressed
		    switch (event.key.keysym.sym) {
		    }
		    break;
		case KMOD_LALT + KMOD_LCTRL:
		    u64_key(14 + lowercase);
		    lowercase = 128 - lowercase;
		    break;
		}
		break;
            case SDL_QUIT:
                run = 0; break;
            }
        }

        // Check for audio
        if(audioFlag) {
            r = SDLNet_UDP_Recv(audiosock, audpkg);
            if(r==1) {

                if(totalAdataBytes==0) {
                    printf("Got data on audio port (%i) from %s:%i\n", listenaudio, intToIp(audpkg->address.host),audpkg->address.port );
                }
                totalAdataBytes += sizeof(a64msg_t);

                a64msg_t *a = (a64msg_t*)audpkg->data;
                if(verbose) chkSeq("UDP audio packet missed or out of order, last received: %i current %i\n", &lastAseq, a->seq);

                if(afp && totalVdataBytes != 0 && totalAdataBytes != 0) {
                    fwrite(a->sample, 192*4, 1, afp);
                }

                SDL_QueueAudio(dev, a->sample, 192*4 );
            } else if(r == -1) {
                printf("SDLNet_UDP_Recv error: %s\n", SDLNet_GetError());
            }
        }

        // Check for video
        r = SDLNet_UDP_Recv(udpsock, pkg);
        if(r==1 && !showHelp) {

            if(totalVdataBytes==0) {
                printf("Got data on video port (%i) from %s:%i\n", listen, intToIp(pkg->address.host),pkg->address.port );
            }
            totalVdataBytes += sizeof(u64msg_t);

            u64msg_t *p = (u64msg_t*)pkg->data;
            if(verbose) chkSeq("UDP video packet missed or out of order, last received: %i current %i\n", &lastVseq, p->seq);

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
                if(vfp && totalVdataBytes != 0 && totalAdataBytes != 0) {
                    fwrite(pixels, sizeof(uint32_t)*width*height, 1, vfp);
                }
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

    if(hostName && stopStreamOnExit) {
        stopStream(hostName);
    }

    SDL_DestroyTexture(tex);
    if(audioFlag) {
        SDL_CloseAudioDevice(dev);
    }

    // The logic being that if opening either went south, we already exited.
    if(vfp) {
        fclose(vfp);
        fclose(afp);
    }
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_Quit();

    if(verbose) {
        printf("\nReceived video data: %"PRIu64" bytes.\nReceived audio data: %"PRIu64" bytes.\n", totalVdataBytes, totalAdataBytes);
    }
    printf("\n\nThanks to Jens Blidon and Markus Schneider for making my favourite tunes!\nThanks to Booze for making the best remix of Chicanes Halcyon and such beautiful visuals to go along with it!\nThanks to Gideons Logic for the U64!\n\n                                    - DusteD says hi! :-)\n\n");
    return 0;
}
