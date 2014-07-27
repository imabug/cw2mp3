#
#	Makefile for cw2mp3
#

all: cw2mp3

cw2mp3: cw2mp3.c
	$(CC) -Wall -o cw2mp3 cw2mp3.c -L/usr/local/lib -lmp3lame

samples: cw2mp3
	./cw2mp3 -F mp3 -r64 -q7 -w 5 -h700 -f message1.txt
	./cw2mp3 -F mp3 -r64 -q7 -w 10 -h900 -ff message2.txt
	./cw2mp3 -F mp3 -r64 -q7 -w 15 -h1100 -ffff message3.txt
