#define INCL_DOS
#define INCL_DOSDEVIOCTL
#define INCL_DOSERRORS
#define INCL_KBD
#define INCL_VIO
#include <os2.h>                /* include dos function declarations    */
#include <stdlib.h>             /* include C memory mgmt defines        */
#include <stdio.h>              /* include C memory mgmt defines        */
#include <malloc.h>             /* include C memory mgmt defines        */
#include <string.h>             /* include C memory mgmt defines        */
#pragma pack(1)

#define CDType         0x8000
#define FloppyType     0x4000
#define LanType        0x2000
#define LogicalType    0x1000
#define VDISKType      0x0800
#define OpticalType    0x0400
#define NonRemovable   0x01

#define DisketteCylinders 80

#define FirstDrive             0
#define B_Drive_0_Based        1
#define Base_1_offset          1
#define Binary_to_Printable 0x41
#define LastDrive             26
#define OpticalSectorsPerCluster 4

#define FSAttachSpace 100

// used for input to logical disk Get device parms Ioctl
struct {
        UCHAR Infotype;
        UCHAR DriveUnit;
        } DriveRequest;

// used for CD number of units Ioctl
struct {
        USHORT count;
        USHORT first;
        } cdinfo;

// array of device info to be collected
BIOSPARAMETERBLOCK devices[LastDrive];

//
//      Check if the B: drive is a logical drive by asking for its letter map
//      if there is ANY map, then it MUST be logical, otherwise default is physical
//
void CheckifBisLogical(UCHAR FixedDriveLetter)
{
  ULONG parmsize,datasize;
  ULONG Action;
  UCHAR DriveIndex=1,Parm,LogicalDrive;
  HFILE handle;
  PSZ devname="C:";

  devname[0]=FixedDriveLetter;
  if(!DosOpen(devname,&handle,&Action,0,FILE_NORMAL,OPEN_ACTION_OPEN_IF_EXISTS,OPEN_FLAGS_DASD|OPEN_SHARE_DENYNONE|OPEN_ACCESS_READONLY,NULL))
    {
    parmsize=sizeof(Parm);
    datasize=sizeof(LogicalDrive);
    Parm=0;
    LogicalDrive=DriveIndex+Base_1_offset;      // drive to check for (1 based)
    if(!DosDevIOCtl(handle,IOCTL_DISK,DSK_GETLOGICALMAP,
      (PVOID)&Parm, sizeof(Parm), &parmsize,
      (PVOID)&LogicalDrive,    sizeof(LogicalDrive)   , &datasize))
      {
      // if the response is non-zero, this drive letter may be
      // a logical drive mapped onto a physical drive
      if(LogicalDrive)
        {
        // mark it logical
        devices[DriveIndex].fsDeviceAttr |= LogicalType;  // say its a diskette drive
        } /* end if */
      } /* end if */
    DosClose(handle);
    } /* end if */
}

int main(int argc, char *argv[], char *envp)
{
  APIRET rc;
  ULONG parmsize,datasize;
  ULONG drivemap,currentdisk,mask,Action;
  UCHAR DriveIndex,NumDiskettesInstalled,NumDiskettesFound=0;
  HFILE handle;
  UCHAR devname[5]="c:";
  PFSQBUFFER2 fsinfo;
  ULONG len;
  PSZ name;
  UCHAR FirstHardfile=0;

  // clear the device workarea
  memset(&devices,0,sizeof(devices));

  // get number of physical diskette drives
  DosDevConfig(&NumDiskettesInstalled,DEVINFO_FLOPPY);

  // get current drive map
  rc=DosQueryCurrentDisk(&currentdisk,&drivemap);

  // loop thru the drive map
  for(DriveIndex=FirstDrive, mask=1L; DriveIndex<LastDrive; DriveIndex++, mask<<=1)
    {
    if(mask & drivemap)  // is this drive present
      {
      parmsize=sizeof(DriveRequest);
      datasize=sizeof(devices[DriveIndex]);
      DriveRequest.Infotype=0;
      DriveRequest.DriveUnit=DriveIndex;

      // ask for the device attributes, type & default BPB cylinder size

      if(!DosDevIOCtl(-1,IOCTL_DISK,DSK_GETDEVICEPARAMS,
        (PVOID)&DriveRequest, sizeof(DriveRequest), &parmsize,
        (PVOID)&devices[DriveIndex],    sizeof(devices[DriveIndex])   , &datasize))
        {
        if(FirstHardfile==0 && devices[DriveIndex].fsDeviceAttr==DEVTYPE_FIXED)
          {
          FirstHardfile=DriveIndex+Binary_to_Printable;
          } /* end if */
        } /* end if */
      } /* end if */
    } /* end for */

  // try to check for CD drives just in case
  if(!DosOpen("\\DEV\\CD-ROM2$",&handle,&Action,0,FILE_NORMAL,OPEN_ACTION_OPEN_IF_EXISTS,OPEN_SHARE_DENYNONE|OPEN_ACCESS_READONLY,NULL))
    {
    datasize=sizeof(cdinfo);
    if(!(rc=DosDevIOCtl(handle,0x82,0x60,
      NULL, 0, NULL,
      (PVOID)&cdinfo,    sizeof(cdinfo)   , &datasize)))
      {
      // mark those devices as CDs
      while(cdinfo.count--)
        {
        devices[cdinfo.first+cdinfo.count].fsDeviceAttr|=CDType;     // say its a CD device
        } /* end while */
      } /* end if */
    DosClose(handle);
    } /* end if */

  // turn off error popups for removables with no media
  DosError(FERR_DISABLEEXCEPTION | FERR_DISABLEHARDERR);

  // allocate space for DosQueryFSAttach response
  fsinfo=( PFSQBUFFER2 )malloc(FSAttachSpace);

  // loop thru the found devices
  for(DriveIndex=FirstDrive; DriveIndex<LastDrive; DriveIndex++)
    {
    // if the device is removable and NOT a CD
    if(devices[DriveIndex].bDeviceType)
      {
      if((devices[DriveIndex].fsDeviceAttr & (CDType | NonRemovable))==0)
        {
        devname[0]=DriveIndex+Binary_to_Printable;
        len=FSAttachSpace;
        rc=DosQueryFSAttach(devname,0L,
                           FSAIL_QUERYNAME,fsinfo,&len);
        if (rc==NO_ERROR)
          {
          if(fsinfo->iType==FSAT_REMOTEDRV)     // if remote
            devices[DriveIndex].fsDeviceAttr |= LanType;      // mark it so
          else if(fsinfo->iType==FSAT_LOCALDRV) // if local
            {
            name=fsinfo->szName;
            name+=strlen(name)+1;
            if(!strcmp(name,"FAT"))
              {
              // device is a removable FAT drive, so it MUST be diskette
              // as Optical has another name as does LAN and SRVIFS
              if(devices[DriveIndex].bSectorsPerCluster!=OpticalSectorsPerCluster)
                {
                devices[DriveIndex].fsDeviceAttr |= FloppyType;   // say its a diskette drive
                NumDiskettesFound++;                  // count it, assume physical drive
                if(DriveIndex==B_Drive_0_Based)       // is this for the B drive?
                  {
                  CheckifBisLogical(FirstHardfile);   // yes, see if logical
                  } /* end if */
                } /* end if */
              else
                {
                devices[DriveIndex].fsDeviceAttr |= OpticalType;  // say its a diskette drive
                } /* end else */
              } /* end if */
            } /* end else */
          } /* end if */
        else // must be no media or audio only (for CDs at least)
          {
          if(devices[DriveIndex].cCylinders<=DisketteCylinders)// floppies will always be 80
            {                                      // or less cylinders
            if(devices[DriveIndex].bSectorsPerCluster!=OpticalSectorsPerCluster)
              {
              devices[DriveIndex].fsDeviceAttr |= FloppyType;    // say its a diskette drive
              NumDiskettesFound++;                   // count it, assume physical drive
              if(DriveIndex==B_Drive_0_Based)        // is this for the B drive?
                {
                CheckifBisLogical(FirstHardfile);    // yes, see if logical
                } /* end if */
              } /* end if */
            else
              {
              devices[DriveIndex].fsDeviceAttr |= OpticalType;  // say its a diskette drive
              } /* end else */
            } /* end if */
          } /* end else */
        } /* end if */
      else      // non removable or CD type. maybe VDISK
        {
        if(!(devices[DriveIndex].fsDeviceAttr & CDType))    // if NOT CD
          {
          if(devices[DriveIndex].cFATs==1)                  // is there only one FAT?
            {
            devices[DriveIndex].fsDeviceAttr |= VDISKType;  // is vdisk
            } /* end if */
          } /* end if */
        } /* end else */
      } /* end if */
    } /* end for */
  free(fsinfo);

  // check to see if we accounted for all physical diskette drives install
  if(NumDiskettesInstalled!=NumDiskettesFound)
    {
    // found more diskettes than physically installed, some are logical
    if(NumDiskettesInstalled<NumDiskettesFound)
      {
      printf("%d more diskette drive(s) were found than are reported physically installed\n",NumDiskettesFound-NumDiskettesInstalled);
      } /* end if */
    else
      {
      //
      // This is a code bug, we didn't find all the physical diskette drives
      // we should NEVER EVER get here
      //
      printf("Some diskette drive was not detected properly\n");
      printf("Diskettes installed =%d Diskettes detected = %d\n", NumDiskettesInstalled, NumDiskettesFound);
      } /* end else */
    } /* end if */

  // display the device types we found
  for(DriveIndex=FirstDrive; DriveIndex<LastDrive; DriveIndex++)
    {
    if(devices[DriveIndex].bDeviceType)
      {
      printf("Device %c is ", DriveIndex+Binary_to_Printable);
      if(devices[DriveIndex].fsDeviceAttr & FloppyType)
        {
        printf("a  %s diskette drive",devices[DriveIndex].fsDeviceAttr & LogicalType?"logical ":"physical");
        } /* end if */
      else if(devices[DriveIndex].fsDeviceAttr & CDType)
        {
        printf("a  CDROM drive");
        } /* end else */
      else if(devices[DriveIndex].fsDeviceAttr & LanType)
        {
        printf("a  redirected drive");
        } /* end else */
      else if(devices[DriveIndex].fsDeviceAttr & VDISKType)
        {
        printf("a  virtual disk");
        } /* end else */
      else if(devices[DriveIndex].fsDeviceAttr & OpticalType)
        {
        printf("an Optical disk");
        } /* end else */
      else if(devices[DriveIndex].bDeviceType == DEVTYPE_FIXED)
        {
        printf("a  hardfile partition");
        } /* end else */
      else if(devices[DriveIndex].bDeviceType == DEVTYPE_UNKNOWN)
        {
        // we should really really never see these if the code above
        // is right (no bernouli or optical drives with no media to test)
        if(devices[DriveIndex].fsDeviceAttr & NonRemovable)
          {
          printf("an unknown non-removable media device Attrib=%02X",
             devices[DriveIndex].fsDeviceAttr );
          } /* end if */
        else
          {
          printf("an unknown removable media device Attrib=%02X",
             devices[DriveIndex].fsDeviceAttr );
          } /* end else */
        } /* end else */
      printf("\n");
      } /* end if */
    } /* end for */
  return rc;
}
