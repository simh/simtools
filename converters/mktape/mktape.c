/*

 SIMH tape image generating tool

Collect scattered files and make a tape image for SIMH. Typical usage:

	mktape boot -b 10240 root.dump usr.tar > dist.tape

"-b" option indicates block size by byte (default 512) for
following files. You can make an empty tape by specifying no
arguments.

	mktape > null.tape

For details of the SIMH tape image format, refer to: 
"SIMH Magtape Representation and Handling"
http://simh.trailing-edge.com/docs/simh_magtape.pdf

12 Mar 2007
Naoki Hamada
nao at tom hyphen yam dot or dot jp

 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

void usage()
{
  fprintf(stderr, "usage: mktape [-b blocklen] [file1] [-b blocklen] [file2] ...\n");
  exit(1);
}

/*
 write longword in little endian order to stdout
 */

void wl(int len)
{
  unsigned char c[4];

  c[0] = len & 0xff;
  c[1] = (len & 0xff00) >> 8;
  c[2] = (len & 0xff0000) >> 16;
  c[3] = (len & 0xff000000) >> 24;

  fwrite(c, 1, 4, stdout);
}

/*
 read from file and write blocked data to stdout
 */

void fblock(char *name, int blen)
{
    FILE *f;
    size_t ret;
    void *b;

    f = fopen(name, "r");
    if (f == NULL) {
      perror(name);
      exit(1);
    }
    b = malloc(blen);
    if (b == NULL) {
      perror("malloc:");
      exit(1);
    }
    while (!feof(f)) {
      memset(b, 0, blen);
      ret = fread(b, blen, 1, f);
      if (ret < blen) {
	if (ferror(f)) {
	  perror(name);
	  exit(1);
	}
      }
      if (ret == 0)
	continue;
      wl(blen);
      fwrite(b, blen, 1, stdout);
      wl(blen);
    }
    if (fclose(f) != 0) {
      perror(name);
      exit(1);
    }
}

int main(int argc, char **argv)
{
  int i = 0;
  int blen = 512;

  while (++i < argc) {
    if (strcmp(argv[i], "-b") == 0){
      if (++i == argc)
	usage();
      blen = atoi(argv[i++]);
      if (blen <= 0)
	usage();
    }
    fblock(argv[i], blen);
    wl(0);
  }
  wl(0);

  return 0;
}

