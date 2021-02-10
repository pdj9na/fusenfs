/*
  fusenfs-smb module: offset paths with a base directory
  Copyright (C) 2007  Miklos Szeredi <miklos@szeredi.hu>

  This program can be distributed under the terms of the GNU LGPLv2.
  See the file COPYING.LIB
*/

#define FUSE_USE_VERSION 26

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <fuse_merge.h>
#include <fuse.h>
#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#ifndef WIN32
#include <poll.h>
#endif
#include <pthread.h>
#include <unistd.h>
#include <sys/types.h>

#include <smb2/smb2.h>
#include <smb2/libsmb2.h>

#ifdef WIN32
#include <winsock2.h>
#include <win32/win32_compat.h>
#endif

#ifndef DTTOIF
/*安卓8.1系统dirent.h 不带这个宏定义*/
#define DTTOIF(dirtype) ((dirtype) << 12)
#endif

extern struct nfsdata d;
void LOG(const char *__restrict __fmt, ...);

static void fill_stat(struct stat *stbuf, struct smb2_stat_64 *st)
{
	//stbuf->st_dev          = st->smb2_type;
	stbuf->st_ino = st->smb2_ino;
	stbuf->st_mode = st->st_mode;
	stbuf->st_nlink = st->smb2_nlink;
	stbuf->st_size = st->smb2_size;
	stbuf->st_uid = fuse_get_context()->uid;
	stbuf->st_gid = fuse_get_context()->gid;
	//stbuf->st_rdev         = st->smb2_rdev;
	//stbuf->st_blksize      = st->smb2_blksize;
	//stbuf->st_blocks       = st->smb2_blocks;

#if defined(HAVE_ST_ATIM) || defined(__MINGW32__)
	stbuf->st_atim.tv_sec = st->smb2_atime;
	stbuf->st_atim.tv_nsec = st->smb2_atime_nsec;
	stbuf->st_mtim.tv_sec = st->smb2_mtime;
	stbuf->st_mtim.tv_nsec = st->smb2_mtime_nsec;
	stbuf->st_ctim.tv_sec = st->smb2_ctime;
	stbuf->st_ctim.tv_nsec = st->smb2_ctime_nsec;
#else
	stbuf->st_atime = st->smb2_atime;
	stbuf->st_mtime = st->smb2_mtime;
	stbuf->st_ctime = st->smb2_ctime;
	stbuf->st_atime_nsec = st->smb2_atime_nsec;
	stbuf->st_mtime_nsec = st->smb2_mtime_nsec;
	stbuf->st_ctime_nsec = st->smb2_ctime_nsec;
#endif
}

static int fuse_nfs_getattr(const char *path, struct stat *stbuf)
{
	LOG("fuse_nfs_getattr entered [%s]\n", path);

	struct smb2_stat_64 st;
	memset(&st, 0, sizeof(st));

	int res = smb2_stat(d.v_nfs, path + 1, &st);
	if (res < 0)
		return res;

	fill_stat(stbuf, &st);
	return 0;
}

static int fuse_nfs_fgetattr(const char *path, struct stat *stbuf,
							 struct fuse_file_info *fi)
{
	LOG("fuse_nfs_fgetattr entered [%s]\n", path);

	struct smb2_stat_64 st;
	memset(&st, 0, sizeof(st));

	int res = smb2_fstat(d.v_nfs, (void *)fi->fh, &st);
	if (res < 0)
		return res;

	fill_stat(stbuf, &st);
	return 0;
}

static int fuse_nfs_access(const char *path, int mask)
{
	return 0;
}

static int fuse_nfs_readlink(const char *path, char *buf, size_t size)
{
	LOG("fuse_nfs_readlink entered [%s]\n", path);
	int res = smb2_readlink(d.v_nfs, path + 1, buf, size - 1);
	if (res < 0)
	{
		printf("Error: %s (%s)\n", smb2_get_error(d.v_nfs), strerror(-errno));
		return res;
	}
	buf[res] = 0;
	return 0;
}

static int fuse_nfs_opendir(const char *path, struct fuse_file_info *fi)
{
	LOG("fuse_nfs_opendir entered [%s]\n", path);
	struct smb2dir *nfsdir;

	nfsdir = smb2_opendir(d.v_nfs, path + 1);
	if (!nfsdir)
	{
		printf("smb2_opendir failed. %s\n", smb2_get_error(d.v_nfs));
		return -errno;
	}
	fi->fh = (uint64_t)nfsdir;
	return 0;
}

static int fuse_nfs_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
							off_t offset, struct fuse_file_info *fi)
{
	LOG("fuse_nfs_readdir entered [%s]\n", path);

	if (offset != smb2_telldir(d.v_nfs, (void *)fi->fh))
		smb2_seekdir(d.v_nfs, (void *)fi->fh, offset);

	struct stat st;
	memset(&st, 0, sizeof(st));
	struct smb2dirent *nfsdirent;
	while ((nfsdirent = smb2_readdir(d.v_nfs, (void *)fi->fh)))
	{
		st.st_ino = nfsdirent->st.smb2_ino;
		switch (nfsdirent->st.smb2_type)
		{
		case SMB2_TYPE_LINK:
			st.st_mode = DTTOIF(DT_LNK);
			break;
			break;
		case SMB2_TYPE_DIRECTORY:
			st.st_mode = DTTOIF(DT_DIR);
			break;
		case SMB2_TYPE_FILE:
		default:
			st.st_mode = DTTOIF(DT_REG);
			break;
		}

		if (filler(buf, nfsdirent->name, &st, smb2_telldir(d.v_nfs, (void *)fi->fh)))
			break;
	}
	return 0;
}

static int fuse_nfs_releasedir(const char *path, struct fuse_file_info *fi)
{
	smb2_closedir(d.v_nfs, (void *)fi->fh);
	return 0;
}

static int fuse_nfs_mkdir(const char *path, mode_t mode)
{
	LOG("fuse_nfs_mkdir entered [%s]\n", path);
	int res = smb2_mkdir(d.v_nfs, path + 1);
	if (res < 0)
		return res;

	return 0;
}

static int fuse_nfs_unlink(const char *path)
{
	LOG("fuse_nfs_unlink entered [%s]\n", path);
	int res = smb2_unlink(d.v_nfs, path + 1);
	if (res < 0)
		return res;

	return 0;
}

static int fuse_nfs_rmdir(const char *path)
{
	LOG("fuse_nfs_mknod entered [%s]\n", path);
	int res = smb2_rmdir(d.v_nfs, path + 1);
	if (res < 0)
		return res;

	return 0;
}

// static int fuse_nfs_symlink(const char *from, const char *to)
// {
// 	return 0;
// }

static int fuse_nfs_rename(const char *from, const char *to)
{
	LOG("fuse_nfs_rename entered [%s -> %s]\n", from, to);
	int res = smb2_rename(d.v_nfs, from + 1, to + 1);
	if (res < 0)
		return res;

	return 0;
}

// static int xmp_link(const char *from, const char *to)
// {
// 	return 0;
// }

static int fuse_nfs_chmod(const char *path, mode_t mode)
{
	return 0;
}

static int fuse_nfs_chown(const char *path, uid_t uid, gid_t gid)
{
	return 0;
}

static int fuse_nfs_truncate(const char *path, off_t size)
{
	LOG("fuse_nfs_truncate entered [%s]\n", path);
	int res = smb2_truncate(d.v_nfs, path + 1, size);
	if (res < 0)
		return res;

	return 0;
}

static int fuse_nfs_ftruncate(const char *path, off_t size,
							  struct fuse_file_info *fi)
{
	LOG("fuse_nfs_ftruncate entered [%s]\n", path);
	int res = smb2_ftruncate(d.v_nfs, (void *)fi->fh, size);
	if (res < 0)
		return res;

	return 0;
}

static int fuse_nfs_utimens(const char *path, const struct timespec ts[2])
{
	return 0;
}

static int fuse_nfs_create(const char *path, mode_t mode, struct fuse_file_info *fi)
{
	LOG("fuse_nfs_create entered [%s]\n", path);
	uint64_t rp = (uint64_t)smb2_open(d.v_nfs, path + 1, fi->flags);
	if (!rp)
		return -errno;

	fi->fh = rp;
	return 0;
}

static int fuse_nfs_open(const char *path, struct fuse_file_info *fi)
{
	LOG("fuse_nfs_open entered [%s]\n", path);
	uint64_t rp = (uint64_t)smb2_open(d.v_nfs, path + 1, fi->flags);
	if (!rp)
		return -errno;

	fi->fh = rp;
	return 0;
}

static int fuse_nfs_read(const char *path, char *buf, size_t size,
						 off_t offset, struct fuse_file_info *fi)
{
	LOG("fuse_nfs_read entered [%s]\n", path);
	int res = smb2_pread(d.v_nfs, (void *)fi->fh, (void *)buf, size, offset);
	return res;
}

static int fuse_nfs_write(const char *path, const char *buf, size_t size,
						  off_t offset, struct fuse_file_info *fi)
{
	LOG("fuse_nfs_write entered [%s]\n", path);
	int res = smb2_pwrite(d.v_nfs, (void *)fi->fh, (void *)buf, size, offset);
	return res;
}

//The 'f_frsize', 'f_favail', 'f_fsid' and 'f_flag' fields are ignored
static int fuse_nfs_statfs(const char *path, struct statvfs *stbuf)
{
	LOG("fuse_nfs_statfs entered [%s]\n", path);
	struct smb2_statvfs smb2svfs;
	memset(&smb2svfs, 0, sizeof(smb2svfs));

	int res = smb2_statvfs(d.v_nfs, path + 1, &smb2svfs);
	if (res < 0)
		return res;

	stbuf->f_bsize = smb2svfs.f_bsize;
	//stbuf->f_frsize = smb2svfs.f_frsize;
	//stbuf->f_fsid = smb2svfs.f_fsid;
	//stbuf->f_flag = smb2svfs.f_flag;
	stbuf->f_namemax = smb2svfs.f_namemax;
	stbuf->f_blocks = smb2svfs.f_blocks;
	stbuf->f_bfree = smb2svfs.f_bfree;
	stbuf->f_bavail = smb2svfs.f_bavail;
	stbuf->f_files = smb2svfs.f_files;
	stbuf->f_ffree = smb2svfs.f_ffree;
	//stbuf->f_favail = smb2svfs.f_favail;

	return res;
}

static int fuse_nfs_flush(const char *path, struct fuse_file_info *fi)
{
	LOG("fuse_nfs_flush entered [%s]\n", path);
	int res = smb2_close(d.v_nfs, smb2_dupfh((void *)fi->fh));
	if (res < 0)
		return res;

	return 0;
}

static int fuse_nfs_release(const char *path, struct fuse_file_info *fi)
{
	LOG("fuse_nfs_release entered [%s]\n", path);
	smb2_close(d.v_nfs, (void *)fi->fh);
	return 0;
}

static int fuse_nfs_fsync(const char *path, int isdatasync, struct fuse_file_info *fi)
{
	LOG("fuse_nfs_fsync entered [%s]\n", path);
	int res = smb2_fsync(d.v_nfs, (void *)fi->fh);
	if (res < 0)
		return res;

	return 0;
}

static int fuse_nfs_setxattr(const char *path, const char *name, const char *value,
							 size_t size, int flags)
{
	return 0;
}

extern struct fuse_operations nfs_oper;
static void set_oper_smb()
{
	memset(&nfs_oper,0,sizeof(struct fuse_operations));

	nfs_oper.getattr = fuse_nfs_getattr;
	nfs_oper.fgetattr = fuse_nfs_fgetattr;
	nfs_oper.access = fuse_nfs_access;
	nfs_oper.readlink = fuse_nfs_readlink;
	nfs_oper.opendir = fuse_nfs_opendir;
	nfs_oper.readdir = fuse_nfs_readdir;
	nfs_oper.releasedir = fuse_nfs_releasedir;
	nfs_oper.mknod = NULL,
	nfs_oper.mkdir = fuse_nfs_mkdir;
	nfs_oper.symlink = NULL;
	nfs_oper.unlink = fuse_nfs_unlink;
	nfs_oper.rmdir = fuse_nfs_rmdir;
	nfs_oper.rename = fuse_nfs_rename;
	nfs_oper.link = NULL;
	nfs_oper.chmod = fuse_nfs_chmod,
	nfs_oper.chown = fuse_nfs_chown,
	nfs_oper.truncate = fuse_nfs_truncate;
	nfs_oper.ftruncate = fuse_nfs_ftruncate;

	nfs_oper.utimens = fuse_nfs_utimens;
	nfs_oper.create = fuse_nfs_create;
	nfs_oper.open = fuse_nfs_open;
	nfs_oper.read = fuse_nfs_read;
	nfs_oper.write = fuse_nfs_write;

	nfs_oper.statfs = fuse_nfs_statfs;
	nfs_oper.flush = fuse_nfs_flush;
	nfs_oper.release = fuse_nfs_release;
	nfs_oper.fsync = fuse_nfs_fsync;
	nfs_oper.setxattr = fuse_nfs_setxattr;
}

static void destroy()
{
	if (d.v_urls)
		smb2_destroy_url(d.v_urls);
	if (d.v_nfs)
	{
		smb2_disconnect_share(d.v_nfs);
		smb2_destroy_context(d.v_nfs);
	}
}

int _env_init_smb(struct nfsdata *_d, struct fuse_args *args)
{
	int res = 0;

	if (!(_d->v_nfs = smb2_init_context()))
	{
		fprintf(stderr, "Failed to init context\n");
		res = -3;
		goto out_free;
	}

	if (!(_d->v_urls = smb2_parse_url(_d->v_nfs, _d->fsname)))
	{
		fprintf(stderr, "Failed to parse url : %s\n", smb2_get_error(d.v_nfs));
		res = -4;
		goto out_free;
	}
	
	smb2_set_security_mode(d.v_nfs, SMB2_NEGOTIATE_SIGNING_ENABLED);
	if (smb2_connect_share(d.v_nfs, d.smburls->server, d.smburls->share, d.smburls->user))
	{
		fprintf(stderr, "Failed to mount nfs share : %s\n", smb2_get_error(d.v_nfs));
		res = -5;
		goto out_free;
	}

	set_oper_smb();
	_d->destory = destroy;
out_free:
	return res;
}