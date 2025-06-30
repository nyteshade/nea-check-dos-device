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

I have also instructed Claude to create a prompt to generate similar learnings
documents in the future. That is found in `LEARNINGS.prompt.md`. It also gave
me these instructions:

### Additional Context to Provide:

When using this prompt, also provide:

 1. **The actual source code** of the project
 2. **Target audience background** (e.g., "C programmers learning Rust")
 3. **Specific concepts to emphasize** based on what was challenging
 4. **Any error messages or problems encountered** during development
 5. **The evolution of the code** if available (different versions)

### Key Success Factors:

The best learning documents:
 - **Show real problems and solutions** from actual development
 - **Explain foreign concepts** by relating to familiar ones
 - **Include mistakes and fixes** to help others avoid them
 - **Provide working examples** that can be adapted
 - **Document the "why"** not just the "how"

This approach creates documents that are genuinely helpful because they're based on real experience rather than theoretical knowledge. The progressive disclosure and problem-solution format makes complex topics approachable.
