WUT
===
A viewer for the UDP VIC video streaming function in the Ultimate 64!

I know this works on V1.21 -3.5a, it probably will work on newer, but maybe not older firmwares.

It listens for video on port 11000.

It listens for audio on port 11001.

![shot of the screen](https://github.com/DusteDdk/u64view/blob/master/screenIsHot.jpeg)

WHAT
===
You can change listening port, and colors in main.c go forth and make it great!

HOW
===
To compile you need to install:
* libsdl2-dev
* libsdl2-net-dev
* gcc
* make

Type:
make

To execute
==========
./u64view

For help
========
./u64view -h

License
=======
wtfpl

Windows ? Mac OSX ?
===================
I've been told that this builds and works fine on OSX, I don't know about windows, but I expect it works fine if you compile it.

For Windows users there is also another Ultimate 64 viewer, called "U64 Streamer" made by Martijn Wieland (TSB), check it out at https://www.tsb.space/projects/u64-streamer/
