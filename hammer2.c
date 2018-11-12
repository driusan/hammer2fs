#include <u.h>
#include <libc.h>
#include <stdio.h>
#include <fcall.h>
#include <thread.h>
#include <9p.h>

#include "uuid.h"
#include "hammer2_disk.h"
#include "hammer2.h"
#include "9phammer.h"

// Loads an inode from dataoff in fd into the inode argument.
void loadinode(hammer2_blockref_t *block, hammer2_inode_data_t *inode) {
	char *err = loadblock(block, inode, sizeof(hammer2_inode_data_t), nil);
	if(err != nil) {
		fprint(2, "error loading inode: %s\n", err);
	}
};

void dumpinode(hammer2_inode_data_t *i) {
	if (i == nil) {
		fprintf(stderr, "No inode\n");
		return;
	}
	fprintf(stderr, "Inode: %d\nSize: %d\n", i->meta.inum, i->meta.size);
	fprintf(stderr, "Type: %d\n", i->meta.type);
	fprintf(stderr, "Name_key: %d\n", i->meta.name_key);
	fprintf(stderr, "OpFlags: %d\n", i->meta.op_flags);
	fprintf(stderr, "CapFlags: %d\n", i->meta.cap_flags);
	fprintf(stderr, "NLinks: %d\n", i->meta.nlinks);
	fprintf(stderr, "pfs_inum: %d: %d\n", i->meta.pfs_inum);
	fprintf(stderr, "Comp_Algo: %d\n", i->meta.comp_algo);
}
void dumpblock(hammer2_blockref_t *b) {
	if (b == nil) {
		fprintf(stderr, "No blockref\n");
		return;
	}
	fprintf(stderr, "Block type: %d\n", b->type);
	fprintf(stderr, "Methods: %d (Check: %d Comp: %d)\n", b->methods, HAMMER2_DEC_CHECK(b->methods), HAMMER2_DEC_COMP(b->methods));
	fprintf(stderr, "Keybits: %d\n", b->keybits);
	fprintf(stderr, "vradix: %d\n", b->vradix);
	fprintf(stderr, "flags: %d\n", b->flags);
	fprintf(stderr, "leaf_count: %d\n", b->leaf_count);
	fprintf(stderr, "key: %d\n", b->key);
	fprintf(stderr, "key: %d\n", b->key);
	fprintf(stderr, "(if applicable) data_count %d inode_count: %d\n", b->embed.stats.data_count, b->embed.stats.inode_count);
}

void readvolume(int fd, hammer2_dev_t *hd) {
	hammer2_volume_data_t vol;
	int valid;
	int i;
	valid = 0;
	for(i = 0; i < 4; i++){
		seek(fd, i*HAMMER2_ZONE_BYTES64, SEEK_SET);
		read(fd, &vol, sizeof(hammer2_volume_data_t));
		if(vol.magic != HAMMER2_VOLUME_ID_HBO) continue;

		// printf("good magic");
		// printf(" Total size: %u Free: %u\n", vol.allocator_size, vol.allocator_free);
		// printf("Mirror_tid: %16d\n", vol.mirror_tid);
		// FIXME: Check crcs here
		if(valid == 0 || hd->voldata.mirror_tid < vol.mirror_tid){
			valid = 1;
			hd->voldata = vol;
			hd->volhdrno = i;
		}
	}
	print("Using volume header #%d\n", hd->volhdrno);
	print("Volume size: %ulld\n", hd->voldata.allocator_size);
	print("Freemap version: %d\n", hd->voldata.freemap_version);
	print("Allocator size: %ulld\n", hd->voldata.volu_size);
	print("Allocator free: %ulld\n", hd->voldata.allocator_free);
	print("Allocator beg: %ulld\n", hd->voldata.allocator_beg);

	/*
	For looking into the freemap on start/debugging
	char buf[HAMMER2_BLOCKREF_LEAF_MAX];
	int j = 0;
	for(i = 0; i < 4; i++) {
		hammer2_blockref_t *block =&(hd->voldata.freemap_blockset.blockref[i]);
		print("Freemap 0.%d Type %d\n", i, block->type);
		print("Key: %ulld keysize: %d\n", block->key, 1<<block->keybits);
		assert(HAMMER2_DEC_COMP(block->methods) == 0);
		pread(fd, buf, HAMMER2_BLOCKREF_LEAF_MAX, block->data_off & HAMMER2_OFF_MASK);
		for(j = 0; j < 1<<block->keybits;j++) {
			block = ((hammer2_blockref_t*) buf) + j;
			print("Freemap 1 Level: %d.%d Type %d\n", i, j, block->type);
			print("Key: %ulld keysize: %uld\n", block->key, 1<<block->keybits);
			print("Available: %ulld Radixes: %ulb\n", block->check.freemap.avail, block->check.freemap.bigmask);
		}
	}
	//print("mirror_tid: %x freemap tid: %8.x bulkfree tid: %x\n", hd->voldata.mirror_tid, hd->voldata.freemap_tid, hd->voldata.bulkfree_tid);
	*/
}

