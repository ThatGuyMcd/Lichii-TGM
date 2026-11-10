# Ocarina of Time Remake aspect Add-Ons

The built-in patch pipeline now supports these game-rendering profiles:

| Display ratio | Game picture | Typical use |
| --- | --- | --- |
| 4:3 | 1920x1440 | Classic display or compact handheld |
| 3:2 | 2160x1440 | Tablet or handheld |
| 16:10 | 2304x1440 | Gaming handheld or laptop |
| 16:9 | Original 2560x1440 | Standard display; no aspect patch |
| 2:1 | 2880x1440 | Wide tablet or phone display |
| 21:9 | Existing 3440x1440 | Ultrawide monitor; legacy profile retained |
| 32:10 | 4608x1440 | Dual 16:10 display |
| 32:9 | 5120x1440 | Super-ultrawide monitor |

Open the game's Properties, select Graphics and the desired Aspect Ratio before
launching it. Use Docked mode and leave that ratio's built-in entry enabled under
Add-Ons. Restart the game after changing the ratio. The 16:9 and Stretch choices
retain their existing behaviour. Choosing a ratio for other games changes their
display aspect only; these game patches apply exclusively to OOT's supported
build ID `E0F3CCFAF50065AE4EA01EB5AEB13B1A45DA3348`.

Fixed-size native DLSS and the legacy DLSS/FSR path are incompatible with these
profiles, as with the original 21:9 Add-On. Choose an AA/upscaling configuration
that does not require the original 16:9 targets. The Graphics tooltip explains
the docked, upscaling and restart requirements.

Each profile changes render targets, camera projection, UI canvas, speech markers,
HUD edge anchors and movie viewport together. Vertical field of view is retained:
narrow ratios show less at the sides, while wide ratios show more. Movies retain
their original 16:9 shape with side bars on wide screens and top/bottom bars on
narrow screens. User ExeFS and RomFS mods retain priority; conflicting original
bytes or occupied executable padding cause the affected built-in patch to skip.
Disable external aspect mods before using these profiles.

Narrow profiles retain the original texture heap allocation. Wide profiles grow
the texture and application heaps with pixel count, rather than borrowing memory
from other game heaps. 32:9 requires about 306 MiB more guest application memory
and renders twice the original pixel count, so GPU cost and host memory use rise.

The original camera/UI hooks derive from Ivan0xFF's
[oot-mod](https://github.com/craftycorvid/oot-mod), with heap sizing based on
[NX2navi](https://github.com/tangerineturtle1/NX2navi). TGM parameterizes the hooks
and relocates their executable padding to avoid Photo Mode's existing routine.
No game files or protected dependency/recompiled sources are modified.

Automated tests cover profiles, saved enum IDs, camera/UI/movie constants,
relocated branch destinations, disabled and conflicting patches, Photo Mode
coexistence, heap sizes and byte-preserving HUD archive edits. They do not certify
every scene or menu: in-game checks for menu clipping, HUD placement, movies and
Photo Mode remain necessary, especially at 4:3 and 32:9. The emulator is not
launched automatically during validation.
