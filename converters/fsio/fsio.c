/*
 * Copyright (C) 2018 John Forecast. All Rights Reserved.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY JOHN FORECAST "AS IS" AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO
 * EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * Utility for manipulating files with file system container files as used
 * by various emulators such as SIMH.
 */

/*
 * Foreign file system interface (9/18/2019)
 *
 * Each foreign file system is described by a struct FSdef which is linked
 * into a master list of supported file systems in FSioInit(). Each entry
 * in struct FSdef is described here:
 *
 *  struct FSdef *next;
 *
 *      Pointer to the next supported file system. Set up by FSioInit().
 *
 *  char *fstype;
 *
 *      Pointer to the file system name. This is used by the "mount" command
 *      to locate a specific file system.
 *
 *  char *descr;
 *
 *      One line description of the file system. Output by the "help" command.
 *
 *  uint16_t flags;
 *
 *      Flags to control operation of the top-level code:
 *
 *        FS_UNITVALID
 *
 *              Some file systems (e.g. RT11) allow a container file to
 *              contain multiple file systems. Setting this bit allows the
 *              device name parser to include a unit number (8 bit, octal
 *              value) when referencing a file/filesystem. For example:
 *
 *                      mount dk rt11sys.dsk rt11
 *                      dir dk1:
 *
 *              displays the directory of the second partition within the
 *              container file.
 *
 *        FS_TAPE
 *
 *              The file system operates on SIMH .tap format magtape
 *              container files.
 *
 *  size_t blocksz;
 *
 *      The size of a data block for this particular file system. It is
 *      possible for a file system to have different block sizes for
 *      different disks (see dos11 for an example).
 *
 *  int (*mount)(struct mountedFS *mount);
 *
 *      This routine is called when processing a "mount" command. The container
 *      file is open, and this routine should verify that it contains a valid
 *      file system. This routine may print out information about the file
 *      system. Return 1 if the file system is valid, 0 if the file system is
 *      invalid and -1 if the file system is invalid and an erroir message
 *      has already been printed.
 *
 *  void (*umount)(struct mountedFS *mount);
 *
 *      This routine is called when processing an "unmount" command. This
 *      routine is responsible for cleaning up any memory allocations, open
 *      files etc (the framework will close the container file after calling
 *      this routine).
 *
 *  size_t (*size)(void)
 *
 *      Return the number of byte used for the container file. The command
 *      line switch (-t type) may be used to override the default size.
 *
 *  int (*newfs)(struct mountedFS *mount, size_t size);
 *
 *      Create an empty file system in the container file provided by the
 *      mountedFS structure. The size of the container file is provided
 *      in the "size" parameter. The file system is never completely mounted
 *      by this command. This routine returns 1 if the file system was
 *      successfully created, 0 otherwise.
 *
 *  void (*set)(struct mountedFS *mount, uint8_t unit, uint8_t present);
 *
 *      This routine is called when processing a "set" command. The arguments
 *      for the "set" command are passed on to the file system and the
 *      command syntax is handled completely by the file system code.
 *
 *  void (*info)(struct mountedFS *mount, uint8_t unit, uint8_t present);
 *
 *      This routine is called when processing an "info" command. It is most
 *      useful for the file system plug-in developer and should output
 *      information about the file system layout including information which
 *      is not normally available to users. On file systems with
 *      FS_UNITVALID set (see above), "present" is non-zero if a unit number
 *      was included on the command line. For example, RT-11 uses "present"
 *      to decide whether to display information about a single file system
 *      ("present" 0) or all file systems in the containder ("present" 1).
 *
 *  void (*dir)(struct mountedFS *mount, uint8_t unit, char *fname);
 *
 *      This routine is called to process a "dir" command. It should output
 *      information in a format native to the file system. If the "-f"
 *      switch is present (SWISSET('f')), more information may be output.
 *      "fname" is a filename string which may be present to filter the
 *      output. Wild carding may be used but the plug-in is responsible
 *      for parsing and handling wild card characters.
 *
 *  void *(*openFileR)(struct mountedFS *mount, uint8_t unit, char *fname);
 *
 *      Open a file on the specified filesystem for reading. The filename
 *      may not include wild card characters. This routine returns an
 *      opaque pointer as a file handle which will be passed to the
 *      read/write/close routines.
 *
 *  void *(*openFileW)(struct mountedFS *mount, uint8_t unit,
 *                       char *fname, off_t size);
 *
 *      Open a file on the specified filesystem for writing, similar to
 *      openFileR() above. "size" is an estimate of the amount of space
 *      needed for the file (in bytes) which may be used to pre-allocate
 *      disk space. A value of 0 indicates that the system is unable to
 *      determine the amount of space required (e.g. input from /dev/tty).
 *
 *  off_t (*fileSize)(void *filep);
 *
 *      Returns the size, in bytes,  of a file currently open for reading.
 *      If it is not possible to determine the actual file size, return 0.
 *      In all other cases, the returned value may over-estimate, but not
 *      under-estimate the size of the file. For example, linked files
 *      on DOS-11 will overestimate the size by 2 bytes/block.
 *
 *  void (*closeFile)(void *filep);
 *
 *      Close a file which is currently open for reading/writing.
 *
 *  size_t (*readFile)(void *filep, void *buf, size_t buflen);
 *
 *      Read a maximum of "buflen" bytes from the file into the buffer.
 *      If ASCII mode is selected by the "-a" switch (SWISSET('a')), the
 *      read request should terminate at record (line) boundaries and the
 *      buffer should be terminated with a <CRLF> character pair. Other
 *      file system dependent processing may occur in ASCII mode, e.g.
 *      DOS-11 masks characters down to 7-bits while RT-11 looks for ^Z
 *      characters indicating EOF. This routine returns the number of bytes
 *      read from the file, which may be less than the buffer size. If the
 *      file is positioned at EOF when the read request is issued or an
 *      error occurs it should return 0.
 *
 *  size_t (*writeFile)(void *filep, void *buf, size_t buflen);
 *
 *      Writes a maximum of "buflen" bytes from the buffer to the file.
 *      If ASCII mode is selected by the "-a" switch (SWISSET('a')),
 *      if the last 2 characters of the buffer are <CRLF> they should be
 *      replaced by the native line ending character(s) for the file
 *      system. This routine returns the number of bytes written to the
 *      file, 0 if an error occurs.
 *
 *  void (*deleteFile)(void *filep, char *fname)
 *
 *      Delete a file which is currently open for reading. This routine
 *      should perform a closeFile() operation to free up resources. "fname"
 *      is the filename used when opening the file, it may be used in those
 *      cases where it is not possible to delete an open file (e.g. Unix).
 *
 *  void (*rewind)(struct mountedFS *mount)
 *
 *      This function is only valid for magtape container files. Rewind the
 *      container file so that it is postiioned at beginning-of-tape.
 *
 *  void (*eom)(struct mountedFS *mount)
 *
 *      This function is only valid for magtape container files. Position the
 *      container file at the logical end-of-media so that subsequent write
 *      operations will append data to the tape.
 *
 *  void (*skipforw)(struct mountedFS *mount, unsigned long count)
 *
 *      This function is only valid for magtape container files. Skip formward
 *      over the specified number of files or until end-of-media is reached.
 *
 *  void (*skiprev)(struct mountedFS *mount, unsigned long count)
 *
 *      This function is only valid for magtape container files. Skip backwards
 *      over the specified number of files or until beginning-of-tape is
 *      reached.
 *
 *
 * Mounting a file system creates a struct mountedFS which is provided to
 * most routines. Each entry is described here:
 *
 *  struct mountedFS *next;
 *
 *      Pointer to the next mounted file system or NULL.
 *
 *  char name[MAX_DEVLEN+1];
 *
 *      The name provided by the user on the "mount" command. MAX_DEVLEN
 *      is defined as 16.
 *
 *  struct FSdef *filesys;
 *
 *      The file system used by this mount.
 *
 *  size_t blocksz;
 *
 *      Block size in use for this file system.
 *
 *  uint16_t flags;
 *
 *      Flags to control operation of the file system code:
 *
 *        FS_READONLY
 *
 *              The file system is mounted read-only and no changes will
 *              be allowed.
 *
 *        FS_DEBUG
 *
 *              Enables debug output. Such debug code may not be included
 *              in the default build.
 *
 *  FILE *container;
 *
 *      The open file handle for performing I/O on file system(s).
 *
 *  union {} FSdata;
 *
 *      Private region for use by the file system code.
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <ctype.h>
#include <string.h>

#include "fsio.h"

/*
 * By default, fsio will use the GNU readline library. If you do not have
 * it available or do not wish to use it, comment out the following line.
 * You may also need to comment out the LIBS= line in Makefile.
 */
#define USE_READLINE

#ifdef USE_READLINE
#include <readline/readline.h>
#include <readline/history.h>
#endif

int verbose = 0, quiet = 0;

#define MAX_CMDLEN      512

static void doMount(void);
static void doUmount(void);
static void doNewfs(void);
static void doSet(void);
static void doInfo(void);
static void doDir(void);
static void doDump(void);
static void doCopy(void);
static void doType(void);
static void doDelete(void);
static void doStatus(void);
static void doDo(void);
static void doHelp(void);
static void doExit(void);
static void doRewind(void);
static void doEOM(void);
static void doSkipForw(void);
static void doSkipRev(void);

typedef void (*cmd_t)(void);

struct command {
  char          *name;                  /* Command name string */
  char          *switches;              /* Command switches (getopt format) */
  int           minargs;                /* Required argument count */
  int           maxargs;                /* Maximum argument count */
  int           flags;                  /* Command flags */
  cmd_t         func;                   /* Command execution function */
} cmdTable[] = {
#ifdef DEBUG
  { "mount", OPTIONS("dfrt:x"), 3, 3, 0, doMount },
#else
  { "mount", OPTIONS("frt:x"), 3, 3, 0, doMount },
#endif
  { "umount", NULL, 1, 1, 0, doUmount },
  { "newfs", OPTIONS("e:t:"), 2, 2, 0, doNewfs },
  { "set", NULL, 2, MAX_CMDLEN, 0, doSet },
  { "info", NULL, 1, 1, 0, doInfo },
  { "dir", OPTIONS("fn"), 1, 1, 0, doDir },
  { "dump", OPTIONS("bcdnwx"), 1, 1, 0, doDump },
  { "copy", OPTIONS("ac:np"), 2, 2, 0, doCopy },
  { "type", OPTIONS("n"), 1, 1, 0, doType },
  { "delete", NULL, 1, 1, 0, doDelete },
  { "status", NULL, 0, 0, 0, doStatus },
  { "do", OPTIONS("q"), 1, 1, 0, doDo },
  { "help", NULL, 0, 0, 0, doHelp },
  { "exit", NULL, 0, 0, 0, doExit },
  { "quit", NULL, 0, 0, 0, doExit },
  { "rewind", NULL, 1, 1, 0, doRewind },
  { "eom", NULL, 1, 1, 0, doEOM },
  { "skipf", NULL, 2, 2, 0, doSkipForw },
  { "skipr", NULL, 2, 2, 0, doSkipRev },
  { NULL, NULL, 0, 0, 0, doExit }
};

/*
 * Switches and values
 */
uint32_t swPresent;
char *swValue[26];

/*
 * Command argument strings
 */
int args;
char **words, *wds[MAX_CMDLEN];

#ifdef DEBUG
FILE *DEBUGout = NULL;
#endif

struct mountedFS *mounts;

struct FSdef *fileSystems = NULL;

/*
 * Tables for use with bitmap allocators
 */
uint16_t bits[16] = {
  0000001, 0000002, 0000004, 0000010, 0000020, 0000040, 0000100, 0000200,
  0000400, 0001000, 0002000, 0004000, 0010000, 0020000, 0040000, 0100000
};

uint16_t lowbits[16] = {
  0000001, 0000003, 0000007, 0000017, 0000037, 0000077, 0000177, 0000377,
  0000777, 0001777, 0003777, 0007777, 0017777, 0037777, 0077777, 0177777
};

uint16_t highbits[16] = {
  0100000, 0140000, 0160000, 0170000, 0174000, 0176000, 0177000, 0177400,
  0177600, 0177700, 0177740, 0177760, 0177770, 0177774, 0177776, 0177777
};

/*
 * Table of # of zeroes in each byte value.
 */
uint8_t zeroes[256] = {
  8,  7,  7,  6,  7,  6,  6,  5,  7,  6,  6,  5,  6,  5,  5,  4,
  7,  6,  6,  5,  6,  5,  5,  4,  6,  5,  5,  4,  5,  4,  4,  3,
  7,  6,  6,  5,  6,  5,  5,  4,  6,  5,  5,  4,  5,  4,  4,  3,
  6,  5,  5,  4,  5,  4,  4,  3,  5,  4,  4,  3,  4,  3,  3,  2,
  7,  6,  6,  5,  6,  5,  5,  4,  6,  5,  5,  4,  5,  4,  4,  3,
  6,  5,  5,  4,  5,  4,  4,  3,  5,  4,  4,  3,  4,  3,  3,  2,
  6,  5,  5,  4,  5,  4,  4,  3,  5,  4,  4,  3,  4,  3,  3,  2,
  5,  4,  4,  3,  4,  3,  3,  2,  4,  3,  3,  2,  3,  2,  2,  1,
  7,  6,  6,  5,  6,  5,  5,  4,  6,  5,  5,  4,  5,  4,  4,  3,
  6,  5,  5,  4,  5,  4,  4,  3,  5,  4,  4,  3,  4,  3,  3,  2,
  6,  5,  5,  4,  5,  4,  4,  3,  5,  4,  4,  3,  4,  3,  3,  2,
  5,  4,  4,  3,  4,  3,  3,  2,  4,  3,  3,  2,  3,  2,  2,  1,
  6,  5,  5,  4,  5,  4,  4,  3,  5,  4,  4,  3,  4,  3,  3,  2,
  5,  4,  4,  3,  4,  3,  3,  2,  4,  3,  3,  2,  3,  2,  2,  1,
  5,  4,  4,  3,  4,  3,  3,  2,  4,  3,  3,  2,  3,  2,  2,  1,
  4,  3,  3,  2,  3,  2,  2,  1,  3,  2,  2,  1,  2,  1,  1,  0,
};

void FSioCommands(FILE *);
static void FSioExecute(char *);

extern struct mountedFS localMount;

extern struct FSdef localFS;
extern struct FSdef dos11FS;
extern struct FSdef rt11FS;
extern struct FSdef dosmtFS;
extern struct FSdef os8FS;

#define MAX(a, b)       (((a) > (b)) ? (a) : (b))
#define MIN(a, b)       (((a) < (b)) ? (a) : (b))

/*++
 *      F S i o I n i t
 *
 *  Perform once-only initialization.
 *
 * Inputs:
 *
 *      None
 *
 * Outputs:
 *
 *      None
 *
 * Returns:
 *
 *      None
 *
 --*/
static void FSioInit(void)
{
  mounts = NULL;

  /*
   * Initialize the the local access device
   */
  strcpy(localMount.name, "local");
  localMount.filesys = &localFS;
  localMount.blocksz = 0; /*** TODO ***/
  localMount.container = NULL;

  /*
   * Add the local file access device.
   */
  localMount.next = mounts;
  mounts = &localMount;

  /*
   * Add supported file systems
   */
  localFS.next = fileSystems;
  fileSystems = &localFS;

  dos11FS.next = fileSystems;
  fileSystems = &dos11FS;

  rt11FS.next = fileSystems;
  fileSystems = &rt11FS;

  dosmtFS.next = fileSystems;
  fileSystems = &dosmtFS;

  os8FS.next = fileSystems;
  fileSystems = &os8FS;
}

/*++
 *      U s a g e
 *
 *  Display a usage message and exit.
 *
 * Inputs:
 *
 *      None
 *
 * Outputs:
 *
 *      None
 *
 * Returns:
 *
 *      Never returns
 *
 --*/
void Usage(void)
{
  fprintf(stderr, "Usage: fsio [cmdFile]\n");
  exit(1);
}

/*++
 *      m a i n
 *
 *  Start routine for the fsio application.
 *
 * Inputs:
 *
 *      argc            - # of supplied arguments
 *      argv            - array of argument strings
 *
 * Outputs:
 *
 *      None
 *
 * Returns:
 *
 *      Exit status
 *
 --*/
int main(
  int argc,
  char **argv
)
{
  FILE *commands = stdin;
  int ch;

  FSioInit();

  /*
   * Process command line switches
   */
  while ((ch = getopt(argc, argv, "qv")) != -1) {
    switch (ch) {
      case 'q':
        quiet = 1;
        break;

      case 'v':
        verbose = 1;
        break;

      default:
        Usage();
    }
  }

  argc -= optind;
  argv += optind;

#if !defined(__linux__)
  optreset = 1;
#endif
  optind = 1;

  if (argc <= 1) {
    if (argc == 1) {
      commands = fopen(argv[0], "r");
      if (commands == NULL) {
        fprintf(stderr, "Failed to open \"%s\": %s\n",
                argv[0], strerror(errno));
        exit(2);
      }
    }
    FSioCommands(commands);
  } else Usage();

  return 0;
}

/*++
 *      c h e c k D e v
 *
 *  Check the validity of a device name. An error message will be generated
 *  if it is invalid.
 *
 * Inputs:
 *
 *      dev             - pointer to device name
 *
 * Outputs:
 *
 *      None
 *
 * Returns:
 *
 *      1 if device name valid, 0 otherwise
 *
 --*/
static int checkDev(
  char *dev
)
{
  unsigned int i;

  /*
   * Remove any trailing ':' characters
   */
  while ((strlen(dev) != 0) && (dev[strlen(dev) - 1] == ':'))
    dev[strlen(dev) - 1] = '\0';

  for (i = 0; i < strlen(dev); i++)
    if (!isalpha(dev[i])) {
      fprintf(stderr,
              "%s: Device name contains non-alpha character\n", wds[0]);
      return 0;
    }
  return 1;
}

/*++
 *      f i n d M o u n t
 *
 *  Given a full device + file specification, find the mount point from
 *  the device name, parse out the optional unit number and return the
 *  remaining file specification. If no device + file specification is
 *  present, assume the file specification is for the local file system.
 *
 * Inputs:
 *
 *      cmd             - current command name
 *      dev             - pointer to device name + file specification
 *      unit            - return optional unit number here
 *      present         - return unit present indicator here (0 or 1)
 *      fname           - return remaining file specification here
 *
 * Outputs:
 *
 *      None
 *
 * Returns:
 *
 *      Pointer to the mounted file system structure, NULL if not found, unit
 *      number invalid or other parse error
 *
 --*/
static struct mountedFS *findMount(
  char *cmd,
  char *dev,
  uint8_t *unit,
  uint8_t *present,
  char **fname
)
{
  struct mountedFS *fs = &localMount;
  unsigned long unitno = 0;
  uint8_t unitpresent = 0;
  unsigned int i;
  char *endptr, *ptr;
  char origdev[32];

  if ((ptr = strchr(dev, ':')) != NULL) {
    *ptr++ = '\0';

    strncpy(origdev, dev, sizeof(origdev));

    /*
     * Determine validity of the device name and whether a unit number is
     * present.
     */
    for (i = 0; i < strlen(dev); i++)
      if (!isalpha(dev[i])) {
        if (isdigit(dev[i])) {
          unitno = strtoul(&dev[i], &endptr, 8);
          if (*endptr != '\0') {
            fprintf(stderr,
                    "%s: Device \"%s\" unit number invalid\n", cmd, origdev);
            return NULL;
          }

          if (unitno > 0377) {
            fprintf(stderr,
                    "%s: Device \"%s\" unit number too large\n", cmd, origdev);
            return NULL;
          }
          dev[i] ='\0';
          unitpresent = 1;
          break;
        }
        fprintf(stderr,
                "%s: Device \"%s\" contains non-alpha character\n",
                cmd, origdev);
        return NULL;
      }

    /*
     * Search the mounted file system list.
     */
    for (fs = mounts; fs != NULL; fs = fs->next)
      if (strcmp(dev, fs->name) == 0) {
        if ((unitpresent == 0) ||
            ((fs->filesys->flags & FS_UNITVALID) != 0))
          break;
        fprintf(stderr,
                "%s: \"%s\" does not support unit numbers\n", cmd, fs->name);
        return NULL;
      }
  
    if (fs == NULL)
      fprintf(stderr, "%s: Device \"%s\" not mounted\n", cmd, origdev);
  } else {
    if (getenv("FSioForceLocal") != NULL) {
      fprintf(stderr, "Local file system access requires use of \"local:\"\n");
      return NULL;
    }
    ptr = dev;
  }

  /*
   * No device present - assume local file system usage
   */
  *unit = unitno & 0377;
  *present = unitpresent;
  *fname = ptr;
  return fs;
}

/*++
 *      l o o k u p D e v
 *
 *  Search the mounted device table for a specified name.
 *
 * Inputs:
 *
 *      dev             - pointer to the device name
 *
 * Outputs:
 *
 *      None
 *
 * Returns:
 *
 *      Pointer to the mounted file system structure, NULL if not found or
 *      unit number invalid
 *
 --*/
static struct mountedFS *lookupDev(
  char *dev
)
{
  struct mountedFS *fs;

  for (fs = mounts; fs != NULL; fs = fs->next)
    if (strcmp(dev, fs->name) == 0)
      return fs;

  return NULL;
}

/*++
 *      l o o k u p F S
 *
 *  Search the list of supported file systems for a specific type.
 *
 * Inputs:
 *
 *      fs              - pointer to the file system type
 *
 * Outputs:
 *
 *      None
 *
 * Returns:
 *
 *      Pointer to the file system definition, NULL if not found
 *
 --*/
static struct FSdef *lookupFS(
  char *fs
)
{
  struct FSdef *filesys;

  for (filesys = fileSystems; filesys != NULL; filesys = filesys->next)
    if (strcmp(fs, filesys->fstype) == 0)
      return filesys;

  return NULL;
}

/*++
 *      d o M o u n t
 *
 *  Mount command processing routine.
 *
 * Inputs:
 *
 *      None
 *
 * Outputs:
 *
 *      None
 *
 * Returns:
 *
 *      None
 *
 --*/
static void doMount(void)
{
  struct FSdef *filesys;
  FILE *container;
  int status;
  char *mode = SWISSET('r') ? "r" : "r+";

  if (checkDev(words[0]) != 0) { 
    if (lookupDev(words[0]) == NULL) {
      if ((filesys = lookupFS(words[2])) != NULL) {
        if (filesys->mount == NULL) {
          fprintf(stderr,
                  "mount: \"%s\" is not a mountable filesystem\n", words[2]);
          return;
        }

        if ((container = fopen(words[1], mode)) != NULL) {
          struct mountedFS *mount;

          if ((mount = malloc(sizeof(struct mountedFS))) != NULL) {
            memset(mount, 0, sizeof(struct mountedFS));

            strcpy(mount->name, words[0]);
            mount->filesys = filesys;
            mount->blocksz = filesys->blocksz;
            mount->flags = SWISSET('r') ? FS_READONLY : 0;
#ifdef DEBUG
            if (SWISSET('d')) {
              mount->flags |= FS_DEBUG;

              if (DEBUGout == NULL) {
                char *dbg = getenv("FSioDebugLog");

                if (dbg != NULL)
                  DEBUGout = fopen(dbg, "a");
              }

              if (DEBUGout == NULL)
                DEBUGout = stdout;
            }
#endif
            mount->container = container;
            mount->skip = 0;

            /*
             * Verify that the container holds a valid file system
             */
            if ((status = (*filesys->mount)(mount)) > 0) {
              mount->next = mounts;
              mounts = mount;
              return;
            }
            free(mount);
            if (status == 0)
              fprintf(stderr,
                      "mount: \"%s\" does not contain a valid file system\n",
                      words[1]);
          } else fprintf(stderr, "mount: out of memory\n");
          fclose(container);
        } else fprintf(stderr, "mount: failed to open \"%s\"\n", words[1]);
      } else fprintf(stderr, "mount: \"%s\" is not a supported file system type\n", words[2]);
    } else fprintf(stderr, "mount: \"%s\" already mounted\n", words[0]);
  }
}

/*++
 *      d o U m o u n t
 *
 *  Umount command processing routine.
 *
 * Inputs:
 *
 *      None
 *
 * Outputs:
 *
 *      None
 *
 * Returns:
 *
 *      None
 *
 --*/
static void doUmount(void)
{
  struct mountedFS *mount, **ptr = &mounts;

  if (checkDev(words[0]) != 0) {
    if ((mount = lookupDev(words[0])) != NULL) {
      if (mount->filesys->umount == NULL) {
        fprintf(stderr, "umount: \"%s\" may not be unmounted\n", words[0]);
        return;
      }

      for (ptr = &mounts; *ptr != NULL; ptr = &((*ptr)->next))
        if (*ptr == mount) {
          *ptr = mount->next;
          (*mount->filesys->umount)(mount);
          fclose(mount->container);
          free(mount);
          return;
        }
      fprintf(stderr, "Mounted file system list corrupted\n");
      exit(10);
    } else fprintf(stderr, "umount: \"%s\" is not mounted\n", words[0]);
  }
}

/*++
 *      d o N e w f s
 *
 *  Create a new container file and build an empty file system on it.
 *
 * Inputs:
 *
 *      None
 *
 * Outputs:
 *
 *      None
 *
 * Returns:
 *
 *      None
 *
 --*/
static void doNewfs(void)
{
  struct FSdef *filesys;
  struct mountedFS mount;
  char ch = 0;
  int status;
  size_t size = 0;

  if ((filesys = lookupFS(words[1])) != NULL) {
    if ((filesys->newfs == NULL) && ((filesys->flags & FS_EMPTYFILE) == 0)) {
      fprintf(stderr,
              "newfs: \"%s\" does not support the newfs command\n", words[1]);
      return;
    }

    if (access(words[0], F_OK) == 0) {
      fprintf(stderr, "newfs: \"%s\" already exists\n", words[0]);
      return;
    }

    memset(&mount, 0, sizeof(mount));

    /*
     * Get the required container file size.
     */
    if (filesys->size != NULL)
      size = (*filesys->size)();

    if ((mount.container = fopen(words[0], "w+")) != NULL) {
      off_t offset = size - 1;

      mount.skip = 0;

      /*
       * If an empty file is valid, we are all done
       */
      if ((filesys->flags & FS_EMPTYFILE) != 0)
        return;

      if ((fseeko(mount.container, offset + mount.skip, SEEK_SET) == 0) &&
          (fwrite(&ch, 1, 1, mount.container) == 1)) {
        mount.filesys = filesys;
        mount.blocksz = filesys->blocksz;

        status = (*filesys->newfs)(&mount, size);

        fclose(mount.container);
        if (status == 0)
          unlink(words[0]);
      } else {
        fprintf(stderr, "newfs: failed to extend \"%s\"\n", words[0]);
        fclose(mount.container);
        unlink(words[0]);
      }
    } else fprintf(stderr, "newfs: failed to create \"%s\"\n", words[0]);
  } else fprintf(stderr, "newfs: \"%s\" is not a supported file system type\n", words[1]);
}

/*++
 *      d o S e t
 *
 *  Set command processing routine.
 *
 * Inputs:
 *
 *      None
 *
 * Outputs:
 *
 *      None
 *
 * Returns:
 *
 *      None
 *
 --*/
static void doSet(void)
{
  struct mountedFS *mount;
  char *fname;
  uint8_t unit, present;

  if ((mount = findMount("set", words[0], &unit, &present, &fname)) != NULL) {
    if (*fname == '\0') {
      if (mount->filesys->set != NULL)
        (*mount->filesys->set)(mount, unit, present);
      else fprintf(stderr, "set: \"%s\" does not support \"set\" command\n",
                   mount->filesys->fstype);
    } else fprintf(stderr, "set: Does not expect a file name\n");
  }
}

/*++
 *      d o I n f o
 *
 *  Info command processing routine.
 *
 * Inputs:
 *
 *      None
 *
 * Outputs:
 *
 *      None
 *
 * Returns:
 *
 *      None
 *
 --*/
static void doInfo(void)
{
  struct mountedFS *mount;
  char *fname;
  uint8_t unit, present;

  if ((mount = findMount("info", words[0], &unit, &present, &fname)) != NULL) {
    if (*fname == '\0')
      (*mount->filesys->info)(mount, unit, present);
    else fprintf(stderr, "info: Does not expect a file name\n");
  }
}

/*++
 *      d o D i r
 *
 *  Dir command processing routine.
 *
 * Inputs:
 *
 *      None
 *
 * Outputs:
 *
 *      None
 *
 * Returns:
 *
 *      None
 *
 --*/
static void doDir(void)
{
  struct mountedFS *mount;
  char *fname;
  uint8_t unit, present;

  if ((mount = findMount("dir", words[0], &unit, &present, &fname)) != NULL) {
    (*mount->filesys->dir)(mount, unit, fname);
    return;
  }
}

/*++
 *      d o D u m p
 *
 *  Dump command processing routine.
 *
 * Inputs:
 *
 *      None
 *
 * Outputs:
 *
 *      None
 *
 * Returns:
 *
 *      None
 *
 --*/
static void doDump(void)
{
  struct mountedFS *mount;
  char *fname;
  void *file;
  uint8_t unit, present;

  if ((mount = findMount("dump", words[0], &unit, &present, &fname)) != NULL) {
    if ((file = (*mount->filesys->openFileR)(mount, unit, fname)) != NULL) {
      unsigned int offset = 0, datasz = 2;
      uint8_t data1;
      uint16_t data2;
      uint32_t data4;
      char output[128];

      if (SWISSET('b') || SWISSET('c'))
        datasz = 1;
      else if (SWISSET('d'))
        datasz = 4;
      else if (SWISSET('w'))
        datasz = 2;

      output[0] = '\0';

      switch (datasz) {
        case 1:
          while ((*mount->filesys->readFile)(file, &data1, 1) != 0) {
            if (SWISSET('b'))
              sprintf(&output[strlen(output)],
                      SWISSET('x') ? " 0x%02X" : " 0%03o", data1);
            else sprintf(&output[strlen(output)], " %s ", Ascii[data1 & 0177]);

            if ((offset & 07) == 07) {
              if (mount->blocksz != 0)
                printf("%s%011o %s\n",
                       ((offset & ~07) % mount->blocksz) == 0 ? "\n" : "",
                       offset & ~07, output);
              else printf("%011o %s\n", offset & ~07, output);
              output[0] = '\0';
            }
            offset++;
          }

          if (output[0] != '\0')
            printf("%011o %s\n", offset & ~07, output);
          break;

        case 2:
          while ((*mount->filesys->readFile)(file, &data2, 2) != 0) {
            sprintf(&output[strlen(output)],
                    SWISSET('x') ? " 0x%04X" : " %07o", data2);

            if ((offset & 017) == 016) {
              if (mount->blocksz != 0)
                printf("%s%011o %s\n",
                       ((offset & ~017) % mount->blocksz) == 0 ? "\n" : "",
                       offset & ~017, output);
              else printf("%011o %s\n", offset & ~017, output);
              output[0] = '\0';
            }
            offset += 2;
            data2 = 0;
          }

          if (output[0] != '\0')
            printf("%011o %s\n", offset & ~07, output);
          break;

        case 4:
          while ((*mount->filesys->readFile)(file, &data4, 4) != 0) {
            sprintf(&output[strlen(output)],
                    SWISSET('x') ? " 0x%08X" : " %011o", data2);

            if ((offset & 017) == 014) {
              if (mount->blocksz != 0)
                printf("%s%011o %s\n",
                       ((offset & ~017) % mount->blocksz) == 0 ? "\n" : "",
                       offset & ~017, output);
              else printf("%011o %s\n", offset & ~017, output);
              output[0] = '\0';
            }
            offset += 4;
            data2 = 0;
          }

          if (output[0] != '\0')
            printf("%011o %s\n", offset & ~07, output);
          break;
      }
      /*
       * Close the file
       */
      (*mount->filesys->closeFile)(file);
    } else fprintf(stderr, "dump: \"%s\" no such file\n", fname);
  }
}

/*++
 *      d o C o p y
 *
 *  Copy command processing routine.
 *
 * Inputs:
 *
 *      None
 *
 * Outputs:
 *
 *      None
 *
 * Returns:
 *
 *      None
 *
 --*/
#define BFRSIZ          512

static void doCopy(void)
{
  struct mountedFS *mountSrc, *mountDest;
  struct FSdef *fsSrc, *fsDest;
  uint8_t unitSrc, unitDest, presentSrc, presentDest;
  char *fnameSrc, *fnameDest;
  void *fileSrc, *fileDest;
  char *endptr;

  mountSrc = findMount("copy", words[0], &unitSrc, &presentSrc, &fnameSrc);
  mountDest = findMount("copy", words[1], &unitDest, &presentDest, &fnameDest);

  if ((mountSrc != NULL) && (mountDest != NULL)) {
    fsSrc = mountSrc->filesys;
    fsDest = mountDest->filesys;

    if (mountSrc == mountDest) {
      if ((fsDest->flags & FS_1OPENFILE) != 0) {
        fprintf(stderr,
                "copy: \"%s\" does not allow simultaneous read/write access\n",
                mountDest->name);
        return;
      }
    }

    if ((mountDest->flags & FS_READONLY) != 0) {
      fprintf(stderr, "copy: \"%s\" is mounted read-only\n", mountDest->name);
      return;
    }
    fsSrc = mountSrc->filesys;
    fsDest = mountDest->filesys;

    if ((fileSrc = (*fsSrc->openFileR)(mountSrc, unitSrc, fnameSrc)) != NULL) {
      off_t size = (*fsSrc->fileSize)(fileSrc);
      unsigned long contig;

      if (SWISSET('c')) {
        contig = strtoul(SWGETVAL('c'), &endptr, 10);
        if (*endptr != '\0') {
          fprintf(stderr, "copy: Invalid character in '-c' argument\n");
          (*fsSrc->closeFile)(fileSrc);
          return;
        }
        if (contig != 0)
          size = MAX(size, (long)(contig * mountDest->blocksz));
      }

      if ((fileDest = (*fsDest->openFileW)(mountDest, unitDest, fnameDest, size)) != NULL) {
        char buf[BFRSIZ];
        size_t len;

        while ((len = (*fsSrc->readFile)(fileSrc, buf, BFRSIZ)) != 0) {
          if ((*fsDest->writeFile)(fileDest, buf, len) == 0) {
            fprintf(stderr, "copy: Error writing \"%s\"\n", fnameDest);
            break;
          }
        }
        (*fsDest->closeFile)(fileDest);
        (*fsSrc->closeFile)(fileSrc);
      } else {
        fprintf(stderr,
                "copy: failed to open \"%s\" for write\n", fnameDest);
        (*fsSrc->closeFile)(fileSrc);
      }
    } else fprintf(stderr,
                   "copy: failed to open \"%s\" for read\n", fnameSrc);
  }
}

/*++
 *      d o T y p e
 *
 *  Type command processing routine.
 *
 * Inputs:
 *
 *      None
 *
 * Outputs:
 *
 *      None
 *
 * Returns:
 *
 *      None
 *
 --*/
static void doType(void)
{
  struct mountedFS *mount;
  char *fname;
  void *file;
  uint8_t unit, present;

  /*
   * Force ASCII mode
   */
  SWSET('a');

  if ((mount = findMount("type", words[0], &unit, &present, &fname)) != NULL) {
    if ((file = (*mount->filesys->openFileR)(mount, unit, fname)) != NULL) {
      char buf[BFRSIZ];
      size_t len, i;

      while ((len = (*mount->filesys->readFile)(file, buf, BFRSIZ)) != 0) {
        for (i = 0; i < len; i++)
          if (buf[i] != '\0')
            putchar(buf[i]);
      }

      /*
       * Close the file
       */
      (*mount->filesys->closeFile)(file);
    } else fprintf(stderr, "type: \"%s\" no such file\n", fname);
  }
}

/*++
 *      d o D e l e t e
 *
 *  Delete command processing routine.
 *
 * Inputs:
 *
 *      None
 *
 * Outputs:
 *
 *      None
 *
 * Returns:
 *
 *      None
 *
 --*/
static void doDelete(void)
{
  struct mountedFS *mount;
  char *fname;
  void *file;
  uint8_t unit, present;

  if ((mount = findMount("delete", words[0], &unit, &present, &fname)) != NULL) {
    if (mount->filesys->deleteFile != NULL) {
      if ((mount->flags & FS_READONLY) == 0) {
        if ((file = (*mount->filesys->openFileR)(mount, unit, fname)) != NULL)
          (*mount->filesys->deleteFile)(file, fname);
        else fprintf(stderr, "delete: \"%s\" no such file\n", fname);
      } else fprintf(stderr, "delete: \"%s\" is mounted read-only\n", mount->name);
    } else fprintf(stderr, "delete: Function not supported\n");
  }
}

/*++
 *      d o S t a t u s
 *
 *  Display the current status of all mounted file systems.
 *
 * Inputs:
 *
 *      None
 *
 * Outputs:
 *
 *      None
 *
 * Returns:
 *
 *      None
 *
 --*/
static void doStatus(void)
{
  if (mounts != NULL) {
    struct mountedFS *mount;

    for (mount = mounts; mount != NULL; mount = mount->next)
      printf("%-20s%s\n", mount->name, mount->filesys->fstype);
  }
}

/*++
 *      d o D o
 *
 *  Execute commands from a file.
 *
 * Inputs:
 *
 *      None
 *
 * Outputs:
 *
 *      None
 *
 * Returns:
 *
 *      None
 *
 --*/
static void doDo(void)
{
  uint8_t noecho = SWISSET('q');
  FILE *cmdFile;
  char buf[MAX_CMDLEN];

  if ((cmdFile = fopen(words[0], "r")) != NULL) {
    for (;;) {
      int len;

      if (fgets(buf, sizeof(buf), cmdFile) == NULL) {
        fclose(cmdFile);
        return;
      }

      if (!noecho)
        printf("fsio> %s", buf);

      /*
       * Remove any trailing newline
       */
      len = strlen(buf);
      if (buf[len - 1] == '\n')
        buf[len - 1] = '\0';

      /*
       * Parse the command line and execute any command found
       */
      FSioExecute(buf);
    }
  } else fprintf(stderr, "do: failed to open \"%s\"\n", words[0]);
}

/*++
 *      d o H e l p
 *
 *  Help command processing routine.
 *
 * Inputs:
 *
 *      None
 *
 * Outputs:
 *
 *      None
 *
 * Returns:
 *
 *      None
 *
 --*/
static void doHelp(void)
{
  struct FSdef *filesys;

  printf(
    "fsio allows manipulating files within file system container files\n"
    "supported by various emulators such as SIMH.\n\n"
    "fsio is executed by the command:\n\n"
    "  fsio [cmdfile]\n\n"
    "If cmdfile is present, fsio will read commands from the command file\n"
    "otherwise it will prompt for input:\n\n"
    "  fsio>\n\n"
    "A common command format is used:\n\n"
    "  verb [switches] arg1 arg2 ...\n\n"
    "The following commands are supported:\n\n"
    "  mount [-r] [-t type] dev[:] container fstype\n\n"
    "The file system container file is made available to fsio (via the dev\n"
    "specifier). fstype specifies the type of the container file system.\n"
    "If -r is specified, the file system will be read-only.\n"
    "In some cases (e.g. OS/8) fsio is unable to determine the type of the\n"
    "underlying disk so it must be specified using \"-t type\"\n\n"
    "  umount dev[:]\n\n"
    "Remove all knowledge of the container file from fsio.\n\n"
    "  newfs [-t type] container fstype\n\n"
    "Create a new container file with an empty file system. The \"-t type\"\n"
    "switch may be used to control the size of the container file, this\n"
    "switch is file-system dependent.\n\n"
    "  set dev: args ...\n\n"
    "Set parameter(s) on a mounted file system.\n\n"
    "  info dev:\n\n"
    "Display some internal information about a mounted file system.\n\n"
    "  dir [-fn] dev:filespec\n\n"
    "List the contents of the specified directory/file(s). Wildcards\n"
    "may be specified in the format expected by the specified file system.\n"
    "By default a \"brief\" format will be displayed, if -f is specified,\n"
    "a \"full\" format directory will be displayed. If -n is specified on\n"
    "a magtape-based filesystem, the tape will not be rewound before\n"
    "starting the listing.\n\n"
    "  dump [-bcdnwx] dev:filespec\n\n"
    "Dump the contents of the specified file (no wildcards allowed) in some\n"
    "human-readable format. The switches control the format; -b bytes, -c\n"
    "characters (ASCII), -w 16-bit words and -d 32-bit double words.\n"
    "By default, bytes, words and double words are dumped in octal, use -x\n"
    "for hex format. If -n is specified on a magtape-based filesystem\n"
    "the tape will not be rewound before looking for the specified file.\n\n"
    "  copy [-anc blocks] dev1:src dev2:dest\n\n"
    "Copy a file between file systems. If -a is specified the copy will be\n"
    "performed in ASCII mode which will translate end-of-line characters.\n"
    "If \"-c blocks\" is specified and the destination file system supports\n"
    "contiguous files, the specified # of contiguous file system blocks will\n"
    "be allocated before starting the transfer. If -n is specified on a\n"
    "magtape-based filesystem the tape will not be rewound before looking\n"
    "for the source file.\n\n"
    "  type [-n] dev:src\n\n"
    "Type the contents of the file on the terminal. This is equivalent to\n"
    "the command:\n\n"
    "\tcopy -a dev:src /dev/tty\n\n"
    "If -n is specified on a magtape-based filesystem the tape will not be\n"
    "rewound before looking for the file\n\n"
    "  delete dev:file\n\n"
    "Delete the specified file from the container file system.\n\n"
    "  status\n\n"
    "Display a list of the currently mounted file systems.\n\n"
    "  do [-q] cmdFile\n\n"
    "Echo and execute commands from a file. If -q is present suppress the echo.\n\n"
    "  help\n\n"
    "Display this help text.\n\n"
    "  exit\n"
    "  quit\n\n"
    "Terminate execution.\n\n"
    "The following commands are only accepted by magtape-based filesystems:\n\n"
    "  rewind dev:\n\n"
    "Position the magtape device to the start of the tape.\n\n"
    "  eom dev:\n\n"
    "Position the magtape past the end of the last file already on the tape.\n\n"
    "  skipf dev: n\n\n"
    "Skip forward over n files (n > 0).\n\n"
    "  skipr dev: n\n\n"
    "Skip backwards over n files (n > 0).\n\n"
    "The special device name \"local:\" may be used to specify files in the\n"
    "local host file system.\n\n"
    "The following container file systems are supported:\n\n");

  for (filesys = fileSystems; filesys != NULL; filesys = filesys->next)
    printf("%s", filesys->descr);
}

/*++
 *      d o E x i t
 *
 *  Exit (and Quit) command processing routine.
 *
 * Inputs:
 *
 *      None
 *
 * Outputs:
 *
 *      None
 *
 * Returns:
 *
 *      None
 *
 --*/
static void doExit(void)
{
  struct mountedFS *mount;

  for (mount = mounts; mount != NULL; mount = mount->next)
    if (mount != &localMount)
      fclose(mount->container);

#ifdef DEBUG
  if ((DEBUGout != NULL) && (DEBUGout != stdout))
    fclose(DEBUGout);
#endif

  exit(0);
}

/*++
 *      d o R e w i n d 
 *
 *  Rewind a tape container file, positioning it a=t beginning-of-tape.
 *
 * Inputs:
 *
 *      None
 *
 * Outputs:
 *
 *      None
 *
 * Returns:
 *
 *      None
 *
 --*/
static void doRewind(void)
{
  struct mountedFS *mount;
  char *fname;
  uint8_t unit, present;

  if ((mount = findMount("rewind", words[0], &unit, &present, &fname)) != NULL) {
    if ((mount->filesys->flags & FS_TAPE) == 0) {
      fprintf(stderr, "rewind: Command only valid on magtapes\n");
      return;
    }

    if (*fname == '\0')
      (*mount->filesys->rewind)(mount);
    else fprintf(stderr, "rewind: Does not expect a file name\n");
  }
}

/*++
 *      d o E O M
 *
 *  Position the tape at the end-of-media.
 *
 * Inputs:
 *
 *      None
 *
 * Outputs:
 *
 *      None
 *
 * Returns:
 *
 *      None
 *
 --*/
static void doEOM(void)
{
  struct mountedFS *mount;
  char *fname;
  uint8_t unit, present;

  if ((mount = findMount("eom", words[0], &unit, &present, &fname)) != NULL) {
    if ((mount->filesys->flags & FS_TAPE) == 0) {
      fprintf(stderr, "eom: Command only valid on magtapes\n");
      return;
    }

    if (*fname == '\0')
      (*mount->filesys->eom)(mount);
    else fprintf(stderr, "eom: Does not expect a file name\n");
  }
}

/*++
 *      d o S k i p F o r w
 *
 *  Skip forward over the specified number of files or until end-of-media is
 *  reached.
 *
 * Inputs:
 *
 *      None
 *
 * Outputs:
 *
 *      None
 *
 * Returns:
 *
 *      None
 *
 --*/
static void doSkipForw(void)
{
  struct mountedFS *mount;
  char *fname;
  uint8_t unit, present;

  if ((mount = findMount("skipf", words[0], &unit, &present, &fname)) != NULL) {
    if ((mount->filesys->flags & FS_TAPE) == 0) {
      fprintf(stderr, "skipf: Command only valid on magtapes\n");
      return;
    }

    if (*fname == '\0') {
      char *endptr;
      unsigned long count = strtoul(words[1], &endptr, 10);

      if (*endptr != '\0') {
        fprintf(stderr, "skipf: Invalid character in count\n");
        return;
      }

      if (count != 0)
        (*mount->filesys->skipforw)(mount, count);
    } else fprintf(stderr, "skipf: Does not expect a file name\n");
  }
}

/*++
 *      d o S k i p R e v
 *
 *  Skip backwards over the specified number of files or beginning-of-tape is
 *  reached.
 *
 * Inputs:
 *
 *      None
 *
 * Outputs:
 *
 *      None
 *
 * Returns:
 *
 *      None
 *
 --*/
static void doSkipRev(void)
{
  struct mountedFS *mount;
  char *fname;
  uint8_t unit, present;

  if ((mount = findMount("skipf", words[0], &unit, &present, &fname)) != NULL) {
    if ((mount->filesys->flags & FS_TAPE) == 0) {
      fprintf(stderr, "skipr: Command only valid on magtapes\n");
      return;
    }

    if (*fname == '\0') {
      char *endptr;
      unsigned long count = strtoul(words[1], &endptr, 10);

      if (*endptr != '\0') {
        fprintf(stderr, "skipr: Invalid character in count\n");
        return;
      }

      if (count != 0)
        (*mount->filesys->skiprev)(mount, count);
    } else fprintf(stderr, "skipr: Does not expect a file name\n");
  }
}

/*++
 *      F S i o E x e c u t e
 *
 *  Parse a command line and execute any command found
 *
 * Inputs:
 *
 *      in              - pointer to the command line (zero terminated)
 *
 * Outputs:
 *
 *      None
 *
 * Returns:
 *
 *      None
 *
 --*/
static void FSioExecute(
  char *in
)
{
  char quote;

  words = wds;
  args = 0;

  /*
   * Split the command line into individual words
   */
  while (*in != '\0') {
    switch (*in) {
      case ' ':
      case '\t':
        in++;
        continue;

      case '\'':
      case '\"':
        quote = *in++;
        words[args++] = in;
        while (*in != quote) {
          if (*in == '\0') {
            fprintf(stderr, "Missing terminating quote - %c%s\n",
                    quote, words[args - 1]);
            return;
          }
          in++;
        }
        *in++ = '\0';
        break;
        
      case '#':
        if (args == 0)
          return;
        /* FALLTHROUGH */

      default:
        words[args++] = in++;
        while ((*in != ' ') && (*in != '\t') && (*in != '\0'))
          in++;

        if (*in != '\0')
          *in++ = '\0';
        break;
    }
  }

  if (args != 0) {
    struct command *cmds = cmdTable;
    int i, len, idx = -1;

    len = strlen(words[0]);

    for (cmds = cmdTable, i = 0; cmds->name != NULL; cmds++, i++) {
      if (strncmp(cmds->name, words[0], len) == 0) {
        if (idx != -1) {
          idx = -1;
          break;
        }
        idx = i;
      }
    }

    if (idx != -1) {
      char *switches = cmdTable[idx].switches;

      swPresent = 0;
      for (i = 0; i < 26; i++)
        swValue[i] = NULL;

      /*
       * Parse any switches associated with the command
       */
      if (switches != NULL) {
        int ch;
        char *ptr;

        while ((ch = getopt(args, words, switches)) != -1) {
          if ((ch == '?') || ((ptr = strchr(switches, ch)) == NULL)) {
#if !defined(__linux__)
            optreset = 1;
#endif
            optind = 1;
            return;
          }
          SWSET(ch);

          if (ptr[1] == ':')
            SWSETVAL(ch, optarg);
        }
        args -= optind;
        words += optind;

        /*
         * Reset getopt() for subsequent uses.
         */
#if !defined(__linux__)
        optreset = 1;
#endif
        optind = 1;
      } else args--, words++;

      if ((args < cmdTable[idx].minargs) || (args > cmdTable[idx].maxargs)) {
        fprintf(stderr, "%s: Too %s arguments\n",
                wds[0], args < cmdTable[idx].minargs ? "few" : "many");
        return;
      }

      /*
       * Execute the command.
       */
      (*cmdTable[idx].func)();
    } else fprintf(stderr, "Unknown command \"%s\"\n", words[0]);
  }
}

/*++
 *      F S i o C o m m a n d s
 *
 *  Read and process commands from the specified input stream.
 *
 * Inputs:
 *
 *      commands        - commands are read from this stream
 *
 * Outputs:
 *
 *      None
 *
 * Returns:
 *
 *      None
 *
 --*/
void FSioCommands(
  FILE *commands
)
{
#ifdef USE_READLINE
  char *buf, bufr[MAX_CMDLEN];

  for (;;) {
    if (commands == stdin) {
      if ((buf = readline("fsio> ")) == NULL)
        return;

      /*
       * Don't add empty lines to history.
       */
      if (*buf)
        add_history(buf);
    } else {
      int len;

      if (fgets(bufr, sizeof(bufr), commands) == NULL)
        return;

      buf = bufr;

      if (verbose)
        printf("fsio> %s", bufr);

      /*
       * Remove any trailing newline
       */
      len = strlen(buf);
      if (buf[len - 1] == '\n')
        buf[len - 1] = '\0';
    }

    /*
     * Parse the command line and execute any command found
     */
    FSioExecute(buf);

    if (commands == stdin)
      free(buf);
  }
#else
  char buf[MAX_CMDLEN];

  for (;;) {
    int len;

    if (isatty(fileno(commands)))
      fputs("fsio> ", stdout);

    if (fgets(buf, sizeof(buf), commands) == NULL)
      return;

    if (verbose && !isatty(fileno(commands)))
      printf("fsio> %s", buf);

    /*
     * Remove any trailing newline
     */
    len = strlen(buf);
    if (buf[len - 1] == '\n')
      buf[len - 1] = '\0';

    /*
     * Parse the command line and execute any command found
     */
    FSioExecute(buf);
  }
#endif
}

/*++
 *      F S i o R e a d B l o b
 *
 *  Read an arbitrary sized blob of binary data from the container file. The
 *  caller is responsible for making sure that the buffer has sufficient
 *  space for the blob.
 *
 * Inputs:
 *
 *      mount           - pointer to a mounted file system descriptor
 *      offset          - offset in the container file to start the read
 *      size            - size of the data to read (in bytes)
 *      buf             - pointer to the buffer to receive the data
 *
 * Outputs:
 *
 *      The buffer will be overwrittn by the contents of the blob from the
 *      container file system
 *
 * Returns:
 *
 *      1 if read was successful, 0 otherwise
 *
 --*/
int FSioReadBlob(
  struct mountedFS *mount,
  off_t offset,
  unsigned int size,
  void *buf
)
{
  if (fseeko(mount->container, offset, SEEK_SET) == 0)
    return fread(buf, size, 1, mount->container);

  return 0;
}

/*++
 *      F S i o W r i t e B l o b
 *
 *  Write an arbitrary sized blob of binary to from the container file.
 *
 * Inputs:
 *
 *      mount           - pointer to a mounted file system descriptor
 *      offset          - offset in the container file to start the write
 *      size            - size of the data to write (in bytes)
 *      buf             - pointer to the buffer with the data
 *
 * Outputs:
 *
 *      None
 *
 * Returns:
 *
 *      1 if write was successful, 0 otherwise
 *
 --*/
int FSioWriteBlob(
  struct mountedFS *mount,
  off_t offset,
  unsigned int size,
  void *buf
)
{
  if (fseeko(mount->container, offset, SEEK_SET) == 0)
    return fwrite(buf, size, 1, mount->container);

  return 0;
}

/*++
 *      F S i o R e a d B l o c k
 *
 *  Read a specified block from the container file. The caller is responsible
 *  for making sure that the buffer has sufficient space for the block.
 *
 * Inputs:
 *
 *      mount           - pointer to a mounted file system descriptor
 *      block           - logical block # in the range 0 - N
 *      buf             - pointer to the buffer to receive the data
 *
 * Outputs:
 *
 *      The buffer will be overwritten by the contents of the block from
 *      the container file system
 *
 * Returns:
 *
 *      1 if read was successful, 0 otherwise
 *
 --*/
int FSioReadBlock(
  struct mountedFS *mount,
  unsigned int block,
  void *buf
)
{
  off_t offset = block * mount->blocksz;

  if (fseeko(mount->container, offset + mount->skip, SEEK_SET) == 0)
    return fread(buf, mount->blocksz, 1, mount->container);

  return 0;
}

/*++
 *      F S i o W r i t e B l o c k
 *
 *  Write a specified block to the container file.
 *
 * Inputs:
 *
 *      mount           - pointer to a mounted file system descriptor
 *      block           - logical block # in the range 0 - N
 *      buf             - pointer to the buffer containing the data
 *
 * Outputs:
 *
 *      None
 *
 * Returns:
 *
 *      1 if write was successful, 0 otherwise
 *
 --*/
int FSioWriteBlock(
  struct mountedFS *mount,
  unsigned int block,
  void *buf
)
{
  off_t offset = block * mount->blocksz;

  if (fseeko(mount->container, offset + mount->skip, SEEK_SET) == 0)
    return fwrite(buf, mount->blocksz, 1, mount->container);

  return 0;
}

/*++
 *      F S i o R e a d S e c t o r
 *
 *  Read a sector of a specified size from the container. The caller is
 *  responsible for making sure that the buffer has sufficient space for
 *  the sector. This routine is used when file system blocks are constructed
 *  from a number of sectors which are interleaved on the physical disk (e.g.
 *  RX02 floppies).
 *
 * Inputs:
 *
 *      mount           - pointer to a mounted file system descriptor
 *      sector          - logical sector # in the range 0 - N
 *      size            - size of each sector (in bytes)
 *      buf             - pointer to the buffer to receive the data
 *
 * Outputs:
 *
 *      The buffer will be overwritten by the contents of the sector from
 *      the container file system
 *
 * Returns:
 *
 *      1 if read was successful, 0 otherwise
 *
 --*/
int FSioReadSector(
  struct mountedFS *mount,
  unsigned int sector,
  unsigned int size,
  void *buf
)
{
  off_t offset = sector * size;

  if (fseeko(mount->container, offset + mount->skip, SEEK_SET) == 0)
    return fread(buf, size, 1, mount->container);

  return 0;
}

/*++
 *      F S i o W r i t e S e c t o r
 *
 *  Write a sector of a specified size to the container. This routine is used
 *  when file system blocks are constructed from a number of sectors which are
 *  interleaved on the physical disk (e.g. RX02 floppies).
 *
 * Inputs:
 *
 *      mount           - pointer to a mounted file system descriptor
 *      sector          - logical sector # in the range 0 - N
 *      size            - size of each sector (in bytes)
 *      buf             - pointer to the buffer with the data
 *
 * Outputs:
 *
 *      None
 *
 * Returns:
 *
 *      1 if write was successful, 0 otherwise
 *
 --*/
int FSioWriteSector(
  struct mountedFS *mount,
  unsigned int sector,
  unsigned int size,
  void *buf
)
{
  off_t offset = sector * size;

  if (fseeko(mount->container, offset + mount->skip, SEEK_SET) == 0)
    return fwrite(buf, size, 1, mount->container);

  return 0;
}
