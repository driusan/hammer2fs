#include <u.h>
#include <libc.h>
#include <fcall.h>
#include <thread.h>
#include <9p.h>

/* FIXME: This hack shouldn't be required. We allocate some big things on the
   stack in Srv, so we need a big stack. Srv should just queue and not
   need the stack, a different thread with a more appropriate stack size should be
   the one reading from the queue doing the heavy lifting instead of hacking up
   threadpostmountsrv. */
static void
tforker(void (*fn)(void*), void *arg, int rflag)
{
	procrfork(fn, arg, 512*1024, rflag);
}

void
mythreadpostmountsrv(Srv *s, char *name, char *mtpt, int flag)
{
	_forker = tforker;
	_postmountsrv(s, name, mtpt, flag);
}

