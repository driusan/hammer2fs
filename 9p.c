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
#include "xxhash.h"
#include "faketypes.h"

char *filename;
int devfd;

static ulong *crctab;
void fileread(Req *r);

void loadinodes(inode i, DirEnts *dirents);
int verifycheck(hammer2_blockref_t *block, void *data);
char* loadblock(hammer2_blockref_t *block, void *dst, int dstsize, int *rsize);

root_t root;


typedef struct{
	hammer2_tid_t inum;
	hammer2_blockref_t block;
} FEntry;


u64int maxinodes;
FEntry *inodes;

Qid makeqid(hammer2_tid_t inum, uchar type) {
	Qid r = {
		.path inum,
		.type type,
	};
	return r;
}


// Gets a cached FEntry from the list, which must have already had its metadata
// populated with mkinode
FEntry* getfentry(Qid q) {
	if (q.path > maxinodes) {
		return nil;
	}
	return &inodes[q.path];
}

// Creates an FEntry for an inode and adds it to the cached list.
FEntry* mkinode(hammer2_tid_t inum, hammer2_blockref_t block){
	if (inum >= maxinodes) {
		// FIXME: Be more intelligent about reallocs. We realloc twice
		// as many to minimize the number of reallocs needed.
		inodes = realloc(inodes, (inum*2)*sizeof(FEntry));
		maxinodes = inum*2;
	}
	FEntry *dir = &inodes[inum];
	dir->inum = inum;
	dir->block = block;
	return dir;
}

void fsstart(Srv *) {
	int i, j;
	hammer2_dev_t hddev;
	hammer2_inode_data_t suproot;
	// FIXME: don't hardcode this;
	devfd = open(filename, OREAD);
	readvolume(devfd, &hddev);
	hammer2_volume_data_t vol = hddev.voldata;
	for(i =0; i < HAMMER2_SET_COUNT; i++) {
		if (vol.sroot_blockset.blockref[i].type == HAMMER2_BREF_TYPE_INODE) {
			loadinode(&vol.sroot_blockset.blockref[i], &suproot);
			
			if (suproot.meta.pfs_type == HAMMER2_PFSTYPE_SUPROOT) {
				// found the superroot, now found the mountable root
				for(j = 0; j < 4; j++) {
						if (suproot.u.blockset.blockref[j].type == HAMMER2_BREF_TYPE_INODE) {
							loadinode(&suproot.u.blockset.blockref[j], &root.inode);
							if (strcmp(root.pfsname, (char *)root.inode.filename) == 0) {
								root.Qid = makeqid(root.meta.inum, QTDIR);
								maxinodes = suproot.u.blockset.blockref[j].embed.stats.inode_count;
								inodes = emalloc9p(maxinodes*sizeof(FEntry));
								root.DirEnts.cap = 0;
								root.DirEnts.count = 0;
								mkinode(root.meta.inum, suproot.u.blockset.blockref[j]);
								loadinodes(root, &root.DirEnts);
								return;
							} 
						}
				}
			}
		}
	}
	sysfatal("Could not find root %s", root.pfsname);
}

typedef struct Aux Aux;
struct Aux {
	Qid;
	RWLock;
	// The currently walked to inode
	hammer2_inode_data_t *inode;

	// If loading an indirect block, the parent is an Aux structure
	// that caused this indirection.
	Aux *parent;

	// The start of this indirection layer
	hammer2_blockref_t *blocks;
	// The current offset and index into blocks
	int offset;
	int count;

	union {
		struct {
			uchar *lastbuf;
			vlong lastbufoffset;
			vlong lastbufcount;
		} file;

		DirEnts dir;
	} cache;
};

void fsattach(Req *r) {
	Aux *a;
	a = emalloc9p(sizeof(Aux));
	a->inode = &root.inode;
	a->parent = nil;
	a->blocks = &(root.u.blockset.blockref[0]);
	a->offset = 0;
	a->count = 4;

	a->cache.dir = root.DirEnts;

	a->Qid = root.Qid;
	r->fid->qid = root.Qid;
	r->ofcall.qid = root.Qid;
	r->fid->aux = a;
	respond(r, nil);
}

// Load all the inodes stored under i into the inodes array
void loadinodes(inode i, DirEnts *dirents) {
	Aux orig;
	Aux *cur;

	orig.parent = nil;
	orig.inode = &i;
	orig.blocks = &i.u.blockset.blockref[0];
	orig.offset = 0;
	orig.count = 4;
	cur = &orig;

start:
	while(cur->offset >= cur->count) {
		if (cur->parent == nil) {
			return;
		}
		Aux *tmp = cur;
		cur = cur->parent;
		free(tmp->blocks);
		free(tmp);
	}

	hammer2_blockref_t *block = &cur->blocks[cur->offset];

	// Things for indirect blocks.
	int radix;
	int dsize;
	int indblocks;
	Aux *tmp;
	char *err;
	switch (block->type) {
	case HAMMER2_BREF_TYPE_INODE: 
		mkinode(block->key, *block);
		cur->offset++;
		goto start;
	case HAMMER2_BREF_TYPE_EMPTY:
		cur->offset++;
		goto start;
	case HAMMER2_BREF_TYPE_DIRENT:
		if (dirents != nil) {
			if (dirents->cap == 0) {
				// Arbitrarily start with enough space for 16
				// directories.
				dirents->cap = 16;
				dirents->entry = emalloc9p(sizeof(hammer2_blockref_t)*16);
			} else if (dirents->cap == dirents->count) {
				dirents->cap *= 2;
				dirents->entry = erealloc9p(dirents->entry, dirents->cap*sizeof(hammer2_blockref_t));
			}
			dirents->entry[dirents->count++] = *block;
		}
		cur->offset++;
		goto start;
	case HAMMER2_BREF_TYPE_INDIRECT:
		radix = block->data_off & HAMMER2_OFF_MASK_RADIX;
		dsize = 1<<radix;
		indblocks = dsize / sizeof(hammer2_blockref_t);
		if (radix == 0) {
			sysfatal("No radix");
		}
		
		cur->offset++;
		tmp = cur;

		cur = emalloc9p(sizeof(Aux));

		cur->inode = &i;
		cur->parent = tmp;
		cur->blocks = emalloc9p(HAMMER2_PBUFSIZE);
		assert(cur->blocks != nil);
		cur->offset = 0;
		cur->count = indblocks;

		// Load the data from disk and start again at the new level
		// of indirection.
		err = loadblock(block, cur->blocks, dsize, nil);
		if (err != nil) {
			fprint(2, "loading err: %s\n", err);
		}	
		goto start;
	default:
		// For now assume if we got here something went wrong.
		printf("Type %d\n", block->type);
		sysfatal("Unhandled block type in directory");
	}
}

int cacheddirread(int n, Dir *dir, void *aux) {
	Aux *a = aux;
	if (n < 0 || n >= a->cache.dir.count) {
		return -1;
	}

	hammer2_blockref_t block = a->cache.dir.entry[n];

	dir->qid = makeqid(block.embed.dirent.inum, 0);
	FEntry *f = getfentry(dir->qid);
	if (f == nil) {
		sysfatal("could not load inode");
	}

	inode i;
	loadinode(&f->block, &i);
	switch (i.meta.type) {
		case HAMMER2_OBJTYPE_DIRECTORY:
			dir->qid.type = QTDIR;
			break;
		case HAMMER2_OBJTYPE_REGFILE:
		case HAMMER2_OBJTYPE_SOFTLINK:
			dir->qid.type = QTFILE;
			break;
		default:
			printf("type %d\n", i.meta.type);
			sysfatal("Unhandled OBJTYPE");
	}
	dir->mode = i.meta.mode;
	if (dir->qid.type == QTDIR){
		dir->mode |= DMDIR;
	}
	dir->atime = i.meta.atime / 1000000; // unsupported.
	dir->mtime = i.meta.mtime / 1000000; // hammer2 is nanoseconds, 9p is seconds
	dir->length = i.meta.size;
	if (block.embed.dirent.namlen <= 64)
		dir->name = estrdup9p(block.check.buf);
	else {
		// FIXME: Figure out how/where these are stored in hammer2
		sysfatal("Long file names not supported");
	}

	// FIXME: Get from uid/gid from inode and parse /etc/passwd (
	// 	or add an /adm/users?)
	dir->uid = estrdup9p(getuser());
	dir->gid = estrdup9p(getuser());
	return 0;
}

Qid loadsubdir(Aux *a, char *name) {
	int i;
	Qid r; 
	for(i=0; i < a->cache.dir.count; i++){
		hammer2_blockref_t block = a->cache.dir.entry[i];
		if (block.embed.dirent.namlen > 64) {
			sysfatal("Long file names not supported");
		}

		if (strcmp(name, block.check.buf) == 0) {
			inode in;
			FEntry *fe;
			r = makeqid(block.embed.dirent.inum, 0);
			fe = getfentry(r);
			if (fe == nil) {
				sysfatal("could not load inode");
			}

			loadinode(&fe->block, &in);
			switch (in.meta.type) {
			case HAMMER2_OBJTYPE_DIRECTORY:
				r.type = QTDIR;
				return r;
			case HAMMER2_OBJTYPE_REGFILE:
			case HAMMER2_OBJTYPE_SOFTLINK:
				r.type = QTFILE;
				return r;
			default:
				printf("%s type %d\n", name, in.meta.type);
				sysfatal("Unhandled OBJTYPE");
			}

		}
	}
	r.path = 0;
	r.vers = 0;
	r.type = 0;
	return r;
}

char* fswalk(Fid *fid, char *name, Qid *qid) {
	Aux *a;
	Qid q;
	a = fid->aux;
	if (strcmp(name, "/") == 0) {
		fid->qid = root.Qid;
		a->inode = &root.inode;
		a->parent = nil;
		a->blocks = &(root.u.blockset.blockref[0]);
		a->offset = 0;
		a->count = 4;
		a->cache.dir = root.DirEnts;
		*qid = root.Qid;
		return nil;
	} else if (strcmp(name, "..") == 0) {
		// The parent should always be a directory.
		q = makeqid(a->inode->meta.iparent, QTDIR);
	} else if (strcmp(name, ".") == 0) {
		*qid = a->Qid;
		fid->qid = a->Qid;
		return nil;
	} else {
		q = loadsubdir(a, name);
		if (q.path == 0) {
			return "not found";
		}
	}
	FEntry *fe = getfentry(q);
	a->inode = emalloc9p(sizeof(inode));
	loadinode(&fe->block, a->inode);

	a->parent = nil;
	a->blocks = &(a->inode->u.blockset.blockref[0]);
	a->offset = 0;
	a->count = 4;
	switch(q.type){
	case QTDIR:
		a->cache.dir.cap = 0;
		a->cache.dir.count = 0;
		a->cache.dir.entry = nil;
		loadinodes(*a->inode, &a->cache.dir);
		break;
	case QTFILE:
		a->cache.file.lastbuf = nil;
		a->cache.file.lastbufcount = 0;
		break;
	default:
		return "unhandled qid type";
	}

	fid->qid = q;
	*qid = q;
	return nil;
}

char* fswalkclone(Fid *old, Fid *new) {
	// FIXME: Fix memory leak by implementing destroy.
	Aux *oaux = old->aux;
	Aux *naux = emalloc9p(sizeof(Aux));
	new->aux = naux;
	memcpy(naux, oaux, sizeof(Aux));

	// Cache can be freed behind our back, so don't reuse it.
	int bsize;
	switch (old->qid.type){
	case QTDIR:
		// We don't leave extra capacity since we know how many entries
		// are in the directory and it's unlikely to change.
		bsize = oaux->cache.dir.count*sizeof(hammer2_blockref_t);
		naux->cache.dir.cap = oaux->cache.dir.count;
		naux->cache.dir.count = oaux->cache.dir.count;
		memcpy(naux->cache.dir.entry, oaux->cache.dir.entry, bsize);
		break;
	case QTFILE:
		naux->cache.file.lastbuf = nil;
		naux->cache.file.lastbufcount = 0;
		break;
	}
	return nil;
}

void fsopen(Req *r) {
	if (r->fid->qid.path == root.Qid.path) {
		Aux *a = r->fid->aux;
		a->cache.dir = root.DirEnts;
	}
	respond(r, nil);
}
	
void fsstat(Req *r) {
	if (r->fid->qid.path == root.Qid.path) {
		r->d.mode = DMREAD | DMEXEC | DMDIR;
		//r->d.name = estrdup9p(getuser());
		r->d.uid = estrdup9p(getuser());
		r->d.gid = estrdup9p(getuser());
		r->d.qid = r->fid->qid;
		
		respond(r, nil);
		return;
	}
	FEntry *f = getfentry(r->fid->qid);
	if (f == nil) {
		respond(r, "not found");
		return;
	}

	inode i;
	loadinode(&f->block, &i);
	r->d.qid = r->fid->qid;
	r->d.mode = i.meta.mode;
	if (r->fid->qid.type == QTDIR){
		r->d.mode |= DMDIR;
	}
	r->d.atime = i.meta.atime / 1000000;
	r->d.mtime = i.meta.mtime / 1000000;
	r->d.length = i.meta.size;
	r->d.uid = estrdup9p(getuser());
	r->d.gid = estrdup9p(getuser());

	respond(r, nil);
}

void fsread(Req *r) {
	switch(r->fid->qid.type) {
	case QTDIR:
		// Walking to the dir should have cached the dirents in
		// fid->aux.cache.dir
		dirread9p(r, cacheddirread, r->fid->aux);
		break;
	case QTFILE:
		fileread(r);
		return;
	default:
		printf("Type: %d\n", r->fid->qid.type);
		sysfatal("Unhandled QID type.");
	}
	respond(r, nil);
}

void fileread(Req *r) {
	// Copy aux so that we can mutate it without messing up other reads.
	Aux orig = *(Aux*)(r->fid->aux);
	Aux *cur;
	orig.parent = nil;
	orig.blocks = &orig.inode->u.blockset.blockref[0];
	orig.offset = 0;
	orig.count = 4;
	cur = &orig;

	// Reading at/past EOF.
	if (r->ifcall.offset >= orig.inode->meta.size) {
		r->ofcall.count = 0;
		respond(r, nil);
		return;
	}

	// If data is stored in the inode, don't bother getting anything from
	// disk.
	if (orig.inode->meta.op_flags & HAMMER2_OPFLAG_DIRECTDATA) {
		assert(orig.inode->meta.size > r->ifcall.offset);
		assert(r->ifcall.offset < HAMMER2_EMBEDDED_BYTES);
		int size = orig.inode->meta.size - r->ifcall.offset;
		memcpy(r->ofcall.data, (void *)&orig.inode->u.data[r->ifcall.offset], size);
		r->ofcall.count = size;
		respond(r, nil);
		return;
	}

	// Check if it's in the data read from the last read before doing anything
	Aux *a = r->fid->aux;
	rlock(a);
	if (a->cache.file.lastbufcount > 0 
		&& r->ifcall.offset >= a->cache.file.lastbufoffset 
		&& r->ifcall.offset < a->cache.file.lastbufcount + a->cache.file.lastbufoffset
	){
		// It's still cached from the last read, so just copy it from memory.
		int count = r->ifcall.count;
		int offstart = r->ifcall.offset - a->cache.file.lastbufoffset;
		if (count + r->ifcall.offset > orig.inode->meta.size) {
			// Ensure we don't send past EOF.
			count = orig.inode->meta.size - r->ifcall.offset;
		}
		if (count + offstart > a->cache.file.lastbufcount) {
			// We can only send as many bytes as are in lastbuf.
			count = a->cache.file.lastbufcount - offstart;
		}
		assert(count > 0);
		assert(offstart + count <= a->cache.file.lastbufcount);

		memcpy(r->ofcall.data, (uchar *)&a->cache.file.lastbuf[offstart],count);
		runlock(a);
		r->ofcall.count = count;
		respond(r, nil);
		return;
	}
	runlock(a);

	// Autozero is the address we report nils until if there's holes in the
	// file. It starts at the end of the file, and shrinks every time we
	// find a block who starts > offset and < current autozero. When the
	// loop finishes, we should have either found the block that covers
	// offset, or this should be the closest block to it that we've found,
	// which can be used to calculate the number of nils to send.
	int autozero = orig.inode->meta.size;

	// Now find the block that contains r->ifcall.offset.
start:
	while(cur->offset >= cur->count) {
		if (cur->parent == nil) {
			if(autozero > 0) {
				// An indirect block said it covered this range,
				// but there were no data blocks for it, so assume
				// nil.
				int count = r->ifcall.count;
				if(r->ifcall.offset + count > autozero){
					// handle the case where the hole was in
					// the middle.
					count = autozero - r->ifcall.offset;
				}

				if(r->ifcall.offset + count > orig.inode->meta.size){
					// make sure we don't add more nils than
					// exist in the file.
					count = orig.inode->meta.size - r->ifcall.offset;
				}

				memset(r->ofcall.data, 0, count);
				r->ofcall.count = count;
				respond(r, nil);
			} else {
				respond(r, "could not find data");
			}
			return;
		}
		Aux *tmp = cur;

		cur = tmp->parent;

		free(tmp->blocks);
		free(tmp);
	}

	hammer2_blockref_t *block = &cur->blocks[cur->offset];

	// the decompressed data
	uchar decompr[HAMMER2_BLOCKREF_LEAF_MAX*2];
	uchar *dp;
	int rlen;
	int nbytes;
	int doff;
	int keysize;
	int radix = block->data_off & HAMMER2_OFF_MASK_RADIX;
	int chksize = 1<<radix;
	int dsize;
	char *err;

	// Things for indirect blocks.
	int indblocks;
	Aux *tmp;

	switch (block->type) {
	case HAMMER2_BREF_TYPE_INDIRECT:
		dsize = chksize;
		
		keysize = (1<<block->keybits); 
		if (r->ifcall.offset < block->key || r->ifcall.offset >= block->key + keysize){
			cur->offset++;
			goto start;
		}
		indblocks = dsize / sizeof(hammer2_blockref_t);
		if (radix == 0) {
			sysfatal("No radix");
		}

		// We hijack aux for the indirect block and move the existing
		// one somewhere else, so that future calls
		// pick up at the same level of indirection once we've found
		// a DIRENT.
		cur->offset++;
		tmp = emalloc9p(sizeof(Aux));
		tmp->inode = cur->inode;
		tmp->parent = cur;
		tmp->blocks = emalloc9p(HAMMER2_PBUFSIZE);
		tmp->offset = 0;
		tmp->count = indblocks;
		tmp->parent = cur;
		tmp->cache.file.lastbufcount = 0;

		cur = tmp;
		// Load the data from disk and start again at the new level
		// of indirection.
		err = loadblock(block, cur->blocks, dsize, &nbytes);
		if (err != nil) {
			respond(r, err);
			goto cleanup;
		}
		goto start;
	case HAMMER2_BREF_TYPE_DATA:
		cur->offset++;
		dsize = 1<<block->keybits;
		if (r->ifcall.offset < block->key || r->ifcall.offset >= block->key + dsize) {
			if (block->key < autozero && block->key > r->ifcall.offset){
				// This block doesn't cover offset, but it's the
				// closest we've found so far.
				autozero = block->key;
			}
			goto start;
		}
		rlen = r->ifcall.count < dsize ? r->ifcall.count : dsize;
		if (rlen > cur->inode->meta.size) {
			rlen = cur->inode->meta.size;
		}
		
		assert(r->ifcall.offset >= block->key);
		assert(r->ifcall.offset < block->key + dsize);

		err = loadblock(block, decompr, HAMMER2_BLOCKREF_LEAF_MAX*2, &nbytes);
		if (err != nil) {
			respond(r, err);
			goto cleanup;
		}
		dp = &decompr[0];

		// Now handle the actual return;
		assert(rlen > 0 && rlen <= 8192);
		if (r->ifcall.offset + rlen > orig.inode->meta.size) {
			rlen = orig.inode->meta.size - r->ifcall.offset;
		}
		if (nbytes < rlen) {
			assert(nbytes > 0);
			rlen = nbytes;
		}
		if(r->ifcall.offset + rlen > block->key + dsize){
			// it spans multiple blocks, so only send the part
			// that's in this block in this read.
			rlen = block->key + dsize - r->ifcall.offset;
		}
		assert(r->ifcall.offset + rlen <= orig.inode->meta.size);
		doff = r->ifcall.offset-block->key;
		assert(rlen > 0);
		memcpy(r->ofcall.data, (uchar *)&dp[doff], rlen);
		assert(nbytes > 0);
		assert(nbytes >= rlen);
		r->ofcall.count = rlen;

		respond(r, nil);

		// Free the temp buffers we created, and cache the data in
		// lastbuf for the next calls to read now that we've sent
		// the response.

		// Store the data read from disk in aux for future reads to
		// bypass.
		wlock(a);

		free(a->cache.file.lastbuf);
		a->cache.file.lastbuf = emalloc9p(nbytes);
		assert(a->cache.file.lastbuf != nil);
		memcpy(a->cache.file.lastbuf, (uchar *)&dp[0], nbytes);
		a->cache.file.lastbufoffset = block->key;
		a->cache.file.lastbufcount = nbytes;
		wunlock(a);
		goto cleanup;
	case HAMMER2_BREF_TYPE_EMPTY:
		cur->offset++;
		goto start;
	default:
		printf("Type %d\n", block->type);
		sysfatal("Unhandled type");
	}

cleanup:
	// Ensure there's no Aux memory leaks.
	while(cur->parent != nil) {
		Aux *tmp = cur;
		cur = cur->parent;
		free(tmp->blocks);
		free(tmp);
	}
	return;
}

int verifycheck(hammer2_blockref_t *block, void *data) {
	int radix = block->data_off & HAMMER2_OFF_MASK_RADIX;
	int size = 1<<radix;
	int r = 0;
	switch (HAMMER2_DEC_CHECK(block->methods)){
	case HAMMER2_CHECK_NONE:
	case HAMMER2_CHECK_DISABLED:
		r = 1;
		break;
	case HAMMER2_CHECK_ISCSI32:
		if(crctab == nil)
			crctab = mkcrctab(0x82f63b78);
		r = (blockcrc(crctab, 0, data, size) == block->check.iscsi32.value);
		break;
	case HAMMER2_CHECK_SHA192:
		print("check sha192\n");
		// FIXME: Implement SHA192.
		r = 0;
		break;
	case HAMMER2_CHECK_XXHASH64:
		r = (XXH64(data, size, XXH_HAMMER2_SEED) == block->check.xxhash64.value);
		break;
	case HAMMER2_CHECK_FREEMAP:
		// we shouldn't encounter a freemap while reading a file..
		assert(0);
		break;
	default:
		r = 0;
	}
	return r;
}

/* Loads the block pointed to at data_off into dst (after decompression), and
 stores the size in rsize.
 Returns an error string if smething went wrong.
 Also validates check code */
char* loadblock(hammer2_blockref_t *block, void *dst, int dstsize, int *rsize) {
	uchar blockdata[HAMMER2_BLOCKREF_LEAF_MAX+1];
	int dsize = 1<<(block->data_off & HAMMER2_OFF_MASK_RADIX);
	int off = block->data_off & HAMMER2_OFF_MASK_LO;
	int csize;
	int decsize;

	pread(devfd, blockdata, HAMMER2_BLOCKREF_LEAF_MAX+1, block->data_off & HAMMER2_OFF_MASK_HI);
	if (!verifycheck(block, &blockdata[off])) {
		return "invalid checksum";
	}
	switch (HAMMER2_DEC_COMP(block->methods)){
	case HAMMER2_COMP_AUTOZERO:
	case HAMMER2_COMP_NONE:
		if (rsize != nil)
			*rsize = dsize;
		memcpy(dst, &blockdata[off] , dsize); 
		break;
	case HAMMER2_COMP_LZ4:
		csize = *(int*) &blockdata[off];
		decsize = LZ4_decompress_safe((char *)blockdata+off+4, (char *)dst, csize, dstsize);
		if (decsize < 0)
			return "bad read";
		if (rsize != nil)
			*rsize = decsize;
		break;
	case HAMMER2_COMP_ZLIB:
		inflateinit();
		decsize = inflatezlibblock(
			dst, dstsize,
			&blockdata[off], HAMMER2_BLOCKREF_LEAF_MAX-off
		);
		if (decsize < 0){
			return flateerr(*rsize);
		}
		if (rsize != nil)
			*rsize = decsize;
		break;
	default:
		printf("Comp: %d\n", HAMMER2_DEC_COMP(block->methods));
		return "Unhandled compression";
	}
	return nil;
}

