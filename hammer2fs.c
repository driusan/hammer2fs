#include <u.h>
#include <libc.h>
#include <stdio.h>
#include <fcall.h>
#include <flate.h>
#include <thread.h>
#include <9p.h>

#include "uuid.h"
#include "hammer2_disk.h"
#include "hammer2.h"
#include "9phammer.h"
#include "lz4.h"

extern char *filename;
extern root_t root;
extern int devfd;

void mythreadpostmountsrv(Srv *s, char *name, char *mtpt, int flag);


static Srv fs = {
	.open = fsopen,
	.attach = fsattach,
	.start = fsstart,
	.read = fsread,
	.walk1 = fswalk,
	.clone = fswalkclone,
	.stat = fsstat,
};

void usage(void) {
	fprint(2, "usage: %s [-r root] [-S srvname] [-f devicename]\n", argv0);
}

void threadmain(int argc, char *argv[])
{
	char *srvname = "hammer2";

	ARGBEGIN{
	case 'D':
		chatty9p++;
		break;
	case 'S':
		srvname = EARGF(usage());
		break;
	case 'f':
		filename = strdup(EARGF(usage()));
		break;
	case 'r':
		root.pfsname = EARGF(usage());
		break;
	default:
		usage();
	}ARGEND;

	if (argc > 0)
		usage();
	if (filename == nil) {
		filename = "/dev/sdE0/hammer2";
	}
	if (root.pfsname == nil) {
		root.pfsname = "ROOT";
	}
	initcons(srvname);
	mythreadpostmountsrv(&fs, srvname, nil, 0);
	threadexits(nil);
}

