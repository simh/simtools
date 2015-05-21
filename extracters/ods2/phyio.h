/* Phy.h  v1.2   Definition of Physical I/O routines */

/*
        This is part of ODS2 written by Paul Nankervis,
        email address:  Paulnank@au1.ibm.com

        ODS2 is distributed freely for all members of the
        VMS community to use. However all derived works
        must maintain comments in their source to acknowledge
        the contibution of the original author.
*/

/*      To set up physical I/O for a new system a group of phyio
        routines need to be set up. They are:-
             phyio_show() which doesn't need to do anything - but
                          it would generally print some statistics
                          about the other phyio calls.
             phyio_init() to prepare a device for use by future
                          read/write calls. The device name would usually
                          map to a local device - for example rra: to /dev/rra
                          on a Unix system. The call needs to return a handle
                          (channel, file handle, reference number...) for
                          future reference, and optionally some device
                          information.
            phyio_read()  will return a specified number of bytes into a
                          buffer from the start of a 512 byte block on the
                          device referred to by the handle.
            phyio_write() will write a number of bytes out to a 512 byte block
                          address on a device.

*/

#define PHYIO_READONLY 1

struct phyio_info {
    unsigned status;
    unsigned sectors;
    unsigned sectorsize;
};

void phyio_show(void);
unsigned phyio_init(int devlen,char *devnam,unsigned *handle,struct phyio_info *info);
unsigned phyio_read(unsigned handle,unsigned block,unsigned length,char *buffer);
unsigned phyio_write(unsigned handle,unsigned block,unsigned length,char *buffer);
