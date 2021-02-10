/*
  FUSE: Filesystem in Userspace
  Copyright (C) 2001-2007  Miklos Szeredi <miklos@szeredi.hu>
  Copyright (C) 2011       Sebastian Pipping <sebastian@pipping.org>

  This program can be distributed under the terms of the GNU GPL.
  See the file COPYING.
  * fusexmp_fh and module subdir merge

  gcc -Wall fusexmp_fh.c `pkg-config fuse --cflags --libs` -lulockmgr -o fusexmp_fh
*/

#define FUSE_USE_VERSION 26

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#define _GNU_SOURCE

#include <fuse_merge.h>
#include <fuse.h>
#include <ulockmgr.h>
#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <errno.h>
#include <sys/time.h>
#ifdef HAVE_SETXATTR
#include <sys/xattr.h>
#endif
#include <sys/file.h> /* flock(2) */

#ifdef __ANDROID__
#include <dirent2.h>
#else
#include <dirent.h>
#endif

#ifndef DTTOIF
/*安卓8.1系统dirent.h 不带这个宏定义*/
#define DTTOIF(dirtype) ((dirtype) << 12)
#endif

static int xmp_getattr(const char *path, struct stat *stbuf)
{
	int res = lstat(path, stbuf);
	if (res == -1)
		return -errno;

	return 0;
}

static int xmp_fgetattr(const char *path, struct stat *stbuf,
						struct fuse_file_info *fi)
{
	int res = fstat(fi->fh, stbuf);
	if (res == -1)
		return -errno;
	return 0;
}

static int xmp_access(const char *path, int mask)
{
	int res = access(path, mask);
	if (res == -1)
		return -errno;
	return 0;
}

static int xmp_readlink(const char *path, char *buf, size_t size)
{
	int res = readlink(path, buf, size - 1);
	if (res == -1)
		return -errno;
	buf[res] = 0;
	return 0;
}

// struct xmp_dirp
// {
// 	DIR *dp;
// 	struct dirent *entry;
// 	off_t offset;
// };

static int xmp_opendir(const char *path, struct fuse_file_info *fi)
{
	DIR *dirp = opendir(path);
	if (!dirp)
		return -errno;
	fi->fh = (uint64_t)dirp;
	return 0;
}

static inline DIR *get_dirp(struct fuse_file_info *fi)
{
	return (void *)fi->fh;
}

static int xmp_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
					   off_t offset, struct fuse_file_info *fi)
{
	DIR *dirp = get_dirp(fi);
	// #ifdef __ANDROID__
	// #pragma GCC diagnostic push
	// #pragma GCC diagnostic ignored "-Wincompatible-pointer-types"
	// #endif
	if (offset != telldir(dirp))
		seekdir(dirp, offset);

	struct stat st;
	memset(&st, 0, sizeof(st));
	struct dirent *dep;
	while ((dep = readdir(dirp)))
	{
		st.st_ino = dep->d_ino;
		st.st_mode = DTTOIF(dep->d_type);
		if (filler(buf, dep->d_name, &st, telldir(dirp)))
			break;
	}
	// #ifdef __ANDROID__
	// #pragma GCC diagnostic pop
	// #endif

	return 0;
}

static int xmp_releasedir(const char *path, struct fuse_file_info *fi)
{
	closedir(get_dirp(fi));
	return 0;
}

static int xmp_mknod(const char *path, mode_t mode, dev_t rdev)
{
	int res;

	if (S_ISFIFO(mode))
		res = mkfifo(path, mode);
	else
		res = mknod(path, mode, rdev);
	if (res == -1)
		return -errno;

	return 0;
}

static int xmp_mkdir(const char *path, mode_t mode)
{
	int res = mkdir(path, mode);
	if (res == -1)
		return -errno;

	return 0;
}

static int xmp_unlink(const char *path)
{
	int res = unlink(path);
	if (res == -1)
		return -errno;

	return 0;
}

static int xmp_rmdir(const char *path)
{
	int res = rmdir(path);
	if (res == -1)
		return -errno;

	return 0;
}

static int xmp_symlink(const char *from, const char *to)
{
	int res = symlink(from, to);
	if (res == -1)
		return -errno;

	return 0;
}

static int xmp_rename(const char *from, const char *to)
{
	int res = rename(from, to);
	if (res == -1)
		return -errno;

	return 0;
}

static int xmp_link(const char *from, const char *to)
{
	int res = link(from, to);
	if (res == -1)
		return -errno;

	return 0;
}

static int xmp_chmod(const char *path, mode_t mode)
{
	int res = chmod(path, mode);
	if (res == -1)
		return -errno;

	return 0;
}

static int xmp_chown(const char *path, uid_t uid, gid_t gid)
{
	int res = lchown(path, uid, gid);
	if (res == -1)
		return -errno;

	return 0;
}

static int xmp_truncate(const char *path, off_t size)
{
	int res = truncate(path, size);
	if (res == -1)
		return -errno;

	return 0;
}

static int xmp_ftruncate(const char *path, off_t size,
						 struct fuse_file_info *fi)
{
	int res = ftruncate(fi->fh, size);
	if (res == -1)
		return -errno;

	return 0;
}

#ifdef HAVE_UTIMENSAT
static int xmp_utimens(const char *path, const struct timespec ts[2])
{
	int res;

	/* don't use utime/utimes since they follow symlinks */
	res = utimensat(0, path, ts, AT_SYMLINK_NOFOLLOW);
	if (res == -1)
		return -errno;

	return 0;
}
#endif

static int xmp_create(const char *path, mode_t mode, struct fuse_file_info *fi)
{
	int fd = open(path, fi->flags, mode);
	if (fd == -1)
		return -errno;

	fi->fh = fd;
	return 0;
}

static int xmp_open(const char *path, struct fuse_file_info *fi)
{
	int fd = open(path, fi->flags);
	if (fd == -1)
		return -errno;

	fi->fh = fd;
	return 0;
}

static int xmp_read(const char *path, char *buf, size_t size, off_t offset,
					struct fuse_file_info *fi)
{
	int res = pread(fi->fh, buf, size, offset);
	if (res == -1)
		res = -errno;

	return res;
}

static int xmp_read_buf(const char *path, struct fuse_bufvec **bufp,
						size_t size, off_t offset, struct fuse_file_info *fi)
{
	struct fuse_bufvec *src = malloc(sizeof(struct fuse_bufvec));
	if (src == NULL)
		return -ENOMEM;

	*src = FUSE_BUFVEC_INIT(size);

	src->buf[0].flags = FUSE_BUF_IS_FD | FUSE_BUF_FD_SEEK;
	src->buf[0].fd = fi->fh;
	src->buf[0].pos = offset;

	*bufp = src;

	return 0;
}

static int xmp_write(const char *path, const char *buf, size_t size,
					 off_t offset, struct fuse_file_info *fi)
{
	int res = pwrite(fi->fh, buf, size, offset);
	if (res == -1)
		res = -errno;

	return res;
}

static int xmp_write_buf(const char *path, struct fuse_bufvec *buf,
						 off_t offset, struct fuse_file_info *fi)
{
	struct fuse_bufvec dst = FUSE_BUFVEC_INIT(fuse_buf_size(buf));

	dst.buf[0].flags = FUSE_BUF_IS_FD | FUSE_BUF_FD_SEEK;
	dst.buf[0].fd = fi->fh;
	dst.buf[0].pos = offset;

	return fuse_buf_copy(&dst, buf, FUSE_BUF_SPLICE_NONBLOCK);
}

static int xmp_statfs(const char *path, struct statvfs *stbuf)
{
	int res = statvfs(path, stbuf);
	if (res == -1)
		return -errno;

	return 0;
}

static int xmp_flush(const char *path, struct fuse_file_info *fi)
{
	int res;
	/* This is called from every close on an open file, so call the
	   close on the underlying filesystem.	But since flush may be
	   called multiple times for an open file, this must not really
	   close the file.  This is important if used on a network
	   filesystem like NFS which flush the data/metadata on close() */
	res = close(dup(fi->fh));
	if (res == -1)
		return -errno;

	return 0;
}

static int xmp_release(const char *path, struct fuse_file_info *fi)
{
	close(fi->fh);
	return 0;
}

static int xmp_fsync(const char *path, int isdatasync,
					 struct fuse_file_info *fi)
{
	int res;

#ifndef HAVE_FDATASYNC
	(void)isdatasync;
#else
	if (isdatasync)
		res = fdatasync(fi->fh);
	else
#endif
	res = fsync(fi->fh);
	if (res == -1)
		return -errno;

	return 0;
}

#ifdef HAVE_POSIX_FALLOCATE
static int xmp_fallocate(const char *path, int mode,
						 off_t offset, off_t length, struct fuse_file_info *fi)
{
	(void)path;

	if (mode)
		return -EOPNOTSUPP;

	return -posix_fallocate(fi->fh, offset, length);
}
#endif

#ifdef HAVE_SETXATTR
/* xattr operations are optional and can safely be left unimplemented */
static int xmp_setxattr(const char *path, const char *name, const char *value,
						size_t size, int flags)
{
	int res = lsetxattr(path, name, value, size, flags);
	if (res == -1)
		return -errno;
	return 0;
}

static int xmp_getxattr(const char *path, const char *name, char *value,
						size_t size)
{
	int res = lgetxattr(path, name, value, size);
	if (res == -1)
		return -errno;
	return res;
}

static int xmp_listxattr(const char *path, char *list, size_t size)
{
	int res = llistxattr(path, list, size);
	if (res == -1)
		return -errno;
	return res;
}

static int xmp_removexattr(const char *path, const char *name)
{
	int res = lremovexattr(path, name);
	if (res == -1)
		return -errno;
	return 0;
}
#endif /* HAVE_SETXATTR */

static int xmp_lock(const char *path, struct fuse_file_info *fi, int cmd,
					struct flock *lock)
{
	(void)path;

	return ulockmgr_op(fi->fh, cmd, lock, &fi->lock_owner,
					   sizeof(fi->lock_owner));
}

static int xmp_flock(const char *path, struct fuse_file_info *fi, int op)
{
	int res;
	(void)path;

	res = flock(fi->fh, op);
	if (res == -1)
		return -errno;

	return 0;
}

extern struct fuse_operations nfs_oper;
static void set_oper_bind()
{
	memset(&nfs_oper,0,sizeof(struct fuse_operations));

	nfs_oper.getattr = xmp_getattr;
	nfs_oper.fgetattr = xmp_fgetattr;
	nfs_oper.access = xmp_access;
	nfs_oper.readlink = xmp_readlink;
	nfs_oper.opendir = xmp_opendir;
	nfs_oper.readdir = xmp_readdir;
	nfs_oper.releasedir = xmp_releasedir;
	nfs_oper.mknod = xmp_mknod;
	nfs_oper.mkdir = xmp_mkdir;
	nfs_oper.symlink = xmp_symlink;
	nfs_oper.unlink = xmp_unlink;
	nfs_oper.rmdir = xmp_rmdir;
	nfs_oper.rename = xmp_rename;
	nfs_oper.link = xmp_link;
	nfs_oper.chmod = xmp_chmod;
	nfs_oper.chown = xmp_chown;
	nfs_oper.truncate = xmp_truncate;
	nfs_oper.ftruncate = xmp_ftruncate;
#ifdef HAVE_UTIMENSAT
	nfs_oper.utimens = xmp_utimens;
#endif
	nfs_oper.create = xmp_create;
	nfs_oper.open = xmp_open;
	nfs_oper.read = xmp_read;
	nfs_oper.read_buf = xmp_read_buf;
	nfs_oper.write = xmp_write;
	nfs_oper.write_buf = xmp_write_buf;
	nfs_oper.statfs = xmp_statfs;
	nfs_oper.flush = xmp_flush;
	nfs_oper.release = xmp_release;
	nfs_oper.fsync = xmp_fsync;
#ifdef HAVE_POSIX_FALLOCATE
	nfs_oper.fallocate = xmp_fallocate;
#endif
#ifdef HAVE_SETXATTR
	nfs_oper.setxattr = xmp_setxattr;
	nfs_oper.getxattr = xmp_getxattr;
	nfs_oper.listxattr = xmp_listxattr;
	nfs_oper.removexattr = xmp_removexattr;
#endif
	nfs_oper.lock = xmp_lock;
	nfs_oper.flock = xmp_flock;

	nfs_oper.flag_nullpath_ok = 1;
#if HAVE_UTIMENSAT
	nfs_oper.flag_utime_omit_ok = 1;
#endif
}

int _env_init_bind(struct nfsdata *_d, struct fuse_args *args)
{
	int res = 0;
#ifdef FLAG_STATIC_LINKFUSE
	fuse_module_libstaticlink_explicitreference_subdir();
#endif
	char *arg_pre = "-omodules=subdir,subdir=", *arg_pre2 = ",fsname=", *arg = NULL;
	size_t mlen = strlen(arg_pre) + strlen(arg_pre2) + strlen(_d->fsname) * 2 + 1;
	arg = malloc(mlen);
	if (!arg)
	{
		fprintf(stderr, "fuse: memory allocation failed\n");
		res = -errno;
		goto out_free;
	}
	arg[0] = 0, strcat(strcat(arg, arg_pre), _d->fsname);
	strcat(strcat(arg, arg_pre2), _d->fsname);
	if (fuse_opt_add_arg(args, arg))
	{
		res = -6;
		goto out_free;
	}

	set_oper_bind();
out_free:
	if (arg)
		free(arg);
	return res;
}
