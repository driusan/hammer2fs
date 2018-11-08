/*
 * Copyright (c) 2011-2017 The DragonFly Project.  All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Matthew Dillon <dillon@dragonflybsd.org>
 * by Venkatesh Srinivas <vsrinivas@dragonflybsd.org>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 * 3. Neither the name of The DragonFly Project nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific, prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * HAMMER2 IN-MEMORY CACHE OF MEDIA STRUCTURES
 *
 * This header file contains structures used internally by the HAMMER2
 * implementation.  See hammer2_disk.h for on-disk structures.
 *
 * There is an in-memory representation of all on-media data structure.
 * Almost everything is represented by a hammer2_chain structure in-memory.
 * Other higher-level structures typically map to chains.
 *
 * A great deal of data is accessed simply via its buffer cache buffer,
 * which is mapped for the duration of the chain's lock.  Hammer2 must
 * implement its own buffer cache layer on top of the system layer to
 * allow for different threads to lock different sub-block-sized buffers.
 *
 * When modifications are made to a chain a new filesystem block must be
 * allocated.  Multiple modifications do not typically allocate new blocks
 * until the current block has been flushed.  Flushes do not block the
 * front-end unless the front-end operation crosses the current inode being
 * flushed.
 *
 * The in-memory representation may remain cached (for example in order to
 * placemark clustering locks) even after the related data has been
 * detached.
 */

/*
 * The xid tracks internal transactional updates.
 *
 * XXX fix-me, really needs to be 64-bits
 */

#define HAMMER2_XID_MIN			0x00000000U
#define HAMMER2_XID_MAX			0x7FFFFFFFU

#define HAMMER2_LIMIT_DIRTY_CHAINS	(65536)

typedef struct hammer2_io hammer2_io_t;

#define HAMMER2_DIO_INPROG	0x8000000000000000LLU	/* bio in progress */
#define HAMMER2_DIO_GOOD	0x4000000000000000LLU	/* dio->bp is stable */
#define HAMMER2_DIO_WAITING	0x2000000000000000LLU	/* wait on INPROG */
#define HAMMER2_DIO_DIRTY	0x1000000000000000LLU	/* flush last drop */

#define HAMMER2_DIO_MASK	0x00FFFFFFFFFFFFFFLLU

/*
 * Special notes on flags:
 *
 * INITIAL	- This flag allows a chain to be created and for storage to
 *		  be allocated without having to immediately instantiate the
 *		  related buffer.  The data is assumed to be all-zeros.  It
 *		  is primarily used for indirect blocks.
 *
 * MODIFIED	- The chain's media data has been modified.  Prevents chain
 *		  free on lastdrop if still in the topology.
 *
 * UPDATE	- Chain might not be modified but parent blocktable needs
 *		  an update.  Prevents chain free on lastdrop if still in
 *		  the topology.
 *
 * FICTITIOUS	- Faked chain as a placeholder for an error condition.  This
 *		  chain is unsuitable for I/O.
 *
 * BMAPPED	- Indicates that the chain is present in the parent blockmap.
 *
 * BMAPUPD	- Indicates that the chain is present but needs to be updated
 *		  in the parent blockmap.
 */
#define HAMMER2_CHAIN_MODIFIED		0x00000001	/* dirty chain data */
#define HAMMER2_CHAIN_ALLOCATED		0x00000002	/* kmalloc'd chain */
#define HAMMER2_CHAIN_DESTROY		0x00000004
#define HAMMER2_CHAIN_DEDUPABLE		0x00000008	/* registered w/dedup */
#define HAMMER2_CHAIN_DELETED		0x00000010	/* deleted chain */
#define HAMMER2_CHAIN_INITIAL		0x00000020	/* initial create */
#define HAMMER2_CHAIN_UPDATE		0x00000040	/* need parent update */
#define HAMMER2_CHAIN_DEFERRED		0x00000080	/* flush depth defer */
#define HAMMER2_CHAIN_TESTEDGOOD	0x00000100	/* crc tested good */
#define HAMMER2_CHAIN_ONFLUSH		0x00000200	/* on a flush list */
#define HAMMER2_CHAIN_FICTITIOUS	0x00000400	/* unsuitable for I/O */
#define HAMMER2_CHAIN_VOLUMESYNC	0x00000800	/* needs volume sync */
#define HAMMER2_CHAIN_DELAYED		0x00001000	/* delayed flush */
#define HAMMER2_CHAIN_COUNTEDBREFS	0x00002000	/* block table stats */
#define HAMMER2_CHAIN_ONRBTREE		0x00004000	/* on parent RB tree */
#define HAMMER2_CHAIN_ONLRU		0x00008000	/* on LRU list */
#define HAMMER2_CHAIN_EMBEDDED		0x00010000	/* embedded data */
#define HAMMER2_CHAIN_RELEASE		0x00020000	/* don't keep around */
#define HAMMER2_CHAIN_BMAPPED		0x00040000	/* present in blkmap */
#define HAMMER2_CHAIN_BMAPUPD		0x00080000	/* +needs updating */
#define HAMMER2_CHAIN_IOINPROG		0x00100000	/* I/O interlock */
#define HAMMER2_CHAIN_IOSIGNAL		0x00200000	/* I/O interlock */
#define HAMMER2_CHAIN_PFSBOUNDARY	0x00400000	/* super->pfs inode */
#define HAMMER2_CHAIN_HINT_LEAF_COUNT	0x00800000	/* redo leaf count */

#define HAMMER2_CHAIN_FLUSH_MASK	(HAMMER2_CHAIN_MODIFIED |	\
					 HAMMER2_CHAIN_UPDATE |		\
					 HAMMER2_CHAIN_ONFLUSH |	\
					 HAMMER2_CHAIN_DESTROY)

/*
 * Hammer2 error codes, used by chain->error and cluster->error.  The error
 * code is typically set on-lock unless no I/O was requested, and set on
 * I/O otherwise.  If set for a cluster it generally means that the cluster
 * code could not find a valid copy to present.
 *
 * All H2 error codes are flags and can be accumulated by ORing them
 * together.
 *
 * IO		- An I/O error occurred
 * CHECK	- I/O succeeded but did not match the check code
 * INCOMPLETE	- A cluster is not complete enough to use, or
 *		  a chain cannot be loaded because its parent has an error.
 *
 * NOTE: API allows callers to check zero/non-zero to determine if an error
 *	 condition exists.
 *
 * NOTE: Chain's data field is usually NULL on an IO error but not necessarily
 *	 NULL on other errors.  Check chain->error, not chain->data.
 */
#define HAMMER2_ERROR_NONE		0	/* no error (must be 0) */
#define HAMMER2_ERROR_EIO		0x00000001	/* device I/O error */
#define HAMMER2_ERROR_CHECK		0x00000002	/* check code error */
#define HAMMER2_ERROR_INCOMPLETE	0x00000004	/* incomplete cluster */
#define HAMMER2_ERROR_DEPTH		0x00000008	/* tmp depth limit */
#define HAMMER2_ERROR_BADBREF		0x00000010	/* illegal bref */
#define HAMMER2_ERROR_ENOSPC		0x00000020	/* allocation failure */
#define HAMMER2_ERROR_ENOENT		0x00000040	/* entry not found */
#define HAMMER2_ERROR_ENOTEMPTY		0x00000080	/* dir not empty */
#define HAMMER2_ERROR_EAGAIN		0x00000100	/* retry */
#define HAMMER2_ERROR_ENOTDIR		0x00000200	/* not directory */
#define HAMMER2_ERROR_EISDIR		0x00000400	/* is directory */
#define HAMMER2_ERROR_EINPROGRESS	0x00000800	/* already running */
#define HAMMER2_ERROR_ABORTED		0x00001000	/* aborted operation */
#define HAMMER2_ERROR_EOF		0x00002000	/* end of scan */
#define HAMMER2_ERROR_EINVAL		0x00004000	/* catch-all */
#define HAMMER2_ERROR_EEXIST		0x00008000	/* entry exists */
#define HAMMER2_ERROR_EDEADLK		0x00010000
#define HAMMER2_ERROR_ESRCH		0x00020000
#define HAMMER2_ERROR_ETIMEDOUT		0x00040000

/*
 * Flags passed to hammer2_chain_lookup() and hammer2_chain_next()
 *
 * NOTES:
 *	NOLOCK	    - Input and output chains are referenced only and not
 *		      locked.  Output chain might be temporarily locked
 *		      internally.
 *
 *	NODATA	    - Asks that the chain->data not be resolved in order
 *		      to avoid I/O.
 *
 *	NODIRECT    - Prevents a lookup of offset 0 in an inode from returning
 *		      the inode itself if the inode is in DIRECTDATA mode
 *		      (i.e. file is <= 512 bytes).  Used by the synchronization
 *		      code to prevent confusion.
 *
 *	SHARED	    - The input chain is expected to be locked shared,
 *		      and the output chain is locked shared.
 *
 *	MATCHIND    - Allows an indirect block / freemap node to be returned
 *		      when the passed key range matches the radix.  Remember
 *		      that key_end is inclusive (e.g. {0x000,0xFFF},
 *		      not {0x000,0x1000}).
 *
 *		      (Cannot be used for remote or cluster ops).
 *
 *	ALLNODES    - Allows NULL focus.
 *
 *	ALWAYS	    - Always resolve the data.  If ALWAYS and NODATA are both
 *		      missing, bulk file data is not resolved but inodes and
 *		      other meta-data will.
 *
 *	NOUNLOCK    - Used by hammer2_chain_next() to leave the lock on
 *		      the input chain intact.  The chain is still dropped.
 *		      This allows the caller to add a reference to the chain
 *		      and retain it in a locked state (used by the
 *		      XOP/feed/collect code).
 */
#define HAMMER2_LOOKUP_NOLOCK		0x00000001	/* ref only */
#define HAMMER2_LOOKUP_NODATA		0x00000002	/* data left NULL */
#define HAMMER2_LOOKUP_NODIRECT		0x00000004	/* no offset=0 DD */
#define HAMMER2_LOOKUP_SHARED		0x00000100
#define HAMMER2_LOOKUP_MATCHIND		0x00000200	/* return all chains */
#define HAMMER2_LOOKUP_ALLNODES		0x00000400	/* allow NULL focus */
#define HAMMER2_LOOKUP_ALWAYS		0x00000800	/* resolve data */
#define HAMMER2_LOOKUP_NOUNLOCK		0x00001000	/* leave lock intact */

/*
 * Flags passed to hammer2_chain_modify() and hammer2_chain_resize()
 *
 * NOTE: OPTDATA allows us to avoid instantiating buffers for INDIRECT
 *	 blocks in the INITIAL-create state.
 */
#define HAMMER2_MODIFY_OPTDATA		0x00000002	/* data can be NULL */
#define HAMMER2_MODIFY_NO_MODIFY_TID	0x00000004
#define HAMMER2_MODIFY_UNUSED0008	0x00000008

/*
 * Flags passed to hammer2_chain_lock()
 *
 * NOTE: RDONLY is set to optimize cluster operations when *no* modifications
 *	 will be made to either the cluster being locked or any underlying
 *	 cluster.  It allows the cluster to lock and access data for a subset
 *	 of available nodes instead of all available nodes.
 */
#define HAMMER2_RESOLVE_NEVER		1
#define HAMMER2_RESOLVE_MAYBE		2
#define HAMMER2_RESOLVE_ALWAYS		3
#define HAMMER2_RESOLVE_MASK		0x0F

#define HAMMER2_RESOLVE_SHARED		0x10	/* request shared lock */
#define HAMMER2_RESOLVE_LOCKAGAIN	0x20	/* another shared lock */
#define HAMMER2_RESOLVE_RDONLY		0x40	/* higher level op flag */

/*
 * Flags passed to hammer2_chain_delete()
 */
#define HAMMER2_DELETE_PERMANENT	0x0001

/*
 * Flags passed to hammer2_chain_insert() or hammer2_chain_rename()
 * or hammer2_chain_create().
 */
#define HAMMER2_INSERT_PFSROOT		0x0004
#define HAMMER2_INSERT_SAMEPARENT	0x0008

/*
 * Flags passed to hammer2_chain_delete_duplicate()
 */
#define HAMMER2_DELDUP_RECORE		0x0001

/*
 * Cluster different types of storage together for allocations
 */
#define HAMMER2_FREECACHE_INODE		0
#define HAMMER2_FREECACHE_INDIR		1
#define HAMMER2_FREECACHE_DATA		2
#define HAMMER2_FREECACHE_UNUSED3	3
#define HAMMER2_FREECACHE_TYPES		4

/*
 * hammer2_freemap_alloc() block preference
 */
#define HAMMER2_OFF_NOPREF		((hammer2_off_t)-1)

/*
 * BMAP read-ahead maximum parameters
 */
#define HAMMER2_BMAP_COUNT		16	/* max bmap read-ahead */
#define HAMMER2_BMAP_BYTES		(HAMMER2_PBUFSIZE * HAMMER2_BMAP_COUNT)

/*
 * hammer2_freemap_adjust()
 */
#define HAMMER2_FREEMAP_DORECOVER	1
#define HAMMER2_FREEMAP_DOMAYFREE	2
#define HAMMER2_FREEMAP_DOREALFREE	3

/*
 * HAMMER2 cluster - A set of chains representing the same entity.
 *
 * hammer2_cluster typically represents a temporary set of representitive
 * chains.  The one exception is that a hammer2_cluster is embedded in
 * hammer2_inode.  This embedded cluster is ONLY used to track the
 * representitive chains and cannot be directly locked.
 *
 * A cluster is usually temporary (and thus per-thread) for locking purposes,
 * allowing us to embed the asynchronous storage required for cluster
 * operations in the cluster itself and adjust the state and status without
 * having to worry too much about SMP issues.
 *
 * The exception is the cluster embedded in the hammer2_inode structure.
 * This is used to cache the cluster state on an inode-by-inode basis.
 * Individual hammer2_chain structures not incorporated into clusters might
 * also stick around to cache miscellanious elements.
 *
 * Because the cluster is a 'working copy' and is usually subject to cluster
 * quorum rules, it is quite possible for us to end up with an insufficient
 * number of live chains to execute an operation.  If an insufficient number
 * of chains remain in a working copy, the operation may have to be
 * downgraded, retried, stall until the requisit number of chains are
 * available, or possibly even error out depending on the mount type.
 *
 * A cluster's focus is set when it is locked.  The focus can only be set
 * to a chain still part of the synchronized set.
 */
#endif
#define HAMMER2_XOPFIFO		16
#define HAMMER2_XOPFIFO_MASK	(HAMMER2_XOPFIFO - 1)
#define HAMMER2_XOPGROUPS	32
#define HAMMER2_XOPGROUPS_MASK	(HAMMER2_XOPGROUPS - 1)

#define HAMMER2_MAXCLUSTER	8
#define HAMMER2_XOPMASK_CLUSTER	(uint64_t)((1LLU << HAMMER2_MAXCLUSTER) - 1)
#define HAMMER2_XOPMASK_VOP	(uint64_t)0x0000000080000000LLU
#define HAMMER2_XOPMASK_FIFOW	(uint64_t)0x0000000040000000LLU
#define HAMMER2_XOPMASK_WAIT	(uint64_t)0x0000000020000000LLU
#define HAMMER2_XOPMASK_FEED	(uint64_t)0x0000000100000000LLU

#define HAMMER2_XOPMASK_ALLDONE	(HAMMER2_XOPMASK_VOP | HAMMER2_XOPMASK_CLUSTER)

#define HAMMER2_SPECTHREADS	1	/* sync */

/*
 * Support structure for dedup heuristic.
 */
struct hammer2_dedup {
	hammer2_off_t	data_off;
	u64int	data_crc;
	u32int	ticks;
	u32int	unused03;
};

typedef struct hammer2_dedup hammer2_dedup_t;

struct hammer2_dev {
	int		volhdrno;	/* last volhdrno written */
	hammer2_volume_data_t voldata;
	hammer2_volume_data_t volsync;	/* synchronized voldata */
};

typedef struct hammer2_dev hammer2_dev_t;

/*
 * Per-cluster management structure.  This structure will be tied to a
 * system mount point if the system is mounting the PFS, but is also used
 * to manage clusters encountered during the super-root scan or received
 * via LNK_SPANs that might not be mounted.
 *
 * This structure is also used to represent the super-root that hangs off
 * of a hard mount point.  The super-root is not really a cluster element.
 * In this case the spmp_hmp field will be non-NULL.  It's just easier to do
 * this than to special case super-root manipulation in the hammer2_chain*
 * code as being only hammer2_dev-related.
 *
 * pfs_mode and pfs_nmasters are rollup fields which critically describes
 * how elements of the cluster act on the cluster.  pfs_mode is only applicable
 * when a PFS is mounted by the system.  pfs_nmasters is our best guess as to
 * how many masters have been configured for a cluster and is always
 * applicable.  pfs_types[] is an array with 1:1 correspondance to the
 * iroot cluster and describes the PFS types of the nodes making up the
 * cluster.
 *
 * WARNING! Portions of this structure have deferred initialization.  In
 *	    particular, if not mounted there will be no wthread.
 *	    umounted network PFSs will also be missing iroot and numerous
 *	    other fields will not be initialized prior to mount.
 *
 *	    Synchronization threads are chain-specific and only applicable
 *	    to local hard PFS entries.  A hammer2_pfs structure may contain
 *	    more than one when multiple hard PFSs are present on the local
 *	    machine which require synchronization monitoring.  Most PFSs
 *	    (such as snapshots) are 1xMASTER PFSs which do not need a
 *	    synchronization thread.
 *
 * WARNING! The chains making up pfs->iroot's cluster are accounted for in
 *	    hammer2_dev->mount_count when the pfs is associated with a mount
 *	    point.
 */
struct hammer2_pfs {
	struct mount		*mp;
};

typedef struct hammer2_pfs hammer2_pfs_t;

TAILQ_HEAD(hammer2_pfslist, hammer2_pfs);

#define HAMMER2_LRU_LIMIT		1024	/* per pmp lru_list */

#define HAMMER2_DIRTYCHAIN_WAITING	0x80000000
#define HAMMER2_DIRTYCHAIN_MASK		0x7FFFFFFF

#define HAMMER2_LWINPROG_WAITING	0x80000000
#define HAMMER2_LWINPROG_WAITING0	0x40000000
#define HAMMER2_LWINPROG_MASK		0x3FFFFFFF

/*
 * hammer2_cluster_check
 */
#define HAMMER2_CHECK_NULL	0x00000001

