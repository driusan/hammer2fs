</$objtype/mkfile

TARG=hammer2fs

OFILES=hammer2.$O \
	lz4.$O \
	9p.$O \
	xxhash.$O \
	cons.$O \
	thread.$O

lz4.$O:
	$CC -FTV lz4.c

xxhash.$O:
	$CC -FTVwp xxhash.c

9p.$O:
	$CC -FTVwp 9p.c


BIN=/$objtype/bin
</sys/src/cmd/mkmany

