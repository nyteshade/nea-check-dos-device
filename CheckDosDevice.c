/**
 * CheckDosDevice - Check if a DOS device has a mounted volume
 *
 * This CLI command checks whether a specified DOS device exists and whether
 * it has an actual volume mounted. Useful for checking diskimage.device units
 * before mounting.
 *
 * Returns:
 *   OK (0) - Device exists and has a volume mounted
 *   WARN (5) - Device exists but no disk present (safe to mount)
 *   ERROR (10) - Device doesn't exist
 *   FAIL (20) - Driver not available
 *
 * Usage: CheckDosDevice <device>
 *
 * Examples:
 *   CheckDosDevice IHD101
 *   CheckDosDevice IMG0
 *   CheckDosDevice DISKIMAGE5
 *
 * Compile with SAS/C:
 *   sc link startup=cres smalldata smallcode nostackcheck CheckDosDevice.c
 *
 * @author Brielle Harrison <nyteshade@gmail.com>
 * @author Anthropic Claude Sonnet 4 (29Jun2025)
 */

#include <exec/types.h>
#include <exec/memory.h>
#include <exec/io.h>
#include <dos/dos.h>
#include <dos/dosextens.h>
#include <dos/filehandler.h>
#include <dos/rdargs.h>
#include <proto/exec.h>
#include <proto/dos.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

/* Version string for AmigaOS version command */
const char * const version =
  "$VER: CheckDosDevice 1.2 (29.06.2025) "
  "Brielle Harrison";

/* Template for ReadArgs */
#define TEMPLATE "DEVICE/A,QUIET/S,DRIVER/K,INFO/S,MOUNTLIST/S"

/* Magic value to determine if thread local context is ours */
#define CONTEXT_MAGIC 0x434B4456 /* 'CKDV' */

/* Return codes */
#define RC_OK          0   /* Device exists with mounted volume */
#define RC_WARN        5   /* Device exists but no disk present */
#define RC_ERROR      10   /* Device doesn't exist */
#define RC_FAIL       20   /* Driver not available */

typedef struct Context {
  ULONG magic;
  BOOL quiet;
  APTR oldContext;
} Context;

/**
 * Structure for command line arguments
 */
struct Arguments {
  STRPTR device;    /* Device name to check */
  LONG quiet;       /* Suppress output if TRUE */
  STRPTR driver;    /* Device driver name (default: diskimage.device) */
  LONG info;        /* Show device information */
  LONG mountlist;   /* Generate mountlist entry */
};

/* Function prototypes */
Context *GetContext(void);
void FreeContext(void);
int exitWith(int rc);

void OPrintf(const char *format, ...);
BOOL CheckDeviceDriver(const char *driverName);
BOOL IsNumber(const char *str);
BOOL FindDeviceByDriverAndUnit(const char *driverName, LONG unitNum, char *foundName, int nameSize);
struct DeviceNode *FindDosDevice(const char *deviceName);
void StripDeviceName(const char *deviceName, char *cleanName, int bufSize);
int CheckDeviceStatus(const char *deviceName, char *volumeName, int volumeNameSize);
void ShowDeviceInfo(const char *deviceName, struct DeviceNode *deviceNode);
void GenerateMountlist(const char *deviceName, struct DeviceNode *deviceNode);
void FindMatchingDevices(const char *pattern);
const char *GetHandlerFromDosType(ULONG dosType);

Context *GetContext(void) {
  struct Task *task = FindTask(NULL);
  Context *context = task->tc_UserData;

  /* Verify it's our context */
  if (context && context->magic == CONTEXT_MAGIC) {
    return context;
  }

  /* Need to allocate */
  context = AllocVec(sizeof(Context), MEMF_CLEAR);
  if (!context) {
    return NULL;  /* Handle allocation failure */
  }

  context->magic = CONTEXT_MAGIC;
  context->quiet = FALSE;
  context->oldContext = task->tc_UserData;
  task->tc_UserData = (APTR)context;

  return context;
}

void FreeContext(void) {
  struct Task *task = FindTask(NULL);
  if (task->tc_UserData != NULL) {
    FreeVec(task->tc_UserData);
    task->tc_UserData = NULL;
  }
}

/**
 * Optional Printf - only prints if not in quiet mode
 *
 * @param format Printf-style format string
 * @param ... Variable arguments
 */
 void OPrintf(const char *format, ...) {
   va_list args;
   Context *context = GetContext();
   BOOL b_quiet = FALSE;

   if (context) {
     b_quiet = context->quiet;
   }

   if (!b_quiet) {  /* Add this check */
     /* Can't get context, just print */
     va_start(args, format);
     VPrintf((STRPTR)format, (APTR)args);
     va_end(args);
     return;
   }
}

/**
 * Check if a device driver is available
 *
 * @param driverName Name of the device driver (e.g., "diskimage.device")
 * @return TRUE if device driver can be opened, FALSE otherwise
 */
BOOL CheckDeviceDriver(const char *driverName) {
  struct MsgPort *msgPort = NULL;
  struct IOStdReq *ioReq = NULL;
  BOOL available = FALSE;
  BYTE error;

  /* Create a message port for IO communication */
  msgPort = CreateMsgPort();
  if (!msgPort) {
    return FALSE;
  }

  /* Create an IO request */
  ioReq = (struct IOStdReq *)CreateIORequest(msgPort, sizeof(struct IOStdReq));
  if (!ioReq) {
    DeleteMsgPort(msgPort);
    return FALSE;
  }

  /* Try to open the device driver unit 0 just to test availability */
  error = OpenDevice((STRPTR)driverName, 0, (struct IORequest *)ioReq, 0);

  if (error == 0) {
    /* Device opened successfully - it's available */
    available = TRUE;
    CloseDevice((struct IORequest *)ioReq);
  }

  /* Clean up */
  DeleteIORequest((struct IORequest *)ioReq);
  DeleteMsgPort(msgPort);

  return available;
}

/**
 * Check if a string is a pure number
 *
 * @param str String to check
 * @return TRUE if string contains only digits, FALSE otherwise
 */
BOOL IsNumber(const char *str) {
  const char *p = str;

  if (!str || !*str) {
    return FALSE;
  }

  while (*p) {
    if (*p < '0' || *p > '9') {
      return FALSE;
    }
    p++;
  }

  return TRUE;
}

/**
 * Get filesystem handler from DosType
 *
 * @param dosType The DosType value from the environment
 * @return Handler path string
 */
const char *GetHandlerFromDosType(ULONG dosType) {
  switch (dosType) {
    case 0x444F5300:  /* DOS\0 - OFS */
    case 0x444F5301:  /* DOS\1 - FFS */
      return "L:FastFileSystem";

    case 0x444F5302:  /* DOS\2 - OFS INTL */
    case 0x444F5303:  /* DOS\3 - FFS INTL */
      return "L:FastFileSystem";

    case 0x444F5304:  /* DOS\4 - OFS DC */
    case 0x444F5305:  /* DOS\5 - FFS DC */
      return "L:FastFileSystem";

    case 0x444F5306:  /* DOS\6 - OFS LNFS */
    case 0x444F5307:  /* DOS\7 - FFS LNFS */
      return "L:FastFileSystem";

    case 0x50465300:  /* PFS\0 - Professional File System */
    case 0x50465301:  /* PFS\1 */
    case 0x50465302:  /* PFS\2 */
      return "L:PFSFileSystem";

    case 0x53465300:  /* SFS\0 - Smart File System */
      return "L:SmartFilesystem";

    case 0x4E425500:  /* NBU\0 - NetBSD UFS */
    case 0x4E425520:  /* NBU  - NetBSD UFS */
      return "L:NetBSDFileSystem";

    case 0x6D756673:  /* mufs - Multi User File System */
      return "L:MultiUserFileSystem";

    case 0x41465300:  /* AFS\0 - Ami File Safe */
    case 0x41465301:  /* AFS\1 */
      return "L:AmiFileSafe";

    default:
      /* Check for CrossDOS signatures */
      if ((dosType & 0xFFFFFF00) == 0x4D534400) {  /* MSD\x - CrossDOS */
        return "L:CrossDOSFileSystem";
      }
      /* Default to FFS for unknown types */
      return "L:FastFileSystem";
  }
}

/**
 * Find a device by unit number and driver name
 *
 * Searches the DOS device list for any device using the specified
 * device driver with the specified unit number.
 *
 * @param driverName Device driver name (e.g., "diskimage.device")
 * @param unitNum Unit number to search for
 * @param foundName Buffer to store the found device name (optional)
 * @param nameSize Size of foundName buffer
 * @return TRUE if found, FALSE otherwise
 */
BOOL FindDeviceByDriverAndUnit(
  const char *driverName,
  LONG unitNum,
  char *foundName,
  int nameSize
) {
  struct RootNode *rootNode;
  struct DosInfo *dosInfo;
  struct DeviceNode *deviceNode;
  struct FileSysStartupMsg *startup;
  char *bstrName;
  char devName[108];
  BOOL found = FALSE;

  if (foundName && nameSize > 0) {
    foundName[0] = '\0';
  }

  /* Get the DOS root node */
  rootNode = (struct RootNode *)DOSBase->dl_Root;
  dosInfo = (struct DosInfo *)BADDR(rootNode->rn_Info);

  /* Lock the DOS device list */
  Forbid();

  /* Walk through the device list */
  deviceNode = (struct DeviceNode *)BADDR(dosInfo->di_DevInfo);

  while (deviceNode && !found) {
    /* Check if this device has startup info */
    if (deviceNode->dn_Startup) {
      startup = (struct FileSysStartupMsg *)BADDR(deviceNode->dn_Startup);

      /* Check if it has a device name */
      if (startup->fssm_Device) {
        /* Get device driver name */
        bstrName = (char *)BADDR(startup->fssm_Device);
        if (bstrName && bstrName[0] > 0) {
          /* Convert BCPL string to C string */
          int len = bstrName[0];
          if (len < sizeof(devName) - 1) {
            memcpy(devName, &bstrName[1], len);
            devName[len] = '\0';

            /* Check if this is the driver we're looking for */
            if (stricmp(devName, driverName) == 0) {
              /* Check if unit number matches */
              if (startup->fssm_Unit == unitNum) {
                /* Found it! Get the DOS device name */
                bstrName = (char *)BADDR(deviceNode->dn_Name);
                if (bstrName && bstrName[0] > 0) {
                  len = bstrName[0];
                  if (foundName && len < nameSize - 1) {
                    memcpy(foundName, &bstrName[1], len);
                    foundName[len] = '\0';
                  }
                  found = TRUE;
                }
              }
            }
          }
        }
      }
    }

    /* Move to next device */
    deviceNode = (struct DeviceNode *)BADDR(deviceNode->dn_Next);
  }

  Permit();

  return found;
}

/**
 * Display device information
 *
 * @param deviceName Device name
 * @param deviceNode Device node pointer
 */
void ShowDeviceInfo(const char *deviceName, struct DeviceNode *deviceNode) {
  struct FileSysStartupMsg *startup;
  struct DosEnvec *environ;
  char *bstrName;
  char driverName[108];

  OPrintf("\nDevice Information for %s:\n", deviceName);
  OPrintf("----------------------------------------\n");

  /* Check device type */
  OPrintf("Type: ");
  if (deviceNode->dn_Type == DLT_DEVICE) {
    OPrintf("Device\n");
  }
  else if (deviceNode->dn_Type == DLT_VOLUME) {
    OPrintf("Volume\n");
  }
  else {
    OPrintf("Unknown (%ld)\n", deviceNode->dn_Type);
  }

  /* Get startup information if available */
  if (deviceNode->dn_Startup) {
    startup = (struct FileSysStartupMsg *)BADDR(deviceNode->dn_Startup);

    /* Device driver name */
    if (startup->fssm_Device) {
      bstrName = (char *)BADDR(startup->fssm_Device);
      if (bstrName && bstrName[0] > 0) {
        int len = bstrName[0];
        if (len < sizeof(driverName) - 1) {
          memcpy(driverName, &bstrName[1], len);
          driverName[len] = '\0';
          OPrintf("Driver: %s\n", driverName);
        }
      }
    }

    /* Unit number */
    OPrintf("Unit: %ld\n", (LONG)startup->fssm_Unit);
    OPrintf("Flags: 0x%08lx\n", startup->fssm_Flags);

    /* Environment vector */
    if (startup->fssm_Environ) {
      environ = (struct DosEnvec *)BADDR(startup->fssm_Environ);
      OPrintf("\nEnvironment:\n");
      OPrintf("  Surfaces: %ld\n", environ->de_Surfaces);
      OPrintf("  Blocks per Track: %ld\n", environ->de_BlocksPerTrack);
      OPrintf("  Reserved Blocks: %ld\n", environ->de_Reserved);
      OPrintf("  Interleave: %ld\n", environ->de_Interleave);
      OPrintf("  Low Cylinder: %ld\n", environ->de_LowCyl);
      OPrintf("  High Cylinder: %ld\n", environ->de_HighCyl);
      OPrintf("  Buffers: %ld\n", environ->de_NumBuffers);
      OPrintf("  Buffer Memory Type: 0x%08lx\n", environ->de_BufMemType);

      if (environ->de_TableSize >= 12) {
        OPrintf("  Max Transfer: 0x%08lx\n", environ->de_MaxTransfer);
        OPrintf("  Mask: 0x%08lx\n", environ->de_Mask);
        OPrintf("  Boot Priority: %ld\n", environ->de_BootPri);
        OPrintf("  DosType: 0x%08lx", environ->de_DosType);

        /* Show DosType as string if printable */
        if (environ->de_DosType) {
          char dosTypeStr[5];
          dosTypeStr[0] = (environ->de_DosType >> 24) & 0xFF;
          dosTypeStr[1] = (environ->de_DosType >> 16) & 0xFF;
          dosTypeStr[2] = (environ->de_DosType >> 8) & 0xFF;
          dosTypeStr[3] = environ->de_DosType & 0xFF;
          dosTypeStr[4] = '\0';
          OPrintf(" ('%s')", dosTypeStr);
        }
        OPrintf("\n");
      }
    }
  }
  else {
    OPrintf("No startup information available\n");
  }

  /* Handler/filesystem info */
  if (deviceNode->dn_Handler) {
    OPrintf("\nHandler: ");
    bstrName = (char *)BADDR(deviceNode->dn_Handler);
    if (bstrName && bstrName[0] > 0) {
      int len = bstrName[0];
      char handlerName[108];
      if (len < sizeof(handlerName) - 1) {
        memcpy(handlerName, &bstrName[1], len);
        handlerName[len] = '\0';
        OPrintf("%s\n", handlerName);
      }
    }
    else {
      OPrintf("0x%08lx\n", deviceNode->dn_Handler);
    }
  }

  OPrintf("----------------------------------------\n");
}

/**
 * Generate mountlist entry for a device
 *
 * @param deviceName Device name
 * @param deviceNode Device node pointer
 */
void GenerateMountlist(const char *deviceName, struct DeviceNode *deviceNode) {
  struct FileSysStartupMsg *startup;
  struct DosEnvec *environ;
  char *bstrName;
  char driverName[108];
  char handlerName[108];
  BOOL hasHandler = FALSE;
  BOOL useAutoHandler = FALSE;

  OPrintf("\n/* Mountlist entry for %s: */\n", deviceName);
  OPrintf("%s:\n", deviceName);

  /* Get handler name if available */
  if (deviceNode->dn_Handler) {
    bstrName = (char *)BADDR(deviceNode->dn_Handler);
    if (bstrName && bstrName[0] > 0) {
      int len = bstrName[0];
      if (len < sizeof(handlerName) - 1) {
        memcpy(handlerName, &bstrName[1], len);
        handlerName[len] = '\0';
        hasHandler = TRUE;
      }
    }
  }

  /* Get startup information */
  if (deviceNode->dn_Startup) {
    startup = (struct FileSysStartupMsg *)BADDR(deviceNode->dn_Startup);

    /* Try to determine handler from DosType if we don't have one */
    if (!hasHandler && startup->fssm_Environ) {
      environ = (struct DosEnvec *)BADDR(startup->fssm_Environ);
      if (environ->de_TableSize >= 12 && environ->de_DosType) {
        const char *autoHandler = GetHandlerFromDosType(environ->de_DosType);
        strcpy(handlerName, autoHandler);
        hasHandler = TRUE;
        useAutoHandler = TRUE;
      }
    }

    /* Output handler */
    if (hasHandler) {
      OPrintf("    Handler = %s", handlerName);
      if (useAutoHandler) {
        OPrintf("  /* Detected from DosType */");
      }
      OPrintf("\n");
    }
    else {
      OPrintf("    Handler = L:FastFileSystem  /* Update as needed */\n");
    }

    /* Device driver name */
    if (startup->fssm_Device) {
      bstrName = (char *)BADDR(startup->fssm_Device);
      if (bstrName && bstrName[0] > 0) {
        int len = bstrName[0];
        if (len < sizeof(driverName) - 1) {
          memcpy(driverName, &bstrName[1], len);
          driverName[len] = '\0';
          OPrintf("    Device = %s\n", driverName);
        }
      }
    }

    /* Unit number */
    OPrintf("    Unit = %ld\n", (LONG)startup->fssm_Unit);

    /* Flags */
    if (startup->fssm_Flags != 0) {
      OPrintf("    Flags = %ld\n", startup->fssm_Flags);
    }

    /* Environment vector */
    if (startup->fssm_Environ) {
      environ = (struct DosEnvec *)BADDR(startup->fssm_Environ);

      OPrintf("    Surfaces = %ld\n", environ->de_Surfaces);
      OPrintf("    BlocksPerTrack = %ld\n", environ->de_BlocksPerTrack);
      if (environ->de_Reserved != 2) {
        OPrintf("    Reserved = %ld\n", environ->de_Reserved);
      }
      if (environ->de_Interleave != 0) {
        OPrintf("    Interleave = %ld\n", environ->de_Interleave);
      }
      OPrintf("    LowCyl = %ld\n", environ->de_LowCyl);
      OPrintf("    HighCyl = %ld\n", environ->de_HighCyl);
      OPrintf("    Buffers = %ld\n", environ->de_NumBuffers);

      /* Optional extended parameters */
      if (environ->de_TableSize >= 12) {
        if (environ->de_BufMemType != 0) {
          OPrintf("    BufMemType = 0x%08lx\n", environ->de_BufMemType);
        }
        if (environ->de_MaxTransfer != 0x7FFFFFFF) {
          OPrintf("    MaxTransfer = 0x%08lx\n", environ->de_MaxTransfer);
        }
        if (environ->de_Mask != 0xFFFFFFFE) {
          OPrintf("    Mask = 0x%08lx\n", environ->de_Mask);
        }
        if (environ->de_BootPri != 0) {
          OPrintf("    BootPri = %ld\n", environ->de_BootPri);
        }
        if (environ->de_DosType != 0x444F5300) { /* DOS\0 */
          OPrintf("    DosType = 0x%08lx\n", environ->de_DosType);
        }
      }
    }
  }
  else {
    OPrintf("    /* No device information available */\n");
    OPrintf("    /* You'll need to fill in the details manually */\n");
  }

  OPrintf("#\n");
}
struct DeviceNode *FindDosDevice(const char *deviceName) {
  struct RootNode *rootNode;
  struct DosInfo *dosInfo;
  struct DeviceNode *deviceNode;
  char *bstrName;
  char devName[108];  /* Max DOS name length */

  /* Get the DOS root node */
  rootNode = (struct RootNode *)DOSBase->dl_Root;
  dosInfo = (struct DosInfo *)BADDR(rootNode->rn_Info);

  /* Lock the DOS device list */
  Forbid();

  /* Walk through the device list */
  deviceNode = (struct DeviceNode *)BADDR(dosInfo->di_DevInfo);

  while (deviceNode) {
    /* Get the device name from BCPL string */
    bstrName = (char *)BADDR(deviceNode->dn_Name);
    if (bstrName && bstrName[0] > 0) {
      /* Convert BCPL string to C string */
      int len = bstrName[0];
      if (len < sizeof(devName) - 1) {
        memcpy(devName, &bstrName[1], len);
        devName[len] = '\0';

        /* Compare with our target device name */
        if (stricmp(devName, deviceName) == 0) {
          Permit();
          return deviceNode;
        }
      }
    }

    /* Move to next device */
    deviceNode = (struct DeviceNode *)BADDR(deviceNode->dn_Next);
  }

  Permit();
  return NULL;
}

/**
 * Strip colon from device name if present
 *
 * @param deviceName Input device name
 * @param cleanName Output buffer for clean name
 * @param bufSize Size of output buffer
 */
void StripDeviceName(const char *deviceName, char *cleanName, int bufSize) {
  int len = strlen(deviceName);

  /* Copy the device name */
  strncpy(cleanName, deviceName, bufSize - 1);
  cleanName[bufSize - 1] = '\0';

  /* Remove trailing colon if present */
  len = strlen(cleanName);
  if (len > 0 && cleanName[len - 1] == ':') {
    cleanName[len - 1] = '\0';
  }
}

/**
 * Check device status
 *
 * @param deviceName The device name (with or without colon)
 * @param volumeName Buffer to store volume name (optional, can be NULL)
 * @param volumeNameSize Size of volume name buffer
 * @return 0 = has volume, 1 = no disk, -1 = device not found
 */
int CheckDeviceStatus(
  const char *deviceName,
  char *volumeName,
  int volumeNameSize
) {
  char cleanName[108];
  char fullName[110];
  struct DeviceNode *deviceNode;
  BPTR lock = 0;
  struct InfoData *infoData = NULL;
  struct DeviceList *volumeNode;
  char *bstrName;
  int status = -1;  /* Default: device not found */

  /* Clean the device name */
  StripDeviceName(deviceName, cleanName, sizeof(cleanName));

  /* Check if device exists in DOS list */
  deviceNode = FindDosDevice(cleanName);
  if (!deviceNode) {
    return -1;  /* Device not found */
  }

  /* Device exists, now check if volume is mounted */
  sprintf(fullName, "%s:", cleanName);
  lock = Lock(fullName, ACCESS_READ);

  if (lock) {
    /* Allocate InfoData structure */
    infoData = AllocMem(sizeof(struct InfoData), MEMF_CLEAR);
    if (infoData) {
      /* Get information about the device */
      if (Info(lock, infoData)) {
        /* Check if there's a disk in the device */
        if (infoData->id_DiskType != ID_NO_DISK_PRESENT) {
          status = 0;  /* Volume is mounted */

          /* Get volume name if buffer provided */
          if (volumeName && volumeNameSize > 0) {
            volumeName[0] = '\0';
            volumeNode = BADDR(infoData->id_VolumeNode);
            if (volumeNode && volumeNode->dl_Name) {
              bstrName = (char *)BADDR(volumeNode->dl_Name);
              if (bstrName && bstrName[0] > 0) {
                int len = bstrName[0];
                if (len < volumeNameSize - 1) {
                  memcpy(volumeName, &bstrName[1], len);
                  volumeName[len] = '\0';
                }
              }
            }
          }
        }
        else {
          status = 1;  /* No disk present */
        }
      }
      FreeMem(infoData, sizeof(struct InfoData));
    }
    UnLock(lock);
  }
  else {
    /* Couldn't lock - device exists but no handler/disk */
    status = 1;  /* No disk present */
  }

  return status;
}

int exitWith(int rc) {
  struct Task *task = FindTask(NULL);
  Context *context = (Context *)task->tc_UserData;

  if (context && context->magic == CONTEXT_MAGIC) {
    task->tc_UserData = context->oldContext;
    FreeVec(context);
  }

  return rc;
}

/**
 * Main entry point
 */
int main(void) {
  struct RDArgs *rdArgs = NULL;
  struct Arguments args = { NULL, FALSE, NULL, FALSE, FALSE };
  struct Context *context = GetContext();

  char volumeName[64];
  char cleanName[108];
  char foundDevice[108];
  STRPTR driverName;
  struct DeviceNode *deviceNode;
  struct Process *proc;
  APTR oldWindowPtr;
  int status;
  int returnCode = RC_ERROR;
  LONG unitNum;

  /* Parse command line arguments */
  rdArgs = ReadArgs(TEMPLATE, (LONG *)&args, NULL);
  if (!rdArgs) {
    Printf("Usage: CheckDosDevice <DEVICE> [QUIET] [<DRIVER> driver] [INFO] [MOUNTLIST]\n");
    Printf("  DEVICE    - DOS device name or unit number\n");
    Printf("  QUIET     - Suppress output\n");
    Printf("  DRIVER    - Device driver name (default: diskimage.device)\n");
    Printf("  INFO      - Show detailed device information\n");
    Printf("  MOUNTLIST - Generate mountlist entry\n");
    Printf("\nExamples:\n");
    Printf("  CheckDosDevice IHD101\n");
    Printf("  CheckDosDevice 101 INFO\n");
    Printf("  CheckDosDevice DF0: MOUNTLIST\n");
    Printf("  CheckDosDevice 0 DRIVER trackdisk.device\n");
    return exitWith(RC_ERROR);
  }

  /* Set global quiet flag */
  context->quiet = args.quiet ? TRUE : FALSE;

  /* Get driver name (default to diskimage.device) */
  driverName = args.driver ? args.driver : (STRPTR)"diskimage.device";

  /* Disable system requesters early to prevent any popups */
  proc = (struct Process *)FindTask(NULL);
  oldWindowPtr = proc->pr_WindowPtr;
  proc->pr_WindowPtr = (APTR)-1L;

  /* First check if the device driver is available */
  if (!CheckDeviceDriver(driverName)) {
    OPrintf("Device driver %s not available\n", driverName);
    proc->pr_WindowPtr = oldWindowPtr;
    FreeArgs(rdArgs);
    return exitWith(RC_FAIL);
  }

  /* Check if argument is a pure number */
  if (IsNumber(args.device)) {
    /* Convert to unit number */
    unitNum = atol(args.device);

    /* Find device with this unit number and driver */
    if (FindDeviceByDriverAndUnit(
      driverName,
      unitNum,
      foundDevice,
      sizeof(foundDevice)
    )) {
      OPrintf("Found %s unit %ld as %s:\n", driverName, unitNum, foundDevice);
      /* Use the found device name for checking */
      StripDeviceName(foundDevice, cleanName, sizeof(cleanName));
    }
    else {
      OPrintf("No %s found with unit %ld\n", driverName, unitNum);
      proc->pr_WindowPtr = oldWindowPtr;
      FreeArgs(rdArgs);
      return exitWith(RC_ERROR);
    }
  }
  else {
    /* Normal device name */
    StripDeviceName(args.device, cleanName, sizeof(cleanName));
  }

  /* Get device node for INFO/MOUNTLIST operations */
  deviceNode = FindDosDevice(cleanName);

  /* Show device info if requested */
  if (args.info && deviceNode) {
    ShowDeviceInfo(cleanName, deviceNode);
  }

  /* Generate mountlist if requested */
  if (args.mountlist && deviceNode) {
    GenerateMountlist(cleanName, deviceNode);
  }

  /* Check device status (unless we only want info/mountlist) */
  if (!args.info && !args.mountlist) {
    status = CheckDeviceStatus(cleanName, volumeName, sizeof(volumeName));

    switch (status) {
      case 0:  /* Volume mounted */
        if (volumeName[0]) {
          OPrintf("%s: has mounted volume \"%s\"\n", cleanName, volumeName);
        }
        else {
          OPrintf("%s: has mounted volume\n", cleanName);
        }
        returnCode = RC_OK;
        break;

      case 1:  /* No disk present */
        OPrintf("%s: no disk present\n", cleanName);
        returnCode = RC_WARN;
        break;

      case -1:  /* Device not found */
        OPrintf("%s: device not found\n", cleanName);
        returnCode = RC_ERROR;
        break;
    }
  }
  else if (deviceNode) {
    /* If only info/mountlist requested and device exists, return OK */
    returnCode = RC_OK;
  }

  /* Restore requester state */
  proc->pr_WindowPtr = oldWindowPtr;

  /* Free the ReadArgs structure */
  FreeArgs(rdArgs);

  return exitWith(returnCode);
}

/**
 * Find all devices matching a pattern
 *
 * This function can be used to find devices that match a pattern
 * like "IHD*" or "IMG*" and check their status.
 *
 * @param pattern Pattern to match (simple wildcards: * = any chars)
 */
void FindMatchingDevices(const char *pattern) {
  struct RootNode *rootNode;
  struct DosInfo *dosInfo;
  struct DeviceNode *deviceNode;
  char *bstrName;
  char devName[108];
  char volumeName[64];
  int status;
  int patLen;
  BOOL foundAny = FALSE;

  OPrintf("Devices matching pattern \"%s\":\n", pattern);

  /* Get pattern length for simple matching */
  patLen = strlen(pattern);

  /* Get the DOS root node */
  rootNode = (struct RootNode *)DOSBase->dl_Root;
  dosInfo = (struct DosInfo *)BADDR(rootNode->rn_Info);

  /* Lock the DOS device list */
  Forbid();

  /* Walk through the device list */
  deviceNode = (struct DeviceNode *)BADDR(dosInfo->di_DevInfo);

  while (deviceNode) {
    /* Get the device name from BCPL string */
    bstrName = (char *)BADDR(deviceNode->dn_Name);
    if (bstrName && bstrName[0] > 0) {
      /* Convert BCPL string to C string */
      int len = bstrName[0];
      if (len < sizeof(devName) - 1) {
        memcpy(devName, &bstrName[1], len);
        devName[len] = '\0';

        /* Simple pattern matching (could be enhanced) */
        /* For now, just check prefix if pattern ends with * */
        if (patLen > 0 && pattern[patLen-1] == '*') {
          if (strnicmp(devName, pattern, patLen-1) == 0) {
            foundAny = TRUE;
            Permit();

            /* Check device status */
            status = CheckDeviceStatus(devName, volumeName, sizeof(volumeName));
            switch (status) {
              case 0:
                if (volumeName[0]) {
                  OPrintf("  %s: Volume \"%s\"\n", devName, volumeName);
                }
                else {
                  OPrintf("  %s: Volume mounted\n", devName);
                }
                break;
              case 1:
                OPrintf("  %s: No disk present\n", devName);
                break;
            }

            Forbid();
          }
        }
      }
    }

    /* Move to next device */
    deviceNode = (struct DeviceNode *)BADDR(deviceNode->dn_Next);
  }

  Permit();

  if (!foundAny) {
    OPrintf("No devices found matching pattern \"%s\"\n", pattern);
  }
}
