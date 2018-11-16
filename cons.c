#include <u.h>
#include <libc.h>
#include <thread.h>
#include <bio.h>

#include "uuid.h"
#include "hammer2_disk.h"

extern hammer2_volume_data_t volumehdr;

// This is mostly adapted from hjfs.
enum {MAXARGS = 16};

typedef struct Cmd Cmd;
struct Cmd {
	char *name;
	int nargs;
	void (*f)(int, char**);
};

void printfriendly(uvlong x) {
	const uvlong tb = 1024ULL*1024ULL*1024ULL*1024ULL;
	const uvlong gb = 1024ULL*1024ULL*1024ULL;
	const uvlong mb = 1024ULL*1024ULL;
	const uvlong kb = 1024ULL;
	if (x >= 10000*gb)
		print("%ulldTB", x / tb);
	else if(x >= 10000*mb)
		print("%ulldGB", x / gb);
	else if(x >= 10000*kb)
		print("%ulldMB", x / mb);
	else if(x >= 1024*10000)
		print("%ulldKB", x / 1024);
	else
		print("%ulld bytes", x);
}
void cmddf(int, char**) {
	print("Size\tUsed\tAvail\tCapacity\n");
	printfriendly(volumehdr.volu_size);
	print("\t");
	printfriendly(volumehdr.allocator_size-volumehdr.allocator_free);
	print("\t");
	printfriendly(volumehdr.allocator_free);
	print("\t");
	print("%ulld%%\n", 100-(volumehdr.allocator_free*100/volumehdr.allocator_size));
}
void cmdhelp(int, char**) {
	print("Command\tDescription\n");
	print("df\tShow free disk space\n");
	print("help\tThis message\n");
}

Cmd cmds[] = {
	{ "df", 0, cmddf},
	{ "help", 0, cmdhelp},
};

static Biobuf bio;
static void consproc(void *v){
	Biobuf *in;
	char *args[MAXARGS];
	char *s;
	Cmd *c;
	int rc;

	in = (Biobuf*) v;
	for(;;){
		s = Brdstr(in, '\n', 1);
		if(s == nil)
			continue;
		rc = tokenize(s, args, MAXARGS);
		if(rc == 0) {
			goto bad;
		}
		for(c = cmds; c < cmds + nelem(cmds); c++) {
			if(strcmp(c->name, args[0]) == 0) {
				if (c->nargs != rc-1)
					goto bad;
				c->f(rc, args);
				goto good;
			}
		}

	bad:
		print("bad command\n");
	good:
		free(s);
	}	

}

void initcons(char *service){
	int fd, pfd[2];
	char buf[512];
	snprint(buf, sizeof(buf), "/srv/%s.cmd", service);
	fd = create(buf, OWRITE|ORCLOSE, 0600);
	if(fd < 0)
		return;
	pipe(pfd);
	fprint(fd, "%d", pfd[1]);
	Binit(&bio, pfd[0], OREAD);


	procrfork(consproc, &bio, mainstacksize,0);
}
