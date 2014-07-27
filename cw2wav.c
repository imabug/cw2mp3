/*
  cw2wav.c v1.1 7dec2009
  Make a .WAV audio file with morse code, for practicing
  (C) Copyright 2009 Eric Shalov. All rights reserved.

   Encoding timing rules (From http://en.wikipedia.org/wiki/Morse_code):
   short mark, dot or 'dit'    1
   longer mark, dash or 'dah'  111
   intra-character gap         0
   short gap (between letters) 000
   medium gap (between words)  0000000
   
   WAV format details from:
   http://technology.niagarac.on.ca/courses/ctec1631/WavFileFormat.html
*/

#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include <malloc.h>
#include <stdlib.h>
#include <unistd.h>

char *morse_table[] = {
  "a",".-",	"b","-...",	"c","-.-.",	"d","-..",
  "e",".",	"f","..-.",	"g","--.",	"h","....",
  "i","..",	"j",".---",	"k","-.-",	"l",".-..",
  "m","--",	"n","-.",	"o","---",	"p",".--.",
  "q","--.-",	"r",".-.",	"s","...",	"t","-",
  "u","..-",	"v","...-",	"w",".--",	"x","-..-",
  "y","-.--",	"z","--..",
  "0","-----",	"1",".----",	"2","..---",	"3","...--",
  "4","....-",	"5",".....",	"6","-....",	"7","--...",
  "8","---..",	"9","----.",
  " ","|",
  NULL
};

FILE *wavfile = NULL;
int units = 0;

int hz = 11025, bytes_per_second, bytes_per_sample = 1;
int freq = 1000;
float dit_length = 0.050; /* seconds, 50ms units = 0.050s ~= 20wpm  */
int inter_word_dits = 7;
int inter_letter_dits = 3;
int dit_bytes;

/* prototypes */
void write_tone(int units);
void write_space(int units);

int main(int argc, char *argv[]) {
  char c;
  char *p, *morse;
  char **search;
  char *encoded = NULL;
  int len = 0;
  char last = '\0';
  FILE *f;
  char *filename = NULL;
  char *output_filename = NULL;

  char opt;

  int package_length;


  while ((opt = getopt(argc, argv, "fh:o:w:")) != -1) {
    switch(opt) {
      case 'f':
        /* Farnsworth method */
        inter_word_dits += 7;
        inter_letter_dits += 3;
        break;
        
      case 'w':
        dit_length = 1.000 / atof(optarg);
        break;
        
      case 'h':
        freq = atoi(optarg);
        break;
        
      case 'o':
        output_filename = strdup(optarg);
        break;
        
      case '?':
      default:
        printf("Usage: cw2wav [-f] [-w 20] [-h 700] [-o output-file.wav]  [textfile-name]\n"
          "\t(-f can be used multiple times to go into double or triple Farnsworth timing.)\n"
          "\t(-w 20 for 20wpm)\n"
          "\t(-h 700 for a 700 Hz tone instead of 1,000 Hz default)\n"
          "\t(-o filename.wav specifies the output filename)\n"
          "\ti.e. $ ./cw2wav -w 10 -ff message.txt\n"
        );
        exit(1);
    }
  }
  argc -= optind;
  argv += optind;
  filename = argv[0];

  if(!filename) {
    fprintf(stderr,"cw2wav: You must specify an input filename.\n");
    exit(1);
  }
  
  if(!output_filename) {
    if(strchr(filename,'.')) {
      output_filename = malloc(strchr(filename,'.')-filename+1+3+1);
      strncpy(output_filename,filename,strchr(filename,'.')-filename);
      sprintf(output_filename+(strchr(filename,'.')-filename),".wav");
    }
    
    else asprintf(output_filename,"%s.wav", filename);
  }

  if( ! (wavfile = fopen(output_filename,"w+")) ) {
    perror("fopen");
    exit(1);
  }
          
  bytes_per_second = hz;
  dit_bytes = (dit_length * hz);

  /* the header */
  fprintf(wavfile,
    "RIFF"
    "%c%c%c%c" /* data length */
    "WAVE"
    "fmt "
    "%c%c%c%c" /* length of format chunk */
    "%c%c" /* always 1 */
    "%c%c" /* channels, 1=mono, 2=stereo */
    "%c%c%c%c" /* sample rate, in hertz */
    "%c%c%c%c" /* bytes per second */
    "%c%c" /* bytes per sample, Bytes Per Sample: 1=8 bit Mono, 2=8 bit Stereo or 16 bit Mono, 4=16 bit Stereo */
    "%c%c" /* bits per sample */
    "data"
    "%c%c%c%c", /* bytes to follow */
    
    0x00,0x00,0x00,0x00, /* data length */
    0x10,0x00,0x00,0x00, /* length of format chunk, always 16, little-endian */
    0x01,0x00, /* always 1 */
    0x01,0x00, /* channels */
    (hz&0x000000FF),(hz&0x0000FF00)>>8,(hz&0x00FF0000)>>16,(hz&0xFF000000)>>24,
    (bytes_per_second&0x000000FF),(bytes_per_second&0x0000FF00)>>8,(bytes_per_second&0x00FF0000)>>16,(bytes_per_second&0xFF000000)>>24,
    (bytes_per_sample%0xFF),(bytes_per_sample&0xFF00)>>8,
    ((bytes_per_sample*8)%0xFF),((bytes_per_sample*8)&0xFF00)>>8,
    0x00,0x00,0x00,0x00 /* bytes of actual data */
  );
  
  if( ! (f=fopen(filename,"r")) ) {
    perror("fopen");
    exit(1);
  }
  
  while((c = fgetc(f)) != EOF) {
    morse = NULL;
    for(search=morse_table;*search;search+=2) {
      if(*search[0] == tolower(c)) morse = search[1];
    }
    if(morse) {
      len += strlen(morse) + 1;
      encoded = realloc(encoded, len+1);
      strcpy(encoded+len-strlen(morse)-1, morse);
      strcpy(encoded+len-1," ");
    }
  }

  if(encoded) {
    printf("%s\n", encoded);
  }
  
  for(p=encoded;*p;p++) {
    if(   (*p == '.' || *p == '-') && (last == '.' || last == '-')  ) {
      units += 1;
      write_space(1);
    }
  
    switch(*p) {
      case '.': write_tone(1);  units += 1; break; /* dit */
      case '-': write_tone(3);  units += 3; break; /* dah */
      case ' ': write_space(inter_letter_dits); units += inter_letter_dits; break; /* inter-letter */
      case '|': write_space(inter_word_dits); units += inter_word_dits; break; /* inter-word */
    }
    
    last = *p;
  }

  fseek(wavfile,4,SEEK_SET); /* rewind and fill in the data length */
  package_length = (units * dit_bytes) +12+24-8;
  fprintf(wavfile,"%c%c%c%c",
    (package_length&0x000000FF),
    ((package_length&0x0000FF00) >> 8),
    ((package_length&0x00FF0000) >>16),
    ((package_length&0xFF000000) >>24)
  );
  fseek(wavfile,40,SEEK_SET); /* rewind and fill in the data length */
  package_length = (units * dit_bytes);
  fprintf(wavfile,"%c%c%c%c",
    (package_length&0x000000FF),
    ((package_length&0x0000FF00) >> 8),
    ((package_length&0x00FF0000) >>16),
    ((package_length&0xFF000000) >>24)
  );

  fclose(wavfile);
  fclose(f);  
  
  return 0;
}

void write_tone(int units) {
  int i;
  for(i=0;i<(dit_bytes*units);i++) fprintf(wavfile, "%c", (i/(hz/freq))&1?64:-63);
}

void write_space(int units) {
  int i;
  for(i=0;i<(dit_bytes*units);i++) fprintf(wavfile, "%c", 0x00);
}
