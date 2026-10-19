
#define FUSE_USE_VERSION 26

#include </usr/include/fuse/fuse_lowlevel.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <errno.h>

#include "tosfs.h"

static void *fs_image;
static size_t fs_image_size;
static struct tosfs_superblock *superblock;
static struct tosfs_inode *inode_table;

static int load_filesystem(const char *path)
{
    int fd = open(path, O_RDONLY);
    struct stat st;

    if (fd == -1) {
        perror("open filesystem image");
        return -1;
    }

    if (fstat(fd, &st) == -1) {
        perror("fstat filesystem image");
        close(fd);
        return -1;
    }

    fs_image_size = st.st_size;

    fs_image = mmap(NULL, fs_image_size, PROT_READ,
                    MAP_PRIVATE, fd, 0);

    close(fd);

    if (fs_image == MAP_FAILED) {
        perror("mmap filesystem image");
        fs_image = NULL;
        return -1;
    }

    superblock = (struct tosfs_superblock *)fs_image;

    if (superblock->magic != TOSFS_MAGIC ||
        superblock->block_size != TOSFS_BLOCK_SIZE) {
        fprintf(stderr, "Invalid filesystem image\n");
        munmap(fs_image, fs_image_size);
        fs_image = NULL;
        return -1;
            }

    inode_table = (struct tosfs_inode *)
        ((char *)fs_image + TOSFS_INODE_BLOCK * TOSFS_BLOCK_SIZE);

    return 0;
}

static void getattr(fuse_req_t req, fuse_ino_t ino,
                            struct fuse_file_info *fi)
{
    (void)fi;

    struct stat stbuf = {0};

    if (ino == 0 ||
    ino >= TOSFS_BLOCK_SIZE / sizeof(struct tosfs_inode)) {
        fuse_reply_err(req, ENOENT);
        return;
    }

    struct tosfs_inode inode;
    memcpy(&inode, &inode_table[ino], sizeof(inode));

    /*fprintf(stderr,
        "getattr: requested ino=%lu, stored inode=%u, "
        "mode=%06o, perm=%06o\n",
        (unsigned long)ino, inode.inode,
        inode.mode, inode.perm);
*/
    if (inode.inode != ino) {
        fuse_reply_err(req, ENOENT);
        return;
    }

    stbuf.st_ino = ino;
    stbuf.st_uid = inode.uid;
    stbuf.st_gid = inode.gid;
    stbuf.st_mode = inode.mode;
    stbuf.st_nlink = inode.nlink;
    stbuf.st_size = inode.size;
    stbuf.st_blksize = TOSFS_BLOCK_SIZE;
    stbuf.st_blocks = (inode.size + 511) / 512;

    fuse_reply_attr(req, &stbuf, 1.0);
}

static void readdir(fuse_req_t req, fuse_ino_t ino,
                    size_t size, off_t off,
                    struct fuse_file_info *fi)
{
    (void)fi;

    // Only handle valid inode numbers
    if (ino == 0 ||
        ino >= TOSFS_BLOCK_SIZE / sizeof(struct tosfs_inode)) {
        fuse_reply_err(req, ENOENT);
        return;
        }

    struct tosfs_inode inode;
    memcpy(&inode, &inode_table[ino], sizeof(inode));

    if (inode.inode != ino) {
        fuse_reply_err(req, ENOENT);
        return;
    }

    // The requested inode must be a directory.
    if (!S_ISDIR(inode.mode)) {
        fuse_reply_err(req, ENOTDIR);
        return;
    }

    // Locate the directory's data block.
    size_t block_offset = (size_t)inode.block_no * TOSFS_BLOCK_SIZE;


    char *buffer = malloc(size);
    if (buffer == NULL) {
        fuse_reply_err(req, ENOMEM);
        return;
    }

    size_t used = 0;
    struct tosfs_dentry *entries =
        (struct tosfs_dentry *)((char *)fs_image + block_offset);

    size_t entry_count = TOSFS_BLOCK_SIZE / sizeof(struct tosfs_dentry);

    for (size_t i = (size_t)off; i < entry_count; i++) {
        if (entries[i].inode == 0)
            continue;

        struct stat st = {0};
        st.st_ino = entries[i].inode;

        size_t remaining = size - used;
        size_t entry_size = fuse_add_direntry(
            req, buffer + used, remaining,
            entries[i].name, &st, (off_t)(i + 1));
        if (entry_size > remaining)
            break;

        used += entry_size;
    }

    fuse_reply_buf(req, buffer, used);
    free(buffer);
}

static struct fuse_lowlevel_ops futosfs_oper = {
    .getattr	= getattr,
    .readdir	= readdir,
/*
    .lookup		= lookup,
    open		= open,
    .read		= read,
    .write		= write,
*/
};

int main(int argc, char *argv[])
{
    struct fuse_args args = FUSE_ARGS_INIT(argc, argv);
    struct fuse_chan *ch;
    char *mountpoint;
    int err = -1;

    if (load_filesystem("test_tosfs_files") == -1)
        return EXIT_FAILURE;

    if (fuse_parse_cmdline(&args, &mountpoint, NULL, NULL) != -1 &&
        (ch = fuse_mount(mountpoint, &args)) != NULL) {
        struct fuse_session *se;

        se = fuse_lowlevel_new(&args, &futosfs_oper,
                               sizeof(futosfs_oper), NULL);

        if (se != NULL) {
            if (fuse_set_signal_handlers(se) != -1) {
                fuse_session_add_chan(se, ch);
                err = fuse_session_loop(se);
                fuse_remove_signal_handlers(se);
                fuse_session_remove_chan(ch);
            }

            fuse_session_destroy(se);
        }

        fuse_unmount(mountpoint, ch);
    }

    fuse_opt_free_args(&args);
    free(mountpoint);

    return err ? 1 : 0;
}
