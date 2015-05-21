/* PHYNT.C  v1.2    Physical I/O module for Windows NT */

/* W95 code now included with code to automatically determine
   if we are running under NT...

   W98 seems to have a performance problem where the
   INT25 call are VERY slow!!! */



/* This version built and tested under Visual C++. To support
   CD drives this version requires ASPI run-time support. For
   SCSI drives probably any old ASPI library will do. But for
   ATAPI drives on NT there are 'ASPI libraries and there are
   ASPI libraries'. I have had success with an older version of
   Adaptec's APSI (version 2,4,0,0) and with Symbios Logic
   libraries. But I have NOT been able to make the latest
   Adaptec routines work on NT!
   Note: Windows 95 comes with APSI by default!  */

#include <stdio.h>
#include <stdlib.h>
#include <windows.h>
#include <winioctl.h>
#include <string.h>
#include <ctype.h>
#include <memory.h>

#include "phyio.h"
#include "ssdef.h"


unsigned init_count = 0;        /* Some counters so we can report */
unsigned read_count = 0;        /* How often we get called */
unsigned write_count = 0;

void phyio_show(void)
{
    printf("PHYIO_SHOW Initializations: %d Reads: %d Writes: %d\n",
           init_count,read_count,write_count);
}


unsigned phyio_write(unsigned handle,unsigned block,unsigned length,char *buffer)
{
    write_count++;
    return SS$_WRITLCK;         /* Not implemented yet!! */
}


/* This routine figures out whether this is an NT system or not... */

unsigned is_NT = 2;
OSVERSIONINFO sysver;

void getsysversion()
{
    memset(&sysver,0,sizeof(OSVERSIONINFO));
    sysver.dwOSVersionInfoSize = sizeof(OSVERSIONINFO);
    GetVersionEx(&sysver);
    if (sysver.dwPlatformId == VER_PLATFORM_WIN32_NT) {
        is_NT = 1;
    } else {
        is_NT = 0;
    }
}

/*
        To read from CD APSI run-time support is required (wnaspi32.dll).
        NT does not come with APSI support so you need to install it first.
        (ASPI is also required for things like Digital Audio Extraction
        and was origionally loaded on my machine to make MP3 audio files.)
        One downside is that ASPI on NT does not have a way to match SCSI
        adapters and units to a system device. So this program works by
        finding the first CD like unit on the system and using it - that
        may NOT be what you want if you have several CD drives! And finally
        there are CD drives and there are CD drives. From what I can tell
        many simply won't work with this (or other!) code. Naturally if
        you find a better way please let me know... Paulnank@au1.ibm.com
*/

#if defined (USE_WNASPI)

#include "wnaspi32.h"

unsigned cd_initialized = 0;    /* Flag for CD read to go... */
unsigned cd_adaptor,cd_target;  /* Adaptor & SCSI #'s */
HANDLE ASPICompletion;          /* Windows event for ASPI wait */


/* Get ASPI ready and locate the first CD drive... */

unsigned cd_initialize()
{
    if (cd_initialized) {
        printf("Can only support one (first) ASPI device\n");
        return 8;
    } else {
        DWORD ASPIStatus;
        ASPIStatus = GetASPI32SupportInfo();
        if (HIBYTE(LOWORD(ASPIStatus)) == SS_COMP) {
            BYTE NumAdapters;
            SRB_GDEVBlock Ssrb;
            Ssrb.SRB_Cmd = SC_GET_DEV_TYPE;
            Ssrb.SRB_HaId = 0;
            Ssrb.SRB_Flags = SRB_EVENT_NOTIFY;
            Ssrb.SRB_Hdr_Rsvd = 0;
            Ssrb.SRB_Target = 0;
            Ssrb.SRB_Lun = 0;
            NumAdapters = LOWORD(LOBYTE(ASPIStatus));
            for (cd_adaptor = 0; cd_adaptor < NumAdapters; cd_adaptor++) {
                for (cd_target = 0; cd_target <= 7; cd_target++) {
                    Ssrb.SRB_HaId = cd_adaptor;
                    Ssrb.SRB_Target = cd_target;
                    ASPIStatus = SendASPI32Command(&Ssrb);
                    if (ASPIStatus == SS_COMP && Ssrb.SRB_DeviceType == 5) break;
                }
                if (cd_target <= 7) break;
            }
            if (cd_adaptor < NumAdapters) {
                if ((ASPICompletion = CreateEvent(NULL,FALSE,FALSE,NULL)) == NULL) return 8;
                cd_initialized = 1;
                return 1;
            } else {
                printf("Could not find ASPI CD device\n");
                return 8;
            }
        } else {
            printf("Could not initialize ASPI (%x)\n",ASPIStatus);
            return 8;
        }
    }
}


/* Read a sector from CD using ASPI... */

unsigned cd_read(unsigned sector,char *buffer)
{
    DWORD ASPIEventStatus;
    SRB_ExecSCSICmd Esrb;
    Esrb.SRB_Cmd = SC_EXEC_SCSI_CMD;
    Esrb.SRB_HaId = cd_adaptor;
    Esrb.SRB_Flags = SRB_DIR_IN | SRB_EVENT_NOTIFY;
    Esrb.SRB_Hdr_Rsvd = 0;
    Esrb.SRB_Target = cd_target;
    Esrb.SRB_Lun = 0;
    Esrb.SRB_BufLen = 2352;
    Esrb.SRB_BufPointer = buffer;
    Esrb.SRB_SenseLen = 0;
    Esrb.SRB_CDBLen = 10;
    Esrb.SRB_PostProc = ASPICompletion;
    Esrb.CDBByte[0] = 0x28;     /* Build SCSI read command packet... */
    Esrb.CDBByte[1] = 0;
    Esrb.CDBByte[2] = (sector >> 24);
    Esrb.CDBByte[3] = (sector >> 16) & 0xff;
    Esrb.CDBByte[4] = (sector >> 8) & 0xff;
    Esrb.CDBByte[5] = sector & 0xff;
    Esrb.CDBByte[6] = 0;
    Esrb.CDBByte[7] = 0;
    Esrb.CDBByte[8] = 1;
    Esrb.CDBByte[9] = 0;
    Esrb.CDBByte[10] = 0;
    SendASPI32Command(&Esrb);   /* Perform the read... */
    while (Esrb.SRB_Status == SS_PENDING)
        ASPIEventStatus = WaitForSingleObject(ASPICompletion,INFINITE);
    /* if (ASPIEventStatus == WAIT_OBJECT_0) */ResetEvent(ASPICompletion);
    if (Esrb.SRB_Status != SS_COMP) return SS$_PARITY;
    return 1;
}
#else /* !defined(USE_WNASPI) */

unsigned cd_initialize()
{
    return 1;
}

unsigned cd_read(unsigned sector,char *buffer)
{
    return 0;
}

#endif /* defined(USE_WNASPI) */


/* Some NT definitions... */

/* #include "Ntddcdrm.h" */
#define IOCTL_CDROM_BASE         FILE_DEVICE_CD_ROM
#define IOCTL_CDROM_RAW_READ     CTL_CODE(IOCTL_CDROM_BASE, 0x000F, METHOD_OUT_DIRECT,  FILE_READ_ACCESS)
#define IOCTL_CDROM_GET_DRIVE_GEOMETRY CTL_CODE(IOCTL_CDROM_BASE, 0x0013, METHOD_BUFFERED, FILE_READ_ACCESS)

/* NT Get disk or CD geometry... */

BOOL
    GetDiskGeometry(
                    HANDLE hDisk,
                    DISK_GEOMETRY *dGeometry
    )
{
    DWORD ReturnedByteCount;
    BOOL results = DeviceIoControl(
                                   hDisk,
                                   IOCTL_DISK_GET_DRIVE_GEOMETRY,
                                   NULL,
                                   0,
                                   dGeometry,
                                   sizeof(*dGeometry),
                                   &ReturnedByteCount,
                                   NULL
                );
    if (!results) results = DeviceIoControl(
                                            hDisk,
                                            IOCTL_CDROM_GET_DRIVE_GEOMETRY,
                                            NULL,
                                            0,
                                            dGeometry,
                                            sizeof(*dGeometry),
                                            &ReturnedByteCount,
                                            NULL
                        );
    return results;
}


/* NT drive lock - we don't want any interference... */

BOOL
    LockVolume(
               HANDLE hDisk
    )
{
    DWORD ReturnedByteCount;

    return DeviceIoControl(
                           hDisk,
                           FSCTL_LOCK_VOLUME,
                           NULL,
                           0,
                           NULL,
                           0,
                           &ReturnedByteCount,
                           NULL
                );
}



/* Windows 95 I/O definitions... */

#define VWIN32_DIOC_DOS_IOCTL 1
#define VWIN32_DIOC_DOS_INT25 2
#define VWIN32_DIOC_DOS_INT26 3

#pragma pack(1)
typedef struct _DIOC_REGISTERS {
    DWORD reg_EBX;
    DWORD reg_EDX;
    DWORD reg_ECX;
    DWORD reg_EAX;
    DWORD reg_EDI;
    DWORD reg_ESI;
    DWORD reg_Flags;
} DIOC_REGISTERS;

typedef struct _DPB {
    unsigned int dpb_sector;
    unsigned short dpb_count;
    char *dpb_buffer;
} DPB;

typedef struct _DEVICEPARAM {
    char junk1[7];
    unsigned short sec_size;
    char junk2[23];
} DEVICEPARAM;

#pragma pack()




/* Each device we talk to has a channel entry so that we can
   remember its details... */

#define CHAN_MAX 32
unsigned chan_count = 0;
struct CHANTAB {
    HANDLE handle;              /* File handle for device */
    char *IoBuffer;             /* Pointer to a buffer for the device */
    unsigned sectorsize;        /* Device sector size */
    unsigned last_sector;       /* Last sector no read (still in buffer) */
    short device_type;          /* Flag for 'normal' or ASPI I/O */
    short device_name;          /* Drive letter (A, B, C, ...) */
} chantab[CHAN_MAX];




/* Initialize device by opening it, locking it and getting it ready.. */

unsigned phyio_init(int devlen,char *devnam,unsigned *chanptr,struct phyio_info *info)
{
    unsigned sts = 1;
    unsigned chan = chan_count;
    if (is_NT > 1) getsysversion();
    if (chan < CHAN_MAX - 1 && devlen == 2 &&
        toupper(*devnam) >= 'A' && *(devnam + 1) == ':') {
        HANDLE hDrive;
        chantab[chan].device_name = toupper(*devnam);
        chantab[chan].device_type = 0;

        /* NT stuff */

        if (is_NT) {
            char ntname[32];
            DISK_GEOMETRY Geometry;
            sprintf(ntname,"\\\\.\\%s",devnam);
            chantab[chan].handle = hDrive = CreateFileA(
                                                       ntname,
                                                       GENERIC_READ,
                                                       FILE_SHARE_READ | FILE_SHARE_WRITE,
                                                       NULL,
                                                       OPEN_EXISTING,
                                                       FILE_FLAG_NO_BUFFERING,
                                                       NULL
                    );
            if (hDrive == INVALID_HANDLE_VALUE) {
                printf("Open %s failed %d\n",devnam,GetLastError());
                return SS$_NOSUCHDEV;
            }
            if (LockVolume(hDrive) == FALSE) {
                printf("LockVolume %s failed %d\n",devnam,GetLastError());
                return 72;
            }
            if (!GetDiskGeometry(hDrive,&Geometry)) {
                printf("GetDiskGeometry %s failed %d\n",devnam,GetLastError());
                return 80;
            }
            /* If this is a 2048 byte sector device past C treat it as a CD... */

            chantab[chan].device_type = 0;
            if (Geometry.BytesPerSector == 2048 && toupper(*devnam) > 'C') {
                sts = cd_initialize();
                if ((sts & 1) == 0) return sts;
                chantab[chan].device_type = 1;
            }
            chantab[chan].sectorsize = Geometry.BytesPerSector;
            info->sectors = (unsigned)(Geometry.Cylinders.QuadPart * Geometry.TracksPerCylinder *
                            Geometry.SectorsPerTrack);

        } else {

#if defined(USE_WIN95)
            /* W95 stuff */


            if (chantab[chan].device_name > 'C') {      /* Assume above C are CDs.. */
                sts = cd_initialize();
                if ((sts & 1) == 0) return sts;
                chantab[chan].device_type = 1;
                chantab[chan].sectorsize = 2048;
            } else {
                DIOC_REGISTERS reg;
                DEVICEPARAM deviceparam;
                BOOL fResult;
                DWORD cb;
                chantab[chan].handle = hDrive = CreateFileA(
                                                           "\\\\.\\vwin32",
                                                           0,0,
                                                           NULL,
                                                           0,
                                                           FILE_FLAG_DELETE_ON_CLOSE,
                                                           NULL);
                if (hDrive == INVALID_HANDLE_VALUE) {
                    printf("Open %s failed %d\n",devnam,GetLastError());
                    return SS$_NOSUCHDEV;
                }
                reg.reg_EAX = 0x440d;
                reg.reg_EBX = chantab[chan].device_name - 'A' + 1;
                reg.reg_ECX = 0x084a;   /* Lock volume */
                reg.reg_EDX = 0;
                reg.reg_Flags = 0x0000; /* Permission  */

                fResult = DeviceIoControl(hDrive,
                                          VWIN32_DIOC_DOS_IOCTL,
                                          &reg,sizeof(reg),
                                          &reg,sizeof(reg),
                                          &cb,0);

                if (!fResult || (reg.reg_Flags & 0x0001)) {
                    printf("Volume lock failed (%d)\n",GetLastError());
                    return SS$_DEVNOTALLOC;
                }
                reg.reg_EAX = 0x440d;
                reg.reg_EBX = chantab[chan].device_name - 'A' + 1;
                reg.reg_ECX = 0x0860;   /* Get device parameters */
                reg.reg_EDX = (DWORD) & deviceparam;
                reg.reg_Flags = 0x0001; /* set carry flag  */

                fResult = DeviceIoControl(hDrive,
                                          VWIN32_DIOC_DOS_IOCTL,
                                          &reg,sizeof(reg),
                                          &reg,sizeof(reg),
                                          &cb,0);

                if (!fResult || (reg.reg_Flags & 0x0001)) {
                    printf("Volume get parameters failed (%d)\n",GetLastError());
                    return 8;
                }
                chantab[chan].sectorsize = deviceparam.sec_size;
            }
            info->sectors = 0;
#endif /* defined(USE_WIN95) */
        }

        chantab[chan].IoBuffer = VirtualAlloc(NULL,
                                              chantab[chan].sectorsize + 304,MEM_COMMIT,PAGE_READWRITE);
        chantab[chan].last_sector = 1000;
        *chanptr = chan_count++;
        info->status = 0;
        info->sectorsize = chantab[chan].sectorsize;
        init_count++;
        return 1;
    } else {
        return SS$_IVCHAN;
    }
}




/* Read a physical sector... */

unsigned phy_getsect(unsigned chan,unsigned sector,char **buffptr)
{
    register unsigned sts = 1;
    if (sector != chantab[chan].last_sector) {
        if (chantab[chan].device_type) {
            sts = cd_read(sector,chantab[chan].IoBuffer);
        } else {
            if (is_NT) {
                DWORD BytesRead = -1;   /* NT Bytes read */
                SetFilePointer(chantab[chan].handle,
                               sector * chantab[chan].sectorsize,0,FILE_BEGIN);
                if (!ReadFile(chantab[chan].handle,chantab[chan].IoBuffer,
                              chantab[chan].sectorsize,&BytesRead,NULL)) sts = SS$_PARITY;
            } else {
                DIOC_REGISTERS reg;     /* W95 DIOC registers */
                DPB dpb;
                BOOL fResult;
                DWORD cb = 0;
                dpb.dpb_sector = sector;        /* sector number */
                dpb.dpb_count = 1;      /* sector count */
                dpb.dpb_buffer = chantab[chan].IoBuffer;        /* sector buffer */
                reg.reg_EAX = chantab[chan].device_name - 'A';  /* drive           */
                reg.reg_EBX = (DWORD) & dpb;    /* parameter block */
                reg.reg_ECX = -1;       /* use dpb    */
                reg.reg_EDX = 0;/* sector num      */
                reg.reg_EDI = 0;
                reg.reg_ESI = 0;
                reg.reg_Flags = 0x0001; /* set carry flag  */
                fResult = DeviceIoControl(chantab[chan].handle,
                                          VWIN32_DIOC_DOS_INT25,
                                          &reg,sizeof(reg),
                                          &reg,sizeof(reg),
                                          &cb,0);
                if (!fResult || (reg.reg_Flags & 0x0001)) {
                    printf("Sector %d read failed %d\n",sector,GetLastError());
                    sts = SS$_PARITY;
                }
            }
        }
    }
    if (sts & 1) {
        chantab[chan].last_sector = sector;
        *buffptr = chantab[chan].IoBuffer;
    }
    return sts;
}




/* Handle an I/O request ... need to read the approriate sectors to
   complete the request... */

unsigned phyio_read(unsigned chan,unsigned block,unsigned length,char *buffer)
{
    register unsigned sts = 1;

    if (chan < chan_count) {
        register unsigned sectorsize = chantab[chan].sectorsize;
        register unsigned sectno = block / (sectorsize / 512);
        register unsigned offset = (block - sectno * (sectorsize / 512)) * 512;
        while (length > 0) {
            register unsigned transfer;
            char *sectbuff;
            if (((sts = phy_getsect(chan,sectno,&sectbuff)) & 1) == 0) break;
            transfer = sectorsize - offset;
            if (transfer > length) transfer = length;
            memcpy(buffer,sectbuff + offset,transfer);
            buffer += transfer;
            length -= transfer;
            sectno++;
            offset = 0;
        }
    } else {
        sts = SS$_IVCHAN;
    }
    if (sts & 1) {
        read_count++;
    } else {
        printf("PHYIO Error %d Block %d Length %d\n",sts,block,length);
    }
    return sts;
}
