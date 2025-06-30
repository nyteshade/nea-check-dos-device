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
 *   sc LINK CheckDosDevice.c
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
const char *version =
  "$VER: CheckDosDevice 1.0 (29.06.2025) "
  "Brielle Harrison";

/* Template for ReadArgs */
#define TEMPLATE "DEVICE/A,QUIET/S,DRIVER/K"

/* Return codes */
#define RC_OK          0   /* Device exists with mounted volume */
#define RC_WARN        5   /* Device exists but no disk present */
#define RC_ERROR      10   /* Device doesn't exist */
#define RC_FAIL       20   /* Driver not available */

/* Global quiet flag for OPrintf */
static BOOL g_quiet = FALSE;

/**
 * Structure for command line arguments
 */
struct Arguments {
  STRPTR device;  /* Device name to check */
  LONG quiet;     /* Suppress output if TRUE */
  STRPTR driver;  /* Device driver name (default: diskimage.device) */
};

/**
 * Optional Printf - only prints if not in quiet mode
 *
 * @param format Printf-style format string
 * @param ... Variable arguments
 */
void OPrintf(const char *format, ...) {
  va_list args;
  
  if (!g_quiet) {
    va_start(args, format);
    VPrintf((STRPTR)format, (APTR)args);
    va_end(args);
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
 * Check if a DOS device exists
 *
 * @param deviceName The device name to check (without colon)
 * @return Pointer to DeviceNode if found, NULL otherwise
 */
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

/**
 * Main entry point
 */
int main(void) {
  struct RDArgs *rdArgs = NULL;
  struct Arguments args = { NULL, FALSE, NULL };
  char volumeName[64];
  char cleanName[108];
  char foundDevice[108];
  STRPTR driverName;
  int status;
  int returnCode = RC_ERROR;
  LONG unitNum;

  /* Parse command line arguments */
  rdArgs = ReadArgs(TEMPLATE, (LONG *)&args, NULL);
  if (!rdArgs) {
    Printf("Usage: CheckDosDevice <DEVICE> [QUIET] [<DRIVER> driver name]\n");
    Printf("  DEVICE - DOS device name or unit number\n");
    Printf("  QUIET  - Suppress output\n");
    Printf("  DRIVER - Device driver name (default: diskimage.device)\n");
    Printf("\nExamples:\n");
    Printf("  CheckDosDevice IHD101\n");
    Printf("  CheckDosDevice 101 (finds device with unit 101)\n");
    Printf("  CheckDosDevice 0 DRIVER trackdisk.device\n");
    Printf("  CheckDosDevice DF0:\n");
    return RC_ERROR;
  }

  /* Set global quiet flag */
  g_quiet = args.quiet ? TRUE : FALSE;

  /* Get driver name (default to diskimage.device) */
  driverName = args.driver ? args.driver : (STRPTR)"diskimage.device";

  /* First check if the device driver is available */
  if (!CheckDeviceDriver(driverName)) {
    OPrintf("Device driver %s not available\n", driverName);
    FreeArgs(rdArgs);
    return RC_FAIL;
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
      FreeArgs(rdArgs);
      return RC_ERROR;
    }
  }
  else {
    /* Normal device name */
    StripDeviceName(args.device, cleanName, sizeof(cleanName));
  }

  /* Check device status */
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

  /* Free the ReadArgs structure */
  FreeArgs(rdArgs);

  return returnCode;
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
