/* Convert an HP disc image between SIMH and HPDrive formats.

   Copyright (c) 2012, J. David Bryan

   Permission is hereby granted, free of charge, to any person obtaining a copy
   of this software and associated documentation files (the "Software"), to deal
   in the Software without restriction, including without limitation the rights
   to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
   copies of the Software, and to permit persons to whom the Software is
   furnished to do so, subject to the following conditions:

   The above copyright notice and this permission notice shall be included in
   all copies or substantial portions of the Software.

   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
   IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
   FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
   AUTHOR BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
   ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
   WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

   Except as contained in this notice, the name of the author shall not be used
   in advertising or otherwise to promote the sale, use or other dealings in
   this Software without prior written authorization from the author.


   HPDrive is a free program written by Ansgar Kueckes that uses a PC and a GPIB
   card to emulate a variety of vintage Hewlett-Packard disc and tape drives
   that interface to computers via the HP-IB.  It is available from:

     http://www.hp9845.net/9845/projects/hpdrive/

   This program converts an HP disc image from SIMH to HPDrive format or
   vice-versa.  This permits interchanging images between the two programs.

     Usage: hpconvert <disc-image-file>

   SIMH writes and reads disc images in little-endian format, regardless of the
   byte order of the host.  In addition, SIMH accesses images for the the 7905
   and 7906 drives in platter order (i.e., all tracks on heads 0 and 1, followed
   by all tracks on heads 2 and 3) to improve locality of access.  It accesses
   images for the 7920, 7925, and all CS/80 drives in cylinder order.

   HPDrive writes and reads images in big-endian format, regardless of the byte
   order of the host, and accesses all images in cylinder order.

   This program swaps each pair of bytes in the disc image.  In addition, if the
   image is precisely the size of a 7905 or 7906 drive (15,151,104 or 20,201,472
   bytes, respectively), the access order is restructured from platter to
   cylinder, or vice-versa.

   Note that SIMH does not currently create full-size disc images unless the
   last sector on the drive is written.  Therefore, it is recommended that new
   images be initialized using the RTE FORMT or SWTCH programs to ensure that
   the image files are of the correct size for this program and HPDrive.

   This program creates a scratch file to store the converted image.  Only when
   conversion is complete is the original file deleted and the scratch file
   renamed to the original file's name.  Running the program twice on the same
   file will return the file to its original configuration.

   To decide the mode used in a 7905 or 7906 image, the program examines the OS
   signature at the start of the file and compares it against a list of known
   operating systems.  If the OS signature is not recognized, the program
   terminates without altering the file.  Signature checking is not performed on
   images other than those for the 7905 and 7906; those images are byte-swapped
   unconditionally.

   Signatures for these operating systems and compatible 7905/06 drives are
   recognized:

     - HP 1000 RTE-IVB:  7906H ICD
     - HP 1000 RTE-6/VM: 7906H ICD
     - HP 3000 MPE:      7905A/7906A MAC

   The signatures are contained in the first four words at the start of each
   disc image.  In hex representation, they are:

     - RTE-IVB  ICD: 676D 06C0 776B 0B40 (LDA HHIGH ; CMA,CCE ; STA HRCNT ; ERB)
     - RTE-6/VM ICD: 676D 06C0 776B 0B40 (LDA HHIGH ; CMA,CCE ; STA HRCNT ; ERB)
     - MPE MAC:      5359 5354 454D 2044 ("SYSTEM D")

   These represent the start of the boot extension for RTE and the start of the
   system disc label for MPE.  The boot extension machine instructions are:

     RTE-IVB ICD
     -----------

       62000         LDA   EQU 062000B
       72000         STA   EQU 072000B
       01200         O0    EQU HSTRT-1400B

       02600 063555  HSTRT ABS LDA+HHIGH-O0   (word 1)
       02601 003300            CMA,CCE        (word 2)
       02602 073553        ABS STA+HRCNT-O0   (word 3)
       02603 005500            ERB            (word 4)

       02753 000000  HRCNT NOP

       02755 077377  NW#DS OCT 77377
       02755         HHIGH EQU NW#DS

     RTE-6/VM ICD
     ------------

             062000  LDA   EQU 062000B
             072000  STA   EQU 072000B
             000000R O0    EQU HSTRT-1400B

       01400 063555  HSTRT ABS LDA+HHIGH-O0   (word 1)
       01401 003300            CMA,CCE        (word 2)
       01402 073553        ABS STA+HRCNT-O0   (word 3)
       01403 005500            ERB            (word 4)

       01553 000000  HRCNT NOP

       01555 077377  NW#DS OCT 77377
             001555R HHIGH EQU NW#DS

   And the disc label is:

     MPE MAC and CS/80
     -----------------

       00000 051531  LABEL ASC 6,SYSTEM DISC  (word 1)
       00001 051524                           (word 2)
       00002 042515                           (word 3)
       00003 020104                           (word 4)
       00004 044523
       00005 041440

*/


#include <memory.h>
#include <stdio.h>


/* MSVC does not provide "stdbool.h" or "unistd.h" */

#if defined (_MSC_VER)
  typedef int bool;
  const   int false = 0;
  #define isatty _isatty
#else
  #include <stdbool.h>
  #include <unistd.h>
#endif


#define  TRACK_SIZE         (  1 * 1 * 48 * 256)
#define  CYLINDER_SIZE      (  1 * 2 * 48 * 256)
#define  HP7905_SIZE        (411 * 3 * 48 * 256)
#define  HP7906_SIZE        (411 * 4 * 48 * 256)


int main (int    argc,
          char **argv)

{
    const char *format [] = { "HPDrive", "SIMH" };

    const char *signatures [] = { "\x67\x6D\x06\xC0\x77\x6B\x0B\x40",   /* RTE ICD */
                                  "SYSTEM D" };                         /* MPE */

    #define  SIGNATURE_SIZE  sizeof (signatures [0])

    const int signature_count = sizeof (signatures) / SIGNATURE_SIZE;

    FILE   *fin, *fout;
    size_t file_size, record_size;
    char   *name_in, *name_out;
    char   sig_fwd [SIGNATURE_SIZE], sig_rev [SIGNATURE_SIZE];
    char   hold, cylinder [CYLINDER_SIZE];
    bool   identified = false, reversed = false, debug = false;
    int    i, cyl, from_cyl, to_cyl, remap;
    int    platter, cylinder_size, hole_size;


/* Read the disc image filename. */

    if (argc != 2) {
        puts ("\nHPConvert version 1.1");
        puts ("\nUsage: hpconvert <disc-image>");
        return 1;
        }

    name_in = argv [1];

/* Open the source image file. */

    fin = fopen (name_in, "rb");

    if (!fin) {
        printf ("Error: cannot open %s\n", name_in);
        return 1;
        }

/* Get the size of the image. */

    fseek (fin, 0, SEEK_END);
    file_size = ftell (fin);


/* The blocks of a 7905 or 7906 image (as determined by the image file size)
   will need to be rearranged.  Set "remap" to the number of surfaces that must
   be remapped. */

    if (file_size == HP7905_SIZE)
        remap = 1;
    else if (file_size == HP7906_SIZE)
        remap = 2;
    else
        remap = 0;


/* If the image is a 7905 or 7906, it must be remapped it for the target system.
   To do that, check the OS signature to determine if it is in SIMH or HPDrive
   format, and set the "reversed" flag if the signature bytes in the image are
   reversed (this implies a platter-to-cylinder remapping of a 7905 or 7906
   image. */

    if (remap) {
        rewind (fin);
        file_size = fread (sig_fwd, 1, SIGNATURE_SIZE, fin);

        for (i = 0; (size_t)i < SIGNATURE_SIZE; i = i + 2) {
            sig_rev [i]     = sig_fwd [i + 1];
            sig_rev [i + 1] = sig_fwd [i];
            }

        for (i = 0; i < signature_count && identified == false; i++) {
            reversed = strncmp (sig_rev, signatures [i], SIGNATURE_SIZE) == 0;
            identified = reversed || strncmp (sig_fwd, signatures [i], SIGNATURE_SIZE) == 0;
            }

/* If the signature cannot be identified, then we do not know how to remap it,
   so report the problem and exit. */

        if (identified == false) {
            printf ("Error: 790%i image OS signature not recognized.\n", remap + 4);
            fclose (fin);
            return 1;
            }
        }


/* Generate a temporary filename for the converted image. */

    name_out = tmpnam (NULL);

    if (name_out == NULL) {
        puts ("Error: cannot generate a temporary filename.");
        fclose (fin);
        return 1;
        }

/* Create the temporary target image. */

    fout = fopen (name_out, "wb");

    if (!fout) {
        printf ("Error: cannot create %s\n", name_out);
        fclose (fin);
        return 1;
        }

/* Report the conversion that will be performed. */

    if (remap)
        printf ("Converting and remapping 790%i %s disc image to %s format.\n",
                remap + 4,
                format [reversed],
                format [!reversed]);
    else
        puts ("Converting disc image.");


/* Enable debugging output if stdout has been redirected to a file. */

    debug = isatty (fileno (stdout)) == 0;


/* Copy the source to the target image while swapping each byte pair.  If the
   disc image is for a 7905 or 7906, remap from platter to cylinder mode (or
   vice-versa if the source image is not reversed).

   In a platter-mode image, the upper platter tracks appear in order before the
   lower platter tracks.  For the 7906, the cylinder-head order of the tracks is
   0-0, 0-1, 1-0, 1-1, ..., 410-0, 410-1, 0-2, 0-3, 1-2, 1-3, ..., 410-2, 410-3.
   The 7905 order is the same, except that head 3 tracks are omitted.

   In a cylinder-mode image, all tracks appear in cylinder-head order, i.e.,
   0-0, 0-1, 0-2, 0-3, 1-0, 1-1, ..., 410-2, 410-3, for the 7906.

   Remapping is performed in two passes, corresponding to the two platters.  In
   the first pass, the source tracks corresponding to the upper platter are
   spread (platter-to-cylinder) or condensed (cylinder-to-platter) as they are
   copied to the target file.  In the second pass, the source tracks
   corresponding to the lower platter are interleaved (platter-to-cylinder) or
   appended (cylinder-to-platter) as they are copied to the target file.

   In either case, the bytes of each 16-bit word are swapped before copying.
*/

    rewind (fin);

/* If the image is not a 7905/06, simply swap each pair of bytes until EOF. */

    if (!remap)
        while (!feof (fin)) {
            record_size = fread (cylinder, 1, CYLINDER_SIZE, fin);

            for (i = 0; (size_t)i < record_size; i = i + 2) {
                hold = cylinder [i];
                cylinder [i] = cylinder [i + 1];
                cylinder [i + 1] = hold;
                }

            fwrite (cylinder, 1, record_size, fout);
            }

/* If the image is a 7905/06, remap the tracks and swap pairs of bytes.  Because
   we know the disc type, we know the number of platters and cylinders present
   in the image. */

    else
        for (platter = 0; platter < 2; platter++) {

/* Calculate the number of bytes per cylinder for the current platter.  The
   upper platter always has two tracks per cylinder.  The lower platter has
   either one (7905) or two (7906) tracks per cylinder. */

            if (platter == 0) {
                cylinder_size = TRACK_SIZE * 2;
                hole_size     = TRACK_SIZE * remap;
                }
            else {
                cylinder_size = TRACK_SIZE * remap;
                hole_size     = TRACK_SIZE * 2;
                }

/* Copy a platter. */

            for (cyl = 0; cyl < 411; cyl++) {

/* If stdout has been redirected, output the remapping information. */

                if (debug) {
                    from_cyl = ftell (fin) / TRACK_SIZE;
                    to_cyl   = ftell (fout) / TRACK_SIZE;

                    if (platter == 1 && remap == 1)
                        printf ("track %i => %i\n", from_cyl, to_cyl);
                    else
                        printf ("track %i, %i => %i, %i\n",
                                from_cyl, from_cyl + 1, to_cyl, to_cyl + 1);
                    }

/* Read a cylinder from the source location. */

                record_size = fread (cylinder, 1, cylinder_size, fin);

/* Swap the bytes. */

                for (i = 0; (size_t)i < record_size; i = i + 2) {
                    hold = cylinder [i];
                    cylinder [i] = cylinder [i + 1];
                    cylinder [i + 1] = hold;
                    }

/* Write the cylinder to the target location. */

                fwrite (cylinder, 1, record_size, fout);

/* For platter-to-cylinder remapping, spread the tracks by seeking ahead in the
   target file; this leaves room for the lower-platter tracks in between the
   upper-platter tracks.  For cylinder-to-platter remapping, condense the
   cylinders by seeking ahead in the source file to the next track on the
   current platter. */

                if (reversed)
                    fseek (fout, hole_size, SEEK_CUR);
                else
                    fseek (fin, hole_size, SEEK_CUR);
                }


/* End of the current platter.  For platter-to-cylinder remapping, reposition
   the target file to the first "hole" left for the lower-platter tracks.  For
   cylinder-to-platter remapping, reposition the source file to access the first
   lower-platter tracks. */

            if (reversed)
                fseek (fout, cylinder_size, SEEK_SET);
            else
                fseek (fin, cylinder_size, SEEK_SET);
            }


/* Close the files. */

    fclose (fin);
    fclose (fout);


/* Delete the original file and replace it by the reversed file. */

    if (unlink (name_in)) {
        puts ("Error: cannot replace original file, which is unchanged.");
        unlink (name_out);
        return 1;
        }

    else if (rename (name_out, name_in)) {
        printf ("Error: cannot rename temporary file %s.\n", name_out);
        return 1;
        }


/* Return success. */

    return 0;
}
