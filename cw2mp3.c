/*
  cw2mp3.c
  Make an .MP3/.WAV audio file with morse code, for practicing
  
  Requires GCC and the LAME MP3 encoding library (tested with 3.98.2).

   Encoding timing rules (From http://en.wikipedia.org/wiki/Morse_code):
   short mark, dot or 'dit'    1
   longer mark, dash or 'dah'  111
   intra-character gap         0
   short gap (between letters) 000
   medium gap (between words)  0000000

    Copyright (c) 2010-2014, Eric Shalov.
    All rights reserved.

    Redistribution and use in source and binary forms, with or without
    modification, are permitted provided that the following conditions are met:

    * Redistributions of source code must retain the above copyright notice, this
      list of conditions and the following disclaimer.

    * Redistributions in binary form must reproduce the above copyright notice,
      this list of conditions and the following disclaimer in the documentation
      and/or other materials provided with the distribution.

    * Neither the name of the {organization} nor the names of its
      contributors may be used to endorse or promote products derived from
      this software without specific prior written permission.

    THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
    AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
    IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
    DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
    FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
    DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
    SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
    CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
    OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
    OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#define _GNU_SOURCE
#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include <malloc.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

#include <lame/lame.h>

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
  "+",".-.-.",
  "-","-....-",
  ",","--..--",
  " ","|",
  NULL
};

FILE *output_file = NULL;
int units = 0;

#define WAV 1
#define MP3 2

int format = MP3;

int hz = 44100, bytes_per_second, bytes_per_sample = 1;
int freq = 1000;
float dit_length = 0.050; /* seconds, 50ms units = 0.050s ~= 20wpm  */
int inter_word_dits = 7;
int inter_letter_dits = 3;
int dit_samples;
int max_dits_per_write;

/* For .wav's */
int package_length;
int dit_bytes;

lame_global_flags *lame_handle;
unsigned char *mp3_buffer = NULL;
int mp3_buffer_size;
int mp3_quality = 7;
int mp3_bitrate = 16;

short int *pcm_buffer;

int verbose = 0;

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

  int lame_ret;

  time_t unixtime;
  struct tm *tm;
  
  char id3_string[128];


  while ((opt = getopt(argc, argv, "fh:o:r:q:w:vF:")) != -1) {
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
        
      case 'r':
        mp3_bitrate = atoi(optarg);
        break;
        
      case 'q':
        mp3_quality = atoi(optarg);
        break;
        
      case 'v':
        verbose = 1;
        break;
        
      case 'F':
        if(strcmp(optarg,"mp3")==0) format = MP3;
        else if(strcmp(optarg,"wav")==0) format = WAV;
        else {
          fprintf(stderr,"Format must be 'wav' or 'mp3'.\n");
          exit(1);
        }
        break;
        
      case '?':
      default:
        printf("Usage: cw2mp3 [-v] [f] [-w 20] [-h 700] [-o output-file.mp3] [-F mp3 | wav] [textfile-name]\n"
          "\t(-f can be used multiple times to go into double or triple Farnsworth timing.)\n"
          "\t(-w 20 for 20wpm)\n"
          "\t(-h 700 for a 700 Hz tone instead of 1,000 Hz default)\n"
          "\t(-o filename.mp3 specifies the output filename)\n"
          "\t(-F mp3 | wav specifies output format)\n"
          "\t(-r 128 to set MP3 output encoding bitrate)\n"
          "\t(-q 9 to set algorithm quality: 0=best/slow, 9=worst/fast)\n"
          "\t(-v for verbose output)\n"
          "\ti.e. $ ./cw2mp3 -w 10 -ff message.txt\n"
          "\tUsing LAME library version %s\n",
          get_lame_version()
        );
        exit(1);
    }
  }
  argc -= optind;
  argv += optind;
  filename = argv[0];

  if(!filename) {
    fprintf(stderr,"cw2mp3: You must specify an input filename.\n");
    exit(1);
  }

  /* If no output filename has been provided, base it on the input filename */  
  if(!output_filename) {
    if(strchr(filename,'.')) {
      output_filename = malloc(strchr(filename,'.')-filename+1+3+1);
      strncpy(output_filename,filename,strchr(filename,'.')-filename);
      sprintf(output_filename+(strchr(filename,'.')-filename),format==MP3?".mp3":".wav");
    }
    
    else asprintf(&output_filename,"%s.%s", filename, format==MP3?"mp3":"wav");
  }


  /* Deterine some timing calculations */            
  bytes_per_second = hz;
  dit_samples = (dit_length * hz);
  max_dits_per_write = 40;

  mp3_buffer_size = 2*dit_samples*max_dits_per_write+7200;
  mp3_buffer = malloc(mp3_buffer_size);

  pcm_buffer = malloc(dit_samples*max_dits_per_write * sizeof(short int));

  /* For wav's */
  dit_bytes = (dit_length * hz);


  /* Initialize our output file */
  if( ! (output_file = fopen(output_filename,"w+")) ) {
    perror("fopen");
    exit(1);
  }

  if(format == MP3) {
    lame_handle = lame_init();
    /*
    lame_set_errorf(lame_handle,error_handler_function);
    lame_set_debugf(lame_handle,error_handler_function);
    lame_set_msgf(lame_handle,error_handler_function);
    */
  }

  if(format == MP3) {
    /* Make the ID3v2 tag */
    id3tag_add_v2(lame_handle);
    sprintf(id3_string,"Morse code at %.0f wpm of %s", 1.0/dit_length, filename);
    id3tag_set_title(lame_handle, id3_string);
    id3tag_set_artist(lame_handle,"cw2mp3");
    id3tag_set_album(lame_handle,"CW");
    time(&unixtime);
    tm = localtime(&unixtime);
    strftime(id3_string,128,"%Y",tm);
    id3tag_set_year(lame_handle, id3_string);
    id3tag_set_comment(lame_handle,"Generated by cw2mp3.c by Eric Shalov");
    id3tag_set_genre(lame_handle,"CW");

    lame_set_num_channels(lame_handle,1); /* mono */
    lame_set_in_samplerate(lame_handle, hz);
    lame_set_brate(lame_handle, mp3_bitrate); /* 128 = 128kHz MP3 */
    lame_set_mode(lame_handle,1);
    lame_set_quality(lame_handle, mp3_quality);   /* 2=high  5 = medium  7=low */
    if( (lame_ret = lame_init_params(lame_handle)) < 0) {
      fprintf(stderr, "lame_init_params() returned %d, exiting.\n", lame_ret);
      exit(1);
    }
  }  
  if(format == WAV) {
    /* the header */
    fprintf(output_file,
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
    
  }
  
  /* Initialize our input file */    
  if( ! (f=fopen(filename,"r")) ) {
    perror("fopen");
    exit(1);
  }

  /* Read the input */
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



  /* Read in the text to encode */  
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

  /* Add AR (+) to end of message */
  encoded=realloc(encoded,strlen(encoded)+6+1);
  strcat(encoded,"|.-.-.");

  if(encoded && verbose) {
    printf("%s\n", encoded);
  }



  /* Encode the text into morse */
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


  /* Tidy up and close the output file */
  
  if(format == WAV) {
    fseek(output_file,4,SEEK_SET); /* rewind and fill in the data length */
    package_length = (units * dit_bytes) +12+24-8;
    fprintf(output_file,"%c%c%c%c",
      (package_length&0x000000FF),
      ((package_length&0x0000FF00) >> 8),
      ((package_length&0x00FF0000) >>16),
      ((package_length&0xFF000000) >>24)
    );
    fseek(output_file,40,SEEK_SET); /* rewind and fill in the data length */
    package_length = (units * dit_bytes);
    fprintf(output_file,"%c%c%c%c",
      (package_length&0x000000FF),
      ((package_length&0x0000FF00) >> 8),
      ((package_length&0x00FF0000) >>16),
      ((package_length&0xFF000000) >>24)
    );
  }

  if(format == MP3) {
    free(pcm_buffer);
    lame_encode_flush(lame_handle, mp3_buffer, mp3_buffer_size);
    free(mp3_buffer);
    lame_mp3_tags_fid(lame_handle, output_file);
    lame_close(lame_handle); 
  }

  fclose(output_file);
  fclose(f);  
  
  return 0;
}

void write_tone(int units) {
  int pcm_samples, mp3_bytes;
  int i;

  if(format == MP3) {
    pcm_samples = dit_samples * units;
    for(i=0;i<pcm_samples;i++) pcm_buffer[i] =  (i/(hz/freq))&1?16384:-16383;

    mp3_bytes = lame_encode_buffer(lame_handle,pcm_buffer,NULL,pcm_samples,mp3_buffer,mp3_buffer_size);
    fwrite(mp3_buffer,1,mp3_bytes,output_file);
  }
  
  if(format == WAV) {
    for(i=0;i<(dit_bytes*units);i++) fprintf(output_file, "%c", (i/(hz/freq))&1?64:-63);
  }
}

void write_space(int units) {
  int pcm_samples, mp3_bytes;
  int i;

  if(format == MP3) {
    pcm_samples = dit_samples * units;
    memset(pcm_buffer,0,pcm_samples * sizeof(short int));

    mp3_bytes = lame_encode_buffer(lame_handle,pcm_buffer,NULL,pcm_samples,mp3_buffer,mp3_buffer_size);
    fwrite(mp3_buffer,1,mp3_bytes,output_file);
  }
  
  if(format == WAV) {
    for(i=0;i<(dit_bytes*units);i++) fprintf(output_file, "%c", 0x00);
  }
}
