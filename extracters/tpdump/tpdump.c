/* Dump tapes in the format of the simh 1401 simulator.
   See "usage" function below.
*/

#include <stdio.h>

/* From i1401_dat.h: */

char bcd_to_ascii_old[64] = {
    ' ',   '1',   '2',   '3',   '4',   '5',   '6',   '7',
    '8',   '9',   '0',   '#',   '@',   ':',   '>',   '(',
    '^',   '/',   'S',   'T',   'U',   'V',   'W',   'X',
    'Y',   'Z',   '\'',  ',',   '%',   '=',   '\\',  '+',
    '-',   'J',   'K',   'L',   'M',   'N',   'O',   'P',
    'Q',   'R',   '!',   '$',   '*',   ']',   ';',   '_',
    '&',   'A',   'B',   'C',   'D',   'E',   'F',   'G',
    'H',   'I',   '?',   '.',   ')',   '[',   '<',   '"'
    };

char bcd_to_ascii_a[64] = {
    ' ',   '1',   '2',   '3',   '4',   '5',   '6',   '7',  
    '8',   '9',   '0',   '#',   '@',   ':',   '>',   '{',  
    '^',   '/',   'S',   'T',   'U',   'V',   'W',   'X',  
    'Y',   'Z',   '|',   ',',   '%',   '~',   '\\',  '"',  
    '-',   'J',   'K',   'L',   'M',   'N',   'O',   'P',  
    'Q',   'R',   '!',   '$',   '*',   ']',   ';',   '_',  
    '&',   'A',   'B',   'C',   'D',   'E',   'F',   'G',  
    'H',   'I',   '?',   '.',   ')',   '[',   '<',   '}'
    };

char bcd_to_ascii_h[64] = {
    ' ',   '1',   '2',   '3',   '4',   '5',   '6',   '7',  
    '8',   '9',   '0',   '=',   '\'',  ':',   '>',   '{',  
    '^',   '/',   'S',   'T',   'U',   'V',   'W',   'X',  
    'Y',   'Z',   '|',   ',',   '(',   '~',   '\\',  '"',  
    '-',   'J',   'K',   'L',   'M',   'N',   'O',   'P',  
    'Q',   'R',   '!',   '$',   '*',   ']',   ';',   '_',  
    '+',   'A',   'B',   'C',   'D',   'E',   'F',   'G',  
    'H',   'I',   '?',   '.',   ')',   '[',   '<',   '}'
    };

/* Interesting BCD characters, from i1401_defs.h */

#define BCD_BLANK       000
#define BCD_ALT         020
#define BCD_WM          035

size_t read_len ( FILE* fi )
/* Read a little-endian four-byte number */
{ unsigned char c;      /* A Character from the file */
  size_t i;
  size_t lc;            /* The character, as a long int */
  size_t n;             /* the number */
  if ( fread ( &c, 1, 1, fi ) == 0 )
    return (0);
  n = c; i = 256;
  if ( fread ( &c, 1, 1, fi ) == 0 )
    return (0);
  n += i*c;
  i *= 256;
  if ( fread ( &c, 1, 1, fi ) == 0 )
    return (0);
  n += i*c;
  i *= 256;
  if ( fread ( &c, 1, 1, fi ) == 0 )
    return (0);
  n += i*c;
  i *= 256;
  return (n);
}

void usage ( char* argv[] )
{ printf ( "Usage: %s [options] <input_file>\n", argv[0] );
  printf ( " Options: -w to print word marks on a separate line)\n" );
  printf ( "          -# number of 'files' to print (default 1)\n");
  printf ( "          -a print all of each record, including blank lines,\n");
  printf ( "             which are otherwise suppressed (except for the last one)\n");
  printf ( "          -e E11 format, i.e., don't require even-length records\n" );
  printf ( "          -h Use the H (Fortran) print arrangement\n");
  printf ( "          -o Use the 'old simh' print arrangement\n");
}

main ( int argc, char* argv[] )
{ FILE* fi;
  int all;              /* "print all of a record, even blank lines" */
  char* bcd_to_ascii;   /* Translation table, ..._a or ..._h */
  int cl;               /* Command line argument index */
  int dowm;             /* "do word marks" */
  int even;             /* Require even-length records */
  int i, j;             /* Subscript, loop inductor */
  int nb;               /* Number of characters in print buffer so far */
  int nf, nft;          /* Number of "files" */
  int np;               /* Number of characters processed so far */
  int nr;               /* Number of records dumped so far */
  char pr[101];         /* Buffer to print tape contents */
  size_t recsiz, recsiz2;    /* Record size before, after */
  char wm[101];         /* Buffer to print word marks */

  if ( argc < 2 )
  { usage( argv );
    return(1);
  }

  all = 0;
  bcd_to_ascii = bcd_to_ascii_a;
  dowm = 0;
  even = 1;
  nf = 1;
  for ( cl=1; cl<=argc; cl++ )
  { if ( argv[cl][0] != '-' ) break;
    if ( argv[cl][1] == 'w' ) dowm = 1;
    else if ( argv[cl][1] == 'a' ) all = 1;
    else if ( argv[cl][1] == 'e' ) even = 0;
    else if ( argv[cl][1] == 'h' ) bcd_to_ascii = bcd_to_ascii_h;
    else if ( argv[cl][1] == 'o' ) bcd_to_ascii = bcd_to_ascii_old;
    else if ( sscanf(argv[cl], "%d", &nft ) ) nf = -nft;
    else
    { usage ( argv );
      return(1);
    }
  }

  fi = fopen ( argv[cl], "r" );
  if ( fi == NULL )
  { printf ( "Unable to open %s\n", argv[cl] );
    return(2);
  }
  bcd_to_ascii[BCD_BLANK] = bcd_to_ascii[BCD_ALT]; /* use '^' for blank */
  nft = 1;
  if ( nf > 1 ) printf ( "File %d\n", nft );
  while ( nf > 0 )
  { nf--;
    for ( np=0; np<100; pr[np++]='.' ); 
    pr[100] = '\0';
    for ( np=5; np<=100; np+=5 )    /* Print a row of dots and column numbers */
      for ( nb=1, i=np; i>0; i/=10, nb++ ) pr[np-nb] = '0' + ( i%10 );
    printf ( "       %s\n", pr );
    nr = 0;
    while ( recsiz = read_len ( fi ) )
    { nb = 1;
      nr++;
      i = recsiz;
      while ( i )
      { for ( np=0; np<101; pr[np]='^',wm[np++]=' ') ; /* Clear buffers */
        for ( np=0; i>0 && np<100; np++ )
        { if ( fread ( &pr[np], 1, 1, fi ) <= 0 )
          { printf ( "Error reading %s\n", argv[cl] );
            return(3);
          }
          i--;
          pr[np] = bcd_to_ascii[pr[np]];
          if ( dowm )
            if ( pr[np] == bcd_to_ascii[BCD_WM] ) wm[np--] = '1';
        }
        for ( j=np--; j>=0 && pr[j] == bcd_to_ascii[BCD_ALT] && 
                              wm[j] == ' '; j-- ) ;
        if ( ( j >= 0 ) || ( nb == 1 ) || all || ( i == 0 ) )
        { printf ( "%5d: ", nb );
          pr[np+1] = '\0';
          printf ( "%s", pr );
          if ( nb == 1 )
          { for ( j=np ; j++<100 ; printf ( " " ) ) ;
            printf ( "  Record %d", nr );
          }
          printf ( "\n" );
          if ( dowm )
          { for ( ; np >= 0 && wm[np] == ' '; np-- );
            wm[++np] = '\0';
            printf ( "       %s\n", wm );
          }
        } 
        nb += 100;
      }
      if ( (recsiz & 1) && even ) fread ( pr, 1, 1, fi );
      recsiz2 = read_len ( fi );
      if ( recsiz2 != recsiz )
      { printf("Unequal starting and ending record sizes: %d != %d\n",
          recsiz, recsiz2);
        return(4);
      }
    }
    if ( nf > 0 ) printf ( "File %d\n", ++nft );
  }
}
