/*
 * label - read disklabel 64
 */
#include <u.h>
#include <libc.h>
#include <bio.h>
#include <disk.h>
#include "edit.h"
#include "disklabel64.h"

enum {
	Maxpath = 128,
};

static int	blank;
static int	file;
static int	doautox;
static int	printflag;
static Part	**opart;
static int	nopart;
static char	*osecbuf;
static char	*secbuf;
static int	rdonly;
static int	dowrite;
static int	docache;
static int	donvram;

static Part	*mkpart(char*, vlong, vlong, int);
static void	rdpart(Edit*);

static void 	cmdsum(Edit*, Part*, vlong, vlong);
static char 	*cmdokname(Edit*, char*);
static char	*cmdctlprint(Edit*, int, char**);

char *okname(Edit *edit, char*name);

Edit edit = {
	.okname=cmdokname,
	.sum=	cmdsum,

	.unit=	"sector",
};


#define TB (1024LL*GB)
#define GB (1024*1024*1024)
#define MB (1024*1024)
#define KB (1024)

void
usage(void)
{
	fprint(2, "usage: disk/label [-s sectorsize] /dev/sdC0/bsd386\n");
	exits("usage");
}

void
main(int argc, char **argv)
{
	int i;
	Disk *disk;
	vlong secsize;

	secsize = 0;
	printflag++;
	rdonly++;
	ARGBEGIN{
	case 'f':
		file++;
		break;
	case 's':
		secsize = atoi(ARGF());
		break;
	default:
		usage();
	}ARGEND;

	if(argc != 1)
		usage();

	disk = opendisk(argv[0], rdonly, file);
	if(disk == nil)
		sysfatal("cannot open disk: %r");

	if(secsize != 0) {
		disk->secsize = secsize;
		disk->secs = disk->size / secsize;
	}
	edit.unitsz = disk->secsize;
	edit.end = disk->secs;

	secbuf = emalloc(disk->secsize+1);
	osecbuf = emalloc(disk->secsize+1);
	edit.disk = disk;

	if(blank == 0)
		rdpart(&edit);

	opart = emalloc(edit.npart*sizeof(opart[0]));

	/* save old partition table */
	for(i=0; i<edit.npart; i++)
		opart[i] = edit.part[i];
	nopart = edit.npart;

	if(printflag) {
		runcmd(&edit, "P");
		exits(0);
	}

	if(dowrite) {
		runcmd(&edit, "w");
		exits(0);
	}

	runcmd(&edit, "p");
	for(;;) {
		fprint(2, ">>> ");
		runcmd(&edit, getline(&edit));
	}
}

static void
cmdsum(Edit *edit, Part *p, vlong a, vlong b)
{
	vlong sz, div;
	char *suf, *name;
	char c;

	c = p && p->changed ? '\'' : ' ';
	name = p ? p->name : "empty";

	sz = (b-a)*edit->disk->secsize;
	if(sz >= 1*TB){
		suf = "TB";
		div = TB;
	}else if(sz >= 1*GB){
		suf = "GB";
		div = GB;
	}else if(sz >= 1*MB){
		suf = "MB";
		div = MB;
	}else if(sz >= 1*KB){
		suf = "KB";
		div = KB;
	}else{
		if (sz < 0)
			fprint(2, "%s: negative size!\n", argv0);
		suf = "B ";
		div = 1;
	}

	if(div == 1)
		print("%c %-12s %*lld %-*lld (%lld sectors, %lld %s)\n", c, name,
			edit->disk->width, a, edit->disk->width, b, b-a, sz, suf);
	else
		print("%c %-12s %*lld %-*lld (%lld sectors, %lld.%.2d %s)\n", c, name,
			edit->disk->width, a, edit->disk->width, b, b-a,
			sz/div, (int)(((sz%div)*100)/div), suf);
}

static char isfrog[256]={
	/*NUL*/	1, 1, 1, 1, 1, 1, 1, 1,
	/*BKS*/	1, 1, 1, 1, 1, 1, 1, 1,
	/*DLE*/	1, 1, 1, 1, 1, 1, 1, 1,
	/*CAN*/	1, 1, 1, 1, 1, 1, 1, 1,
	[' ']	1,
	['/']	1,
	[0x7f]	1,
};

static char*
cmdokname(Edit*, char *elem)
{
	for(; *elem; elem++)
		if(isfrog[*(uchar*)elem])
			return "bad character in name";
	return nil;
}

static Part*
mkpart(char *name, vlong start, vlong end, int changed)
{
	Part *p;

	p = emalloc(sizeof(*p));
	p->name = estrdup(name);
	p->ctlname = estrdup(name);
	p->start = start;
	p->end = end;
	p->changed = changed;
	return p;
}

static char*
rdbsdpart(Edit *edit);

/* disklabel partition is first sector of the partition */
static void
rdpart(Edit *edit)
{
	int i, nline, nf, waserr;
	vlong a, b;
	char *line[128];
	char *f[5];
	char *err;
	Disk *disk;

	disk = edit->disk;
	seek(disk->fd, disk->secsize, 0);
	if(readn(disk->fd, osecbuf, disk->secsize) != disk->secsize)
		return;
	osecbuf[disk->secsize] = '\0';
	memmove(secbuf, osecbuf, disk->secsize+1);

	if(strncmp(secbuf, "part", 4) != 0){
		err = rdbsdpart(edit);
		if(err != nil){
			fprint(2, "no plan9 partition table found\n");
			exits(err);
		}
		return;
	}

	waserr = 0;
	nline = getfields(secbuf, line, nelem(line), 1, "\n");
	for(i=0; i<nline; i++){
		if(strncmp(line[i], "part", 4) != 0) {
		Error:
			if(waserr == 0)
				fprint(2, "syntax error reading partition\n");
			waserr = 1;
			continue;
		}

		nf = getfields(line[i], f, nelem(f), 1, " \t\r");
		if(nf != 4 || strcmp(f[0], "part") != 0)
			goto Error;

		a = strtoll(f[2], 0, 0);
		b = strtoll(f[3], 0, 0);
		if(a >= b)
			goto Error;

		if(err = addpart(edit, mkpart(f[1], a, b, 0))) {
			fprint(2, "?%s: not continuing\n", err);
			exits("partition");
		}

	}
}

static char*
rdbsdpart(Edit *edit)
{
	int i, j;
	char *name, *nname;
	Disk *disk;
	char *err;
	int start, end;
	struct disklabel64 dl;
	disk = edit->disk;

	seek(disk->fd, 0, 0);

	readn(disk->fd, (void *)&dl, sizeof(struct disklabel64));
	if(dl.d_magic != DISKMAGIC64)
		return "bad magic";

	for(i = 0; i < dl.d_npartitions; i++){
		switch(dl.d_partitions[i].p_fstype){
		case FS_UNUSED: continue;
		case FS_SWAP:
			name = "bsdswap";
			break;
		case FS_UFS:
			name = "ufs";
			break;
		case FS_HAMMER:
			name = "hammer";
			break;
		case FS_HAMMER2:
			name = "hammer2";
			break;
		case FS_ZFS:
			name = "zfs";
			break;
		case FS_OTHER:
		default:
			name = "other";
			break;
		}

		nname = name;
		for(j = 0; j < 16; j++){
			if(!okname(edit, nname)) break;
			
			sprint(nname, "%s.%d", name,j+1);
		}
		start = dl.d_partitions[i].p_boffset / disk->secsize;
		end = start + (dl.d_partitions[i].p_bsize / disk->secsize);
		if(err = addpart(edit, mkpart(nname, start, end, 0))){
			fprint(2, "?%s: not continuing\n", err);
			return err;
		}
	}
	return nil;
}


static vlong
min(vlong a, vlong b)
{
	if(a < b)
		return a;
	return b;
}

