# CheckDosDevice - An Amiga CLI Utility

## Overview

I worked with Claude (Anthropic AI) to help me generate a utility in a time
sensitive fashion. I love the Amiga but I am not as familiar with OS based 
APIs as I'd like. 

This utility was born out of the needs I found when shell scripting around
diskimage.device. Its functionality was universal and helpful enough it was
expanded to work with any dos device (in theory). I have tested it with
trackdisk.device, scsi.device and uaehf.device, as well as its intended 
target diskimage.device.

It has a few modes of operation. Since it is intended to help with scripting,
its primary purpose is to help me identify which unit number of a dos driver
is available for use. The tool I pair this with is `MountHDF` which is part
of the diskimage.device suite of tools.

I can check the RC value of a call to `CheckDosDevice` to determine if:

 * A device is mounted and has a "disk" inserted (RC 0)
 * A device is mounted but has no "disk" inserted (RC 5 aka WARN)
 * A device is NOT mounted (RC 10 aka ERROR)
 * A device driver like diskimage.device is not present (RC 20 aka FAIL)

This can be used by AmigaDOS shell scripts like this

```sh
CheckDosDevice 100 Quiet
If Not Warn
  Echo "diskimage.device unit 100 present and mounted"
Else If Warn
  Echo "diskimage.device unit 100 present, no disk"
Else If Error
  Echo "diskimage.device unit 100 not mounted"
Else If Fail
  Echo "diskimage.device not present"
EndIf
```

## Learnings

As I worked through various revisions, I didn't want Claude to simply make
it for me. I want to learn too. So I had it generate a learnings.html file
that describes what it was doing and goes over the revisions I made with it.

View those here:
 * [View the learnings page](https://htmlpreview.github.io/?https://github.com/nyteshade/nea-check-dos-device/blob/main/learnings/learnings.html)

