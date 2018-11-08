void loadinode(int fd, hammer2_off_t dataoff, hammer2_inode_data_t *inode);
void dumpinode(hammer2_inode_data_t *i);
void readvolume(int fd, hammer2_dev_t *hd);

void fsstart(Srv *);
void fsattach(Req *r);
void fsopen(Req *r);
char* fswalk(Fid *fid, char *name, Qid *qid);
char* fswalkclone(Fid *old, Fid *new);
void fsstat(Req *r);
void fsread(Req *r);

// constantly writing out the whole name is annoying, so we make a type
typedef hammer2_inode_data_t inode; 
typedef struct{
	// A pointer to the start of a DIRENT array.
	hammer2_blockref_t *entry;

	// the number of DIRENT blocks populated
	vlong count;
	// The maximum number of DIRENT blocks entry[] can contain.
	vlong cap;
} DirEnts;

typedef struct{
	Qid;
	inode;
	DirEnts;
	char *pfsname;
} root_t;
