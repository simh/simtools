/*
 * A little tool to convert files with variable records to byte streams.
 *
 * Each record consist of 2 bytes of length (little endian) followed by
 * that number of data bytes.
 *
 * If the length is odd, there is a padding byte. This byte does not have
 * to be 0.
 */
#include <stdio.h>

int main(int argc, char **argv)
{
    while (!feof(stdin)) {
	int count, savecount;
	unsigned char ch;

	ch = getchar();
	count = ch;
	ch = getchar();
	count += ch << 8;

	savecount  = count;

	while (count-- > 0) {
	    ch = getchar();
	    putchar(ch);
	}

	if (savecount & 1) {
	    getchar();
	}
    }
}
