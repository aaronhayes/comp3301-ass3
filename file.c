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
#include <asm/uaccess.h>

/*
 * Forward declarations
 */
ssize_t write_encrypt (struct file*, const char __user*, size_t, loff_t *);

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
 * Checks if a file is in the /encrypt folder
 * Return 1 for true, 0 for false.
 */
int is_encrypt_folder(struct file *f) {
    struct dentry *parent, *last;

	if (f != NULL && f->f_dentry != NULL && f->f_dentry->d_parent != NULL) {
		last = f->f_dentry;
		parent = f->f_dentry->d_parent;

		while (parent != NULL) {
			if (!strcmp(parent->d_name.name, "/")) {
				if (!strcmp(last->d_name.name, EXT_ENCRYPTION_DIRECTORY)) {
					return 1;
				} else {
					break;
				}
			}
			last = parent;
			parent = parent->d_parent;
		}
	}
    return 0;
}

/*
 *  Run encryption on a buffer
 */
void encrypt(char *buffer, ssize_t length) {
	int i;
	for (i = 0; i < length; i++) {
		if (buffer[i] != '\0') {
			buffer[i] = buffer[i] ^ encryption_key;
		}
	}
}

/*
 *  Run decryption on a buffer
 */
void decrypt(char *buffer, ssize_t length) {
	int i;
	for (i = 0; i < length; i++) {
		if (buffer[i] != '\0') {
			buffer[i] = buffer[i] ^ encryption_key;
		}
	}
	buffer[length] = '\0';
}


/*
 * COMP3301 Addition
 * Write immediate files data to the inode. Covert back to
 * Regular files when the data exceeds the limit.
 */
ssize_t do_immediate_write (struct file* flip, const char __user* buf,
	size_t len, loff_t *ppos, int need_to_encrypt) {

	struct ext2_inode_info *inode_info = EXT2_I(flip->f_dentry->d_inode);
	struct inode *inode = flip->f_dentry->d_inode;
	char *data = (char *)inode_info->i_data;
	char *copy;
	char *ext_inode_data = (char *) (EXT2_I(inode)->i_data);
	int err;
	ssize_t result;


	if (*ppos + len >= IMMEDIATE_FILE_SIZE) {
		// Convert to regular file

       	copy = (char *) kmalloc(sizeof(char) * strlen(ext_inode_data)
       	 	+ 1, GFP_KERNEL);
       	memset(copy, 0, strlen(ext_inode_data) + 1);
       	memcpy(copy, ext_inode_data, strlen(ext_inode_data));
       	copy[strlen(ext_inode_data)] = 0;


       	inode->i_mode &= ~(S_IF_IMMEDIATE & S_IFMT);
       	inode->i_mode |= S_IFREG & S_IFMT;

        inode_info->i_data[0] = ext2_new_block(inode, 0, &err);
        mark_inode_dirty(inode);

       	flip->f_pos = 0;
       	result = write_encrypt(flip, copy, strlen(copy), &flip->f_pos);
       	result = write_encrypt(flip, buf, len, ppos);

       	kfree(copy);
        return result;
	}

    if (need_to_encrypt) {
    	encrypt(buf, len);
    }

	if (copy_from_user(data + *ppos, buf, len)) {
		return -1;
	}

    *ppos += len;
    flip->f_pos = *ppos;
    inode->i_size += len;
    mark_inode_dirty(inode);

	return len;
}

/*
 * Copy char * buffer into new buffer
 */
void copy_buffer(char *dest, char *source, int len) {
	int n;
	for (n = 0; n < len; n++) {
		dest[n] = source[n];
	}
}

/*
 * COMP3301 Addition
 * Read from immediate files with data stored directly in the inode.
 */
ssize_t do_immediate_read (struct file* flip, const char __user* buf,
	size_t len, loff_t *ppos) {

    struct inode *inode = flip->f_dentry->d_inode;
    struct ext2_inode_info *inode_info = EXT2_I(inode);
    char* new_buffer = (char *) kmalloc(sizeof(char) * len, GFP_KERNEL);
    char *data = (char *)inode_info->i_data;
    copy_buffer(new_buffer, data, len);

    if (*ppos + len > inode->i_size) {
    	len -= ((*ppos + len) - inode->i_size);
    }

	if (copy_to_user(buf, new_buffer + *ppos, len)) {
		kfree(new_buffer);
		return -1;
	}

	kfree(new_buffer);
	*ppos += len;

    return len;
}


/*
 * COMP3301 Addition
 * Use the encryption key to change the content of a buffer
 *  then write the buffer to the FS
 */
ssize_t write_encrypt (struct file* flip, const char __user* buf,
	size_t len, loff_t *ppos) {

	ssize_t result;
	struct inode *inode = flip->f_dentry->d_inode;
    mm_segment_t user_filesystem = get_fs();
    char* new_buffer = (char *) kmalloc(sizeof(char) * len, GFP_KERNEL);

    if (copy_from_user(new_buffer, buf, len)) {
    	kfree(new_buffer);
    	return -1;
    }

    if (is_encrypt_folder(flip)) {
        set_fs(get_ds());

        if (S_IS_IMMEDIATE(inode->i_mode)) {
    		result = do_immediate_write(flip, new_buffer, len, ppos, 1);
    	} else {
    		encrypt(new_buffer, len);
    		result = do_sync_write(flip, new_buffer, len, ppos);
    	}

    	set_fs(user_filesystem);
   	} else {
   		set_fs(get_ds());

        if (S_IS_IMMEDIATE(inode->i_mode)) {
    		result = do_immediate_write(flip, new_buffer, len, ppos, 0);
    	} else {
    		result = do_sync_write(flip, new_buffer, len, ppos);
    	}

		set_fs(user_filesystem);
   	}

	kfree(new_buffer);
	return result;
}



/*
 * COMP3301 Addition
 * Use the encryption key to change the content of a buffer
 *  then read the buffer to user
 */
ssize_t read_encrypt (struct file* flip, const char __user* buf,
	size_t len, loff_t *ppos) {

	ssize_t result;
    struct inode *inode = flip->f_dentry->d_inode;
    mm_segment_t user_filesystem = get_fs();
    char* new_buffer = (char *) kmalloc(sizeof(char) * len, GFP_KERNEL);


	memset(new_buffer, 0, len);

    set_fs(get_ds());

    if (S_IS_IMMEDIATE(inode->i_mode)) {
    	result = do_immediate_read(flip, new_buffer, len, ppos);
    } else {
   		result = do_sync_read(flip, new_buffer, len, ppos);
   	}
    set_fs(user_filesystem);

    if (is_encrypt_folder(flip)) {
		decrypt(new_buffer, len);
   	}

	if (copy_to_user(buf, new_buffer, len)) {
		kfree(new_buffer);
		return -1;
	}

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
