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
    if ((!strcmp(f->f_dentry->d_parent->d_name.name, EXT_ENCRYPTION_DIRECTORY)) &&
    	(!strcmp(f->f_dentry->d_parent->d_parent->d_name.name, "/"))) {
    		return 1;
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
	buffer[length] = (char) 0;
}

/*
 * COMP3301
 *
 *
 *
 */

ssize_t write_encrypt (struct file* flip, const char __user* buf,
	size_t len, loff_t *ppos) {

	ssize_t result;
    mm_segment_t user_filesystem = get_fs();
    char* new_buffer = kmalloc(sizeof(char) * len, GFP_KERNEL);

    if (copy_from_user(new_buffer, buf, len)) {
    	kfree(new_buffer);
    	return -1;
    }

	/*printk(KERN_INFO "Text: %s\n", buffer);
	printk(KERN_INFO "Encryption Key: %x\n", encryption_key);
    printk(KERN_INFO "Length = %x\n", len);*/

    if (is_encrypt_folder(flip)) {
    	encrypt(new_buffer, len);
    	/*printk(KERN_INFO "Text: %s\n", new_buffer);*/
        set_fs(get_ds());
    	result = do_sync_write(flip, new_buffer, len, ppos);
    	set_fs(user_filesystem);
   	} else {
		result = do_sync_write(flip, buf, len, ppos);
   	}
	kfree(new_buffer);
	return result;
}

/*
 *
 *
 */
ssize_t read_encrypt (struct file* flip, const char __user* buf,
	size_t len, loff_t *ppos) {

	ssize_t result;
    mm_segment_t user_filesystem = get_fs();
    char* new_buffer = kmalloc(sizeof(char) * len, GFP_KERNEL);


	memset(new_buffer, 0, len);

    set_fs(get_ds());
   	result = do_sync_read(flip, new_buffer, len, ppos);
    set_fs(user_filesystem);

	/*printk(KERN_INFO "Text: %s\n", buffer);
	printk(KERN_INFO "Encryption Key: %x\n", encryption_key);
    printk(KERN_INFO "Length = %x\n", len);


	printk(KERN_INFO "Text: %s\n", new_buffer); */


    if (is_encrypt_folder(flip)) {
    	/*printk(KERN_INFO "Text: %s\n", new_buffer);

		if (copy_from_user(new_buffer, buf, len)) {
			kfree(new_buffer);
			return -1;
		} */

		decrypt(new_buffer, len);

		/* printk(KERN_INFO "Decrypted Text: %s\n", new_buffer); */
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
