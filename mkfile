</$objtype/mkfile

TARG=hammer2fs

OFILES=hammer2.$O \
	lz4.$O \
	9p.$O

lz4.$O:
	$CC -FTV lz4.c

BIN=/$objtype/bin
</sys/src/cmd/mkmany
