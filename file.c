/*
 *  linux/fs/ext2/file.c
 *
 * Copyright (C) 1992, 1993, 1994, 1995
 * Remy Card (card@masi.ibp.fr)
 * Laboratoire MASI - Institut Blaise Pascal
 * Universite Pierre et Marie Curie (Paris VI)
 *
 *  from
 *
 *  linux/fs/minix/file.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 *
 *  ext2 fs regular file handling primitives
 *
 *  64-bit file support on 64-bit platforms by Jakub Jelinek
 * 	(jj@sunsite.ms.mff.cuni.cz)
 */

#include <linux/time.h>
#include <linux/pagemap.h>
#include <linux/quotaops.h>
#include "ext2.h"
#include "xattr.h"
#include "acl.h"

/*
 * Called when filp is released. This happens when all file descriptors
 * for a single struct file are closed. Note that different open() calls
 * for the same file yield different struct file structures.
 */
static int ext2_release_file (struct inode * inode, struct file * filp)
{
	if (filp->f_mode & FMODE_WRITE) {
		mutex_lock(&EXT2_I(inode)->truncate_mutex);
		ext2_discard_reservation(inode);
		mutex_unlock(&EXT2_I(inode)->truncate_mutex);
	}
	return 0;
}

/*
 * COMP3301
 *
 *
 *
 */

ssize_t write_encrypt (struct file* f, const char __user* buffer,
	size_t length, loff_t* pos) {

	ssize_t result;
	int n;
    struct dentry *loop, *last;
    mm_segment_t user_filesystem;
    char* new_buffer = kmalloc(length, GFP_NOFS);
    int crypt = 0;
    memset(new_buffer, 0, length);

    // Check to see if file should be encrypted
    if (f != NULL) {
        if (f->f_dentry != NULL && &f->f_dentry->d_name != NULL) {
    	  	last = NULL;
          	loop = f->f_dentry->d_parent;

            while (loop != NULL) {
            	if (!strncmp(loop->d_name.name, "/", 2)) {
                	if (last != NULL &&
                		!strncmp(last->d_name.name, EXT_ENCRYPTION_DIRECTORY,
                			strlen(EXT_ENCRYPTION_DIRECTORY) + 2)) {
                            crypt = 1;
                			// Encrypt Buffer
                			for (n = 0; n < length; n++) {
                				new_buffer[n] = buffer[n] ^ encryption_key; // XOR
                			}

                	}
                	break;
            	}
				last = loop;
				loop = loop->d_parent;
            }
        }
    }

	// Must switch to Kernel Space before writing
    user_filesystem = get_fs();
    set_fs(KERNEL_DS);

	// Check if we are writing the encrypted buffer or the original buffer
    if (crypt) {
    	result = do_sync_write(f, new_buffer, length, pos);
    } else {
    	result = do_sync_write(f, buffer, length, pos);
    }

	set_fs(user_filesystem);
	kfree(new_buffer);
	return result;
}


/*
 *
 *
 *
 */
ssize_t read_encrypt(struct file* f, char __user* buffer,
	size_t length, loff_t* pos) {

	ssize_t result;
	int n;
    struct dentry *loop, *last;
    mm_segment_t user_filesystem;
    char* new_buffer = kmalloc(length, GFP_NOFS);
    int crypt = 0;
    memset(new_buffer, 0, length);

   	user_filesystem = get_fs();
   	set_fs(KERNEL_DS);
   	result = do_sync_read(f, new_buffer, length, pos);
   	set_fs(user_filesystem);

	// Check to see if file is encrypted
	if (f != NULL) {
		if (f->f_dentry != NULL && &f->f_dentry->d_name != NULL) {
			last = NULL;
			loop = f->f_dentry->d_parent;

			while (loop != NULL) {
				if (!strncmp(loop->d_name.name, "/", 2)) {
					if (last != NULL &&
						!strncmp(last->d_name.name, EXT_ENCRYPTION_DIRECTORY,
							strlen(EXT_ENCRYPTION_DIRECTORY) + 2)) {
							crypt = 1;
							// Decrypt Buffer
							for (n = 0; n < length; n++) {
								new_buffer[n] = buffer[n] ^ encryption_key; // XOR
							}
							new_buffer[length] = (char) 0;
					}
					break;
				}
				last = loop;
				loop = loop->d_parent;
			}
		}
	}

    // Copy to User Space
    copy_to_user(buffer, new_buffer, length);
	kfree(new_buffer);
	return result;
}



int ext2_fsync(struct file *file, loff_t start, loff_t end, int datasync)
{
	int ret;
	struct super_block *sb = file->f_mapping->host->i_sb;
	struct address_space *mapping = sb->s_bdev->bd_inode->i_mapping;

	ret = generic_file_fsync(file, start, end, datasync);
	if (ret == -EIO || test_and_clear_bit(AS_EIO, &mapping->flags)) {
		/* We don't really know where the IO error happened... */
		ext2_error(sb, __func__,
			   "detected IO error when writing metadata buffers");
		ret = -EIO;
	}
	return ret;
}

/*
 * We have mostly NULL's here: the current defaults are ok for
 * the ext2 filesystem.
 *
 * COMP3301
 * 	Ensure read/write will try to encrypt in the right folder
 */
const struct file_operations ext2_file_operations = {
	.llseek		= generic_file_llseek,
	.read		= read_encrypt,
	.write		= write_encrypt,
	.aio_read	= generic_file_aio_read,
	.aio_write	= generic_file_aio_write,
	.unlocked_ioctl = ext2_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl	= ext2_compat_ioctl,
#endif
	.mmap		= generic_file_mmap,
	.open		= dquot_file_open,
	.release	= ext2_release_file,
	.fsync		= ext2_fsync,
	.splice_read	= generic_file_splice_read,
	.splice_write	= generic_file_splice_write,
};

#ifdef CONFIG_EXT2_FS_XIP
const struct file_operations ext2_xip_file_operations = {
	.llseek		= generic_file_llseek,
	.read		= xip_file_read,
	.write		= xip_file_write,
	.unlocked_ioctl = ext2_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl	= ext2_compat_ioctl,
#endif
	.mmap		= xip_file_mmap,
	.open		= dquot_file_open,
	.release	= ext2_release_file,
	.fsync		= ext2_fsync,
};
#endif

const struct inode_operations ext2_file_inode_operations = {
#ifdef CONFIG_EXT2_FS_XATTR
	.setxattr	= generic_setxattr,
	.getxattr	= generic_getxattr,
	.listxattr	= ext2_listxattr,
	.removexattr	= generic_removexattr,
#endif
	.setattr	= ext2_setattr,
	.get_acl	= ext2_get_acl,
	.fiemap		= ext2_fiemap,
};
