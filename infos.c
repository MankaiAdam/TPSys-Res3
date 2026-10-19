#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>

#include "tosfs.h"

int main(void)
{
    struct stat st;

    /* Open the filesystem image */
    int fd = open("test_tosfs_files", O_RDONLY);
    if (fd == -1) {
        perror("open");
        return EXIT_FAILURE;
    }

    /* Get the image size */
    if (fstat(fd, &st) == -1) {
        perror("fstat");
        close(fd);
        return EXIT_FAILURE;
    }

    /* Map the image into memory */
    void *image = mmap(NULL, st.st_size, PROT_READ,
                       MAP_PRIVATE, fd, 0);

    if (image == MAP_FAILED) {
        perror("mmap");
        close(fd);
        return EXIT_FAILURE;
    }

    close(fd);

    /* Interpret block 0 as the superblock */
    struct tosfs_superblock *sb =
        (struct tosfs_superblock *)image;

    printf("Filesystem information:\n");
    printf("-----------------------\n");
    printf("Magic number : 0x%08x\n", sb->magic);
    printf("Block bitmap : 0x%08x\n", sb->block_bitmap);
    printf("Inode bitmap : 0x%08x\n", sb->inode_bitmap);
    printf("Block size   : %u bytes\n", sb->block_size);
    printf("Block count  : %u\n", sb->blocks);
    printf("Inode count  : %u\n", sb->inodes);
    printf("Root inode   : %u\n", sb->root_inode);
    printf("Image size   : %lld bytes\n", (long long)st.st_size);

    /* Validate the superblock */
    if (sb->magic != TOSFS_MAGIC) {
        fprintf(stderr, "Invalid filesystem magic number!\n");
        munmap(image, st.st_size);
        return EXIT_FAILURE;
    }

    if (sb->block_size != TOSFS_BLOCK_SIZE) {
        fprintf(stderr, "Inconsistent filesystem dimensions!\n");
        munmap(image, st.st_size);
        return EXIT_FAILURE;
        }

    printf("\nSuperblock validation successful.\n");

    struct tosfs_inode *inodes =
        (struct tosfs_inode *)((char *)image +
                               TOSFS_INODE_BLOCK * TOSFS_BLOCK_SIZE);

    printf("=== Inode table ===\n");

    for (unsigned int i = 0; i <= sb->inodes; i++) {
        struct tosfs_inode inode;
        memcpy(&inode, &inodes[i], sizeof(inode));

        printf("Entry %u: inode=%u, block=%u, mode=0%o, "
               "perm=0%o, size=%u, nlink=%u, uid=%u, gid=%u\n",
               i,
               inode.inode,
               inode.block_no,
               inode.mode,
               inode.perm,
               inode.size,
               inode.nlink,
               inode.uid,
               inode.gid);
    }

    /*
     * Block 2 contains the root directory entries.
     */
    struct tosfs_dentry *entries =
        (struct tosfs_dentry *)((char *)image +
                                TOSFS_ROOT_BLOCK * TOSFS_BLOCK_SIZE);

    printf("\n=== Root directory entries ===\n");

    size_t max_entries = TOSFS_BLOCK_SIZE / sizeof(struct tosfs_dentry);

    for (size_t i = 0; i < max_entries; i++) {
        struct tosfs_dentry entry;
        memcpy(&entry, &entries[i], sizeof(entry));

        if (entry.inode == 0)
            continue;

        /* Ensure the name is safely terminated before printing. */
        char name[TOSFS_MAX_NAME_LENGTH + 1];
        memcpy(name, entry.name, TOSFS_MAX_NAME_LENGTH);
        name[TOSFS_MAX_NAME_LENGTH] = '\0';

        printf("Entry %zu: inode=%u, name=\"%s\"\n",
               i, entry.inode, name);
    }

    munmap(image, st.st_size);
    return EXIT_SUCCESS;
}