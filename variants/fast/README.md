# STRING-fast

The same STRING built with `-DKS_FAST`: `karplus.c` adds a second copy of
the per-sample loop in `.fast`, which digihealth's FAST AUDIO copies to
on-chip SRAM. Each block uses that copy only while digihealth reports the
copies made and checked (`r_on`); otherwise it runs the normal one.

`mod.json` here mirrors the top-level one (same sites, machine and
version) with its own id, the extra flag and the digihealth requirement.
Keep the two in step when either changes.

Build it from the elekloader checkout:

```sh
python3 -m elekloader.sdk.build /path/to/digistring/variants/fast --stock $STOCK
```
