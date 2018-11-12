# hammer2 9p

This contains a 9p implementation of the hammer2 filesystem, to make
it possible to read the DragonFly filesystem on Plan 9.  (I've only
tried it on 9front, but as far as I know I didn't do anything 9front
specific.)

The util directory contains a hacked up version of disk/prep which
installs as disk/label.  disk/label reads BSD disklabel64 labels and
outputs in a similar fashion to disk/prep -p.  You can use it as
"disk/label /dev/sdE0/bsd386 > /dev/sdE0/ctl" to see your BSD
partitions as regular files under /dev/sdE0.  (Assuming you're using a
64-bit disklabel and not the 32-bit disklabel format and your disk
partition is named sdE0.)

hammer2fs reads a hammer2 partition (by default /dev/sdE0/hammer2) and
serves it over 9p.  It posts to /srv/hammer2.  It's currently
read-only and doesn't handle long filenames (>64 characters) and will
likely remain read-only for the foreseeable future.

lz4.^(c h) are a port of the basic lz4 library.  I mostly just removed
#ifdefs for other operating systems/compilers and changed the types to
be compatible with the Plan 9 compiler.  You should be able to just
copy them elsewhere and use them if you need lz4 (de)compression
support under Plan 9.

hammer2.h and hammer2_disk.h are similarly ported from DragonFly (and
mostly just have the types changed and packed pragma syntax changed.)
They contain the disk structures and documentation about the
filesystem.

