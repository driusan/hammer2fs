#include <u.h>
#include <libc.h>
#include <stdio.h>

#include "uuid.h"
#include "hammer2_disk.h"
#include "hammer2.h"

// Loads an inode from dataoff in fd into the inode argument.
void loadinode(int fd, hammer2_off_t dataoff, hammer2_inode_data_t *inode) {
	char buf[HAMMER2_BLOCKREF_LEAF_MAX];
	hammer2_inode_data_t *inodebuf;

	pread(fd, buf, HAMMER2_BLOCKREF_LEAF_MAX, dataoff & HAMMER2_OFF_MASK_HI);
	inodebuf = (hammer2_inode_data_t*) &buf[dataoff & HAMMER2_OFF_MASK_LO];
	//printf("read inode %d\n", inodebuf->meta.inum);
	memmove(inode, inodebuf, sizeof(hammer2_inode_data_t));
};

void dumpinode(hammer2_inode_data_t *i) {
	if (i == nil) {
		fprintf(stderr, "No inode\n");
		return;
	}
	fprintf(stderr, "Inode: %d\nSize: %d\n", i->meta.inum, i->meta.size);
	fprintf(stderr, "Type: %d\n", i->meta.type);
	fprintf(stderr, "OpFlags: %d\n", i->meta.op_flags);
	fprintf(stderr, "CapFlags: %d\n", i->meta.cap_flags);
	fprintf(stderr, "NLinks: %d\n", i->meta.nlinks);
	fprintf(stderr, "Comp_Algo: %d\n", i->meta.comp_algo);
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

