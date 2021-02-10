/*
  fusenfs module: offset paths with a base directory
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

#include <nfsc/libnfs.h>

#ifdef WIN32
#include <winsock2.h>
#include <win32/win32_compat.h>
#endif

struct nfsdata d;
int _env_init_smb(struct nfsdata *_d, struct fuse_args *args);
int _env_init_bind(struct nfsdata *_d, struct fuse_args *args);

void LOG(const char *__restrict __fmt, ...)
{
	if (!d.logfile)
		return;

	FILE *fh = fopen(d.logfile, "a+");
	if (!fh)
		return;

	time_t t = time(NULL);
	static char tmp[256];
	strftime(tmp, sizeof(tmp), "%T", localtime(&t));
	fprintf(fh, "[nfs] %s ", tmp);

	va_list args;
	va_start(args, __fmt);
	vfprintf(fh, __fmt, args);
	va_end(args);

	fclose(fh);
}

#ifdef __MINGW32__
gid_t getgid()
{
	if (d.custom_gid == -1U)
		return 65534;
	return d.custom_gid;
}

uid_t getuid()
{
	if (d.custom_uid == -1U)
		return 65534;
	return d.custom_uid;
}
#endif

static uid_t map_uid(uid_t possible_uid)
{
	if (d.custom_uid != -1U && possible_uid == d.custom_uid)
		return fuse_get_context()->uid;
	return possible_uid;
}

static gid_t map_gid(gid_t possible_gid)
{
	if (d.custom_gid != -1U && possible_gid == d.custom_gid)
		return fuse_get_context()->gid;
	return possible_gid;
}

static uid_t map_reverse_uid(uid_t possible_uid)
{
	if (d.custom_uid != -1U && possible_uid == getuid())
		return d.custom_uid;
	return possible_uid;
}

static gid_t map_reverse_gid(gid_t possible_gid)
{
	if (d.custom_gid != -1U && possible_gid == getgid())
		return d.custom_gid;
	return possible_gid;
}

static void wait_for_nfs_reply(struct nfs_context *nfs, struct sync_cb_data *cb_data)
{
	struct pollfd pfd;
	int revents;
	int ret;

	while (!cb_data->is_finished)
	{
		pfd.fd = nfs_get_fd(nfs);
		pfd.events = nfs_which_events(nfs);
		pfd.revents = 0;

		ret = poll(&pfd, 1, 100);
		if (ret < 0)
			revents = -1;
		else
			revents = pfd.revents;

		ret = nfs_service(nfs, revents);
		if (ret < 0)
		{
			cb_data->status = -EIO;
			break;
		}
	}
}

static void generic_cb(int status, struct nfs_context *nfs, void *data, void *private_data)
{
	struct sync_cb_data *cb_data = private_data;

	cb_data->is_finished = 1;
	cb_data->status = status;
}

/* Update the rpc credentials to the current user unless
 * have are overriding the credentials via url arguments.
 */
static void update_rpc_credentials()
{
	struct fuse_context *ctx = fuse_get_context();
	uid_t uid = d.custom_uid == -1U ? ctx->uid : d.custom_uid;
	gid_t gid = d.custom_gid == -1U ? ctx->gid : d.custom_gid;
	nfs_set_uid_gid(d.nfs, uid, gid);
}

static void stat64_cb(int status, struct nfs_context *nfs, void *data, void *private_data)
{
	struct sync_cb_data *cb_data = private_data;
	LOG("stat64_cb status:%d\n", status);

	cb_data->status = status;
	if (status >= 0)
		memcpy(cb_data->return_data, data, sizeof(struct nfs_stat_64));
	cb_data->is_finished = 1;
}

static int fuse_nfs_getattr(const char *path, struct stat *stbuf)
{
	struct nfs_stat_64 st;
	struct sync_cb_data cb_data;
	int ret;

	LOG("fuse_nfs_getattr entered [%s]\n", path);

	memset(&cb_data, 0, sizeof(struct sync_cb_data));
	cb_data.return_data = &st;

	ret = nfs_lstat64_async(d.nfs, path, stat64_cb, &cb_data);
	if (ret < 0)
	{
		return ret;
	}
	wait_for_nfs_reply(d.nfs, &cb_data);

	stbuf->st_dev = st.nfs_dev;
	stbuf->st_ino = st.nfs_ino;
	stbuf->st_mode = st.nfs_mode;
	stbuf->st_nlink = st.nfs_nlink;
	stbuf->st_uid = map_uid(st.nfs_uid);
	stbuf->st_gid = map_gid(st.nfs_gid);
	stbuf->st_rdev = st.nfs_rdev;
	stbuf->st_size = st.nfs_size;
	stbuf->st_blksize = st.nfs_blksize;
	stbuf->st_blocks = st.nfs_blocks;

#if defined(HAVE_ST_ATIM) || defined(__MINGW32__)
	stbuf->st_atim.tv_sec = st.nfs_atime;
	stbuf->st_atim.tv_nsec = st.nfs_atime_nsec;
	stbuf->st_mtim.tv_sec = st.nfs_mtime;
	stbuf->st_mtim.tv_nsec = st.nfs_mtime_nsec;
	stbuf->st_ctim.tv_sec = st.nfs_ctime;
	stbuf->st_ctim.tv_nsec = st.nfs_ctime_nsec;
#else
	stbuf->st_atime = st.nfs_atime;
	stbuf->st_mtime = st.nfs_mtime;
	stbuf->st_ctime = st.nfs_ctime;
	stbuf->st_atime_nsec = st.nfs_atime_nsec;
	stbuf->st_mtime_nsec = st.nfs_mtime_nsec;
	stbuf->st_ctime_nsec = st.nfs_ctime_nsec;
#endif
	return cb_data.status;
}

static void readdir_cb(int status, struct nfs_context *nfs, void *data, void *private_data)
{
	struct sync_cb_data *cb_data = private_data;

	cb_data->is_finished = 1;
	cb_data->status = status;

	LOG("readdir_cb status:%d\n", status);

	if (status < 0)
	{
		return;
	}
	cb_data->return_data = data;
}

static int fuse_nfs_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
							off_t offset, struct fuse_file_info *fi)
{
	struct nfsdir *nfsdir;
	struct nfsdirent *nfsdirent;
	struct sync_cb_data cb_data;
	int ret;

	LOG("fuse_nfs_readdir entered [%s]\n", path);

	memset(&cb_data, 0, sizeof(struct sync_cb_data));

	ret = nfs_opendir_async(d.nfs, path, readdir_cb, &cb_data);
	if (ret < 0)
	{
		return ret;
	}
	wait_for_nfs_reply(d.nfs, &cb_data);

	nfsdir = cb_data.return_data;
	while ((nfsdirent = nfs_readdir(d.nfs, nfsdir)) != NULL)
	{
		filler(buf, nfsdirent->name, NULL, 0);
	}

	nfs_closedir(d.nfs, nfsdir);

	return cb_data.status;
}

static void readlink_cb(int status, struct nfs_context *nfs, void *data, void *private_data)
{
	struct sync_cb_data *cb_data = private_data;

	cb_data->is_finished = 1;
	cb_data->status = status;

	if (status < 0)
	{
		return;
	}
	strncat(cb_data->return_data, data, cb_data->max_size);
}

static int fuse_nfs_readlink(const char *path, char *buf, size_t size)
{
	struct sync_cb_data cb_data;
	int ret;

	LOG("fuse_nfs_readlink entered [%s]\n", path);

	memset(&cb_data, 0, sizeof(struct sync_cb_data));
	cb_data.return_data = buf;
	cb_data.max_size = size;

	ret = nfs_readlink_async(d.nfs, path, readlink_cb, &cb_data);
	if (ret < 0)
	{
		return ret;
	}
	wait_for_nfs_reply(d.nfs, &cb_data);

	return cb_data.status;
}

static void open_cb(int status, struct nfs_context *nfs, void *data, void *private_data)
{
	struct sync_cb_data *cb_data = private_data;

	cb_data->is_finished = 1;
	cb_data->status = status;

	LOG("open_cb status:%d\n", status);

	if (status < 0)
	{
		return;
	}
	cb_data->return_data = data;
}

static int fuse_nfs_open(const char *path, struct fuse_file_info *fi)
{
	struct sync_cb_data cb_data;
	int ret;

	LOG("fuse_nfs_open entered [%s]\n", path);

	memset(&cb_data, 0, sizeof(struct sync_cb_data));

	fi->fh = 0;
	ret = nfs_open_async(d.nfs, path, fi->flags, open_cb, &cb_data);
	if (ret < 0)
	{
		return ret;
	}
	wait_for_nfs_reply(d.nfs, &cb_data);

	fi->fh = (uint64_t)cb_data.return_data;

	return cb_data.status;
}

static int fuse_nfs_release(const char *path, struct fuse_file_info *fi)
{
	struct sync_cb_data cb_data;
	struct nfsfh *nfsfh = (struct nfsfh *)fi->fh;

	memset(&cb_data, 0, sizeof(struct sync_cb_data));

	nfs_close_async(d.nfs, nfsfh, generic_cb, &cb_data);
	wait_for_nfs_reply(d.nfs, &cb_data);

	return 0;
}

static void read_cb(int status, struct nfs_context *nfs, void *data, void *private_data)
{
	struct sync_cb_data *cb_data = private_data;

	cb_data->is_finished = 1;
	cb_data->status = status;

	if (status < 0)
	{
		return;
	}
	memcpy(cb_data->return_data, data, status);
}

static int fuse_nfs_read(const char *path, char *buf, size_t size,
						 off_t offset, struct fuse_file_info *fi)
{
	struct nfsfh *nfsfh = (struct nfsfh *)fi->fh;
	struct sync_cb_data cb_data;
	int ret;

	LOG("fuse_nfs_read entered [%s]\n", path);

	memset(&cb_data, 0, sizeof(struct sync_cb_data));
	cb_data.return_data = buf;

	ret = nfs_pread_async(d.nfs, nfsfh, offset, size, read_cb, &cb_data);
	if (ret < 0)
	{
		return ret;
	}
	wait_for_nfs_reply(d.nfs, &cb_data);

	return cb_data.status;
}

static int fuse_nfs_write(const char *path, const char *buf, size_t size,
						  off_t offset, struct fuse_file_info *fi)
{
	struct nfsfh *nfsfh = (struct nfsfh *)fi->fh;
	struct sync_cb_data cb_data;
	int ret;

	LOG("fuse_nfs_write entered [%s]\n", path);

	memset(&cb_data, 0, sizeof(struct sync_cb_data));

	ret = nfs_pwrite_async(d.nfs, nfsfh, offset, size, buf,
						   generic_cb, &cb_data);
	if (ret < 0)
	{
		return ret;
	}
	wait_for_nfs_reply(d.nfs, &cb_data);

	return cb_data.status;
}

static int fuse_nfs_create(const char *path, mode_t mode, struct fuse_file_info *fi)
{
	struct sync_cb_data cb_data;
	int ret = 0;

	LOG("fuse_nfs_create entered [%s]\n", path);

	memset(&cb_data, 0, sizeof(struct sync_cb_data));

	ret = nfs_creat_async(d.nfs, path, mode, open_cb, &cb_data);
	if (ret < 0)
	{
		return ret;
	}
	wait_for_nfs_reply(d.nfs, &cb_data);

	fi->fh = (uint64_t)cb_data.return_data;

	return cb_data.status;
}

static int fuse_nfs_utime(const char *path, struct utimbuf *times)
{
	struct sync_cb_data cb_data;
	int ret;

	LOG("fuse_nfs_utime entered [%s]\n", path);

	memset(&cb_data, 0, sizeof(struct sync_cb_data));

	ret = nfs_utime_async(d.nfs, path, times, generic_cb, &cb_data);
	if (ret < 0)
	{
		LOG("fuse_nfs_utime returned %d. %s\n", ret, nfs_get_error(d.v_nfs));
		return ret;
	}
	wait_for_nfs_reply(d.nfs, &cb_data);

	return cb_data.status;
}

static int fuse_nfs_unlink(const char *path)
{
	struct sync_cb_data cb_data;
	int ret;

	LOG("fuse_nfs_unlink entered [%s]\n", path);

	memset(&cb_data, 0, sizeof(struct sync_cb_data));

	ret = nfs_unlink_async(d.nfs, path, generic_cb, &cb_data);
	if (ret < 0)
	{
		return ret;
	}
	wait_for_nfs_reply(d.nfs, &cb_data);

	return cb_data.status;
}

static int fuse_nfs_rmdir(const char *path)
{
	struct sync_cb_data cb_data;
	int ret;

	LOG("fuse_nfs_rmdir entered [%s]\n", path);

	memset(&cb_data, 0, sizeof(struct sync_cb_data));

	ret = nfs_rmdir_async(d.nfs, path, generic_cb, &cb_data);
	if (ret < 0)
	{
		return ret;
	}
	wait_for_nfs_reply(d.nfs, &cb_data);

	return cb_data.status;
}

static int fuse_nfs_mkdir(const char *path, mode_t mode)
{
	struct sync_cb_data cb_data;
	int ret;

	LOG("fuse_nfs_mkdir entered [%s]\n", path);

	memset(&cb_data, 0, sizeof(struct sync_cb_data));

	ret = nfs_mkdir_async(d.nfs, path, generic_cb, &cb_data);
	if (ret < 0)
	{
		return ret;
	}
	wait_for_nfs_reply(d.nfs, &cb_data);

	cb_data.is_finished = 0;

	ret = nfs_chmod_async(d.nfs, path, mode, generic_cb, &cb_data);
	if (ret < 0)
	{
		return ret;
	}
	wait_for_nfs_reply(d.nfs, &cb_data);

	return cb_data.status;
}

static int fuse_nfs_mknod(const char *path, mode_t mode, dev_t rdev)
{
	struct sync_cb_data cb_data;
	int ret;

	LOG("fuse_nfs_mknod entered [%s]\n", path);

	memset(&cb_data, 0, sizeof(struct sync_cb_data));

	ret = nfs_mknod_async(d.nfs, path, mode, rdev, generic_cb, &cb_data);
	if (ret < 0)
	{
		return ret;
	}
	wait_for_nfs_reply(d.nfs, &cb_data);

	return cb_data.status;
}

static int fuse_nfs_symlink(const char *from, const char *to)
{
	struct sync_cb_data cb_data;
	int ret;

	LOG("fuse_nfs_symlink entered [%s -> %s]\n", from, to);

	memset(&cb_data, 0, sizeof(struct sync_cb_data));

	ret = nfs_symlink_async(d.nfs, from, to, generic_cb, &cb_data);
	if (ret < 0)
	{
		return ret;
	}
	wait_for_nfs_reply(d.nfs, &cb_data);

	return cb_data.status;
}

static int fuse_nfs_rename(const char *from, const char *to)
{
	struct sync_cb_data cb_data;
	int ret;

	LOG("fuse_nfs_rename entered [%s -> %s]\n", from, to);

	memset(&cb_data, 0, sizeof(struct sync_cb_data));

	ret = nfs_rename_async(d.nfs, from, to, generic_cb, &cb_data);
	if (ret < 0)
	{
		return ret;
	}
	wait_for_nfs_reply(d.nfs, &cb_data);

	return cb_data.status;
}

static int fuse_nfs_link(const char *from, const char *to)
{
	struct sync_cb_data cb_data;
	int ret;

	LOG("fuse_nfs_link entered [%s -> %s]\n", from, to);

	memset(&cb_data, 0, sizeof(struct sync_cb_data));

	ret = nfs_link_async(d.nfs, from, to, generic_cb, &cb_data);
	if (ret < 0)
	{
		return ret;
	}
	wait_for_nfs_reply(d.nfs, &cb_data);

	return cb_data.status;
}

static int fuse_nfs_chmod(const char *path, mode_t mode)
{
	struct sync_cb_data cb_data;
	int ret;

	LOG("fuse_nfs_chmod entered [%s]\n", path);

	memset(&cb_data, 0, sizeof(struct sync_cb_data));

	ret = nfs_chmod_async(d.nfs, path, mode, generic_cb, &cb_data);
	if (ret < 0)
	{
		return ret;
	}
	wait_for_nfs_reply(d.nfs, &cb_data);

	return cb_data.status;
}

static int fuse_nfs_chown(const char *path, uid_t uid, gid_t gid)
{
	struct sync_cb_data cb_data;
	int ret;

	LOG("fuse_nfs_chown entered [%s]\n", path);

	memset(&cb_data, 0, sizeof(struct sync_cb_data));

	ret = nfs_chown_async(d.nfs, path,
						  map_reverse_uid(uid), map_reverse_gid(gid),
						  generic_cb, &cb_data);
	if (ret < 0)
	{
		return ret;
	}
	wait_for_nfs_reply(d.nfs, &cb_data);

	return cb_data.status;
}

static int fuse_nfs_truncate(const char *path, off_t size)
{
	struct sync_cb_data cb_data;
	int ret;

	LOG("fuse_nfs_truncate entered [%s]\n", path);

	memset(&cb_data, 0, sizeof(struct sync_cb_data));

	ret = nfs_truncate_async(d.nfs, path, size, generic_cb, &cb_data);
	if (ret < 0)
	{
		return ret;
	}
	wait_for_nfs_reply(d.nfs, &cb_data);

	return cb_data.status;
}

static int fuse_nfs_fsync(const char *path, int isdatasync,
						  struct fuse_file_info *fi)
{
	struct nfsfh *nfsfh = (struct nfsfh *)fi->fh;
	struct sync_cb_data cb_data;
	int ret;

	LOG("fuse_nfs_fsync entered [%s]\n", path);

	memset(&cb_data, 0, sizeof(struct sync_cb_data));

	ret = nfs_fsync_async(d.nfs, nfsfh, generic_cb, &cb_data);
	if (ret < 0)
	{
		return ret;
	}
	wait_for_nfs_reply(d.nfs, &cb_data);

	return cb_data.status;
}

static void statvfs_cb(int status, struct nfs_context *nfs, void *data, void *private_data)
{
	struct sync_cb_data *cb_data = private_data;

	cb_data->is_finished = 1;
	cb_data->status = status;

	if (status < 0)
	{
		return;
	}
	memcpy(cb_data->return_data, data, sizeof(struct statvfs));
}

//The 'f_frsize', 'f_favail', 'f_fsid' and 'f_flag' fields are ignored
static int
fuse_nfs_statfs(const char *path, struct statvfs *stbuf)
{
	int ret;
	struct statvfs svfs;

	struct sync_cb_data cb_data;

	LOG("fuse_nfs_statfs entered [%s]\n", path);

	memset(&cb_data, 0, sizeof(struct sync_cb_data));
	cb_data.return_data = &svfs;

	ret = nfs_statvfs_async(d.nfs, path, statvfs_cb, &cb_data);
	if (ret < 0)
	{
		return ret;
	}
	wait_for_nfs_reply(d.nfs, &cb_data);

	stbuf->f_bsize = svfs.f_bsize;
	//stbuf->f_frsize = svfs.f_frsize;
	//stbuf->f_fsid = svfs.f_fsid;
	//stbuf->f_flag = svfs.f_flag;
	stbuf->f_namemax = svfs.f_namemax;
	stbuf->f_blocks = svfs.f_blocks;
	stbuf->f_bfree = svfs.f_bfree;
	stbuf->f_bavail = svfs.f_bavail;
	stbuf->f_files = svfs.f_files;
	stbuf->f_ffree = svfs.f_ffree;
	//stbuf->f_favail = svfs.f_favail;

	return cb_data.status;
}

static void destroy()
{
	if (d.v_urls)
		nfs_destroy_url(d.v_urls);
	if (d.v_nfs)
		nfs_destroy_context(d.v_nfs);
}

struct fuse_operations nfs_oper = {
	.chmod = fuse_nfs_chmod,
	.chown = fuse_nfs_chown,
	.create = fuse_nfs_create,
	.fsync = fuse_nfs_fsync,
	.getattr = fuse_nfs_getattr,
	.link = fuse_nfs_link,
	.mkdir = fuse_nfs_mkdir,
	.mknod = fuse_nfs_mknod,
	.open = fuse_nfs_open,
	.read = fuse_nfs_read,
	.readdir = fuse_nfs_readdir,
	.readlink = fuse_nfs_readlink,
	.release = fuse_nfs_release,
	.rmdir = fuse_nfs_rmdir,
	.unlink = fuse_nfs_unlink,
	.utime = fuse_nfs_utime,
	.rename = fuse_nfs_rename,
	.symlink = fuse_nfs_symlink,
	.truncate = fuse_nfs_truncate,
	.write = fuse_nfs_write,
	.statfs = fuse_nfs_statfs,
};

//from lib/helper.c
enum
{
	KEY_HELP,
	//KEY_HELP_NOHEADER,
	//KEY_VERSION,
};

static void nfs_help()
{
	fprintf(stderr,
			R"(
<fusebind>
fuse option [fsname] format:
       a directory path,add as module subdir,e.g:
       fsname=/path/... Resolve to modules=subdir,subdir=/path/...
 ==将执行文件放入/usr/sbin （也许还支持环境变量PATH的其他某些路径）下面，可通过以下方式挂载：
     >mount 不使用fstab (经过测试不支持该挂载方式！）：
     	mount -t fuse.fusebind -o allow_other -ofsname=/mnt/fusesubdir_src /mnt/fusesubdir_dest
     >mount 结合fstab  ：
     	fstab格式：-ofsname=/mnt/fusesubdir_src /mnt/fusesubdir_dest fuse.fusebind allow_other	0	0
     	mount格式：mount -a [-T fstab path]
     		   mount -t fuse.fusebind /mnt/fusesubdir_dest [-T fstab path]
     经过测试：自定义的路径哪怕添加到环境变量PATH中也找不到执行文件，所以必须放到/usr/sbin下面
 ==执行文件未在系统默认环境变量PATH的路径中，而仅添加执行文件的路径到环境变量PATH中，，可通过以下方式挂载：
     >mount.fuse：
     	mount.fuse type#[source] destination [-t type] [-o opt[,opts...]]
     	mount.fuse -ofsname=/mnt/fusesubdir_src /mnt/fusesubdir_dest -t fusebind -o allow_other

<fusenfs>
Custom options:
    -o logfile=logfile	   log file path
fuse option [fsname] format:
      a URL-FORMAT, fsname=url
    	Libnfs uses RFC2224 style URLs extended with libnfs specific url arguments
    	  some minor extensions.

    	The basic syntax of these URLs is :
    	  nfs://<server|ipv4|ipv6>/path[?arg=val[&arg=val]*]

    	Arguments supported by libnfs are :
    	 tcp-syncnt=<int>	: Number of SYNs to send during the session establish
    				  before failing setting up the tcp connection to the
    				  server.
    	 uid=<int>		: UID value to use when talking to the server.
    				  default it 65534 on Windows and getuid() on unixen.
    	 gid=<int>		: GID value to use when talking to the server.
    				  default it 65534 on Windows and getgid() on unixen.
    	 readahead=<int>	: Enable readahead for files and set the maximum amount
    				  of readahead to <int>.
    	 auto-traverse-mounts=<0|1>
    				: Should libnfs try to traverse across nested mounts
    				  automatically or not. Default is 1 == enabled.
    	 dircache=<0|1>		: Disable/enable directory caching. Enabled by default.
    	 if=<interface>		: Interface name (e.g., eth1) to bind; requires `root`
    	 version=<3|4>		: NFS version to use. Version 3 is the default.

<fusesmb>
Custom options:
    -o logfile=logfile	   log file path
fuse option [fsname] format:
    	The SMB URL format is currently a small subset of the URL format that is
    	defined/used by the Samba project.
    	The goal is to eventually support the full URL format, thus making URLs
    	interchangable between Samba utilities and Libsmb2 but we are not there yet.
	
    	smb://[<domain>;][<user>@]<server>[:<port>]/<share>[/path][?arg=val[&arg=val]*]
	
    	<server> is either a hostname, an IPv4 or an IPv6 address.
	
    	Aruments supported by libsmb2 are :
    	 sec=<mech>    : Mechanism to use to authenticate to the server. Default
    		         is any available mech, but can be overridden by :
    			 krb5: Use Kerberos using credentials from kinit.
    			 krb5cc: Use Kerberos using credentials from credentials
    		                 cache.
    			 ntlmssp : Only use NTLMSSP
    	 vers=<version> : Which SMB version to negotiate:
    	                  2: Negotiate any version of SMB2
    	                  3: Negotiate any version of SMB3
    			  2.02, 2.10, 3.00, 3.02, 3.1.1 : negotiate a specific version.
    			  Default is to negotiate any SMB2 or SMB3 version.
    	  seal          : Enable SMB3 encryption.
    	  sign          : Require SMB2/3 signing.
    	  timeout=<second>
    	                : Timeout in seconds when to cancel a command.
    	                  Default it 0: No timeout.
    	  credentials=<file>
    	                : set credentials file path,format:
    	                  [domain]:username:password
    	  password=<***>: set password
)");
}

static int nfs_opt_proc(void *data, const char *arg, int key, struct fuse_args *outargs)
{
	if (key == KEY_HELP)
		return -1;
	return 1;
}

// const char *v_nfs_get_error()
// {
// 	const char *errc = NULL;
// 	if (d.type == E_FSTYPE_NFS)
// 		errc = nfs_get_error(d.v_nfs);
// 	else if (d.type == E_FSTYPE_SMB)
// 		errc = smb2_get_error(d.v_nfs);
// 	return errc;
// }

static int set_fsname(struct fuse_args *args)
{
	int res = 0;
	char *pre_opt = "-ofsname=";

	size_t mlen = strlen(pre_opt) + strlen(d.fsname) + 1;
	char *_opt = malloc(mlen);
	if (!_opt)
	{
		fprintf(stderr, "fuse: memory allocation failed\n");
		res = -errno;
		goto end;
	}
	//strcpy 复制指针会出错，而strcat不会
	//strcpy(_opt, pre_opt);
	_opt[0] = 0, strcat(_opt, pre_opt);

	char *url2 = strchr(d.fsname, '?');
	//smb://aaa/bbb?password=a&credentials=b
	if (url2)
	{
		++url2, strncat(_opt, d.fsname, strlen(d.fsname) - strlen(url2));

		char *_opt2;
		size_t mlen2, ind = 0;
		struct _arg_t
		{
			char *pre_pwd;
			char *ppass;
			char *mask;
		} * _arg, *_args2[2],
			_args[] = {{.pre_pwd = "password=", .mask = "***"},
					   {.pre_pwd = "credentials=", .mask = "*filepath*"}};
		_args[0].ppass = strstr(url2, _args[0].pre_pwd);
		_args[1].ppass = strstr(url2, _args[1].pre_pwd);

		_args2[0] = _args[0].ppass < _args[1].ppass ? _args : (_args + 1);
		_args2[1] = _args[0].ppass > _args[1].ppass ? _args : (_args + 1);
		goto begin;

	set_pwd:
		if (mlen2 > mlen)
		{
			_opt2 = realloc(_opt, mlen2);
			if (!_opt2)
			{
				fprintf(stderr, "fuse: memory allocation failed\n");
				res = 1;
				goto out_free;
			}
			_opt = _opt2, mlen = mlen2;
		}

		if (ind <= 2)
		{
			strncat(_opt, url2, strlen(url2) - strlen(_arg->ppass) + strlen(_arg->pre_pwd));
			strcat(_opt, _arg->mask), url2 = strchr(_arg->ppass, '&');
		}
		else
			strcat(_opt, url2);
	begin:
		if (ind < 2)
		{
			_arg = _args2[ind++];
			if (_arg->ppass)
			{
				mlen2 = strlen(_opt) + strlen(url2) - strlen(_arg->ppass) +
						strlen(_arg->pre_pwd) + strlen(_arg->mask) + 1;
				goto set_pwd;
			}
			goto begin;
		}
		else if ((ind++) == 2 && url2)
		{
			mlen2 = strlen(_opt) + strlen(url2) + 1;
			goto set_pwd;
		}
	}
	else
		strcat(_opt, d.fsname);

	res = fuse_opt_add_arg(args, _opt);

out_free:
	if (_opt)
		free(_opt);
end:
	return res;
}

static int add_default_subtype(struct fuse_args *args)
{
	int res;
	char *arg_pre = "-osubtype=", *basename = args->argv[0], *pos = strrchr(basename, '/'), *arg;
	if (pos)
		basename = pos + 1;
	arg = malloc(strlen(arg_pre) + strlen(basename) + 1);
	if (!arg)
	{
		fprintf(stderr, "fuse: memory allocation failed\n");
		res = -errno;
		goto end;
	}
	arg[0] = 0, strcat(strcat(arg, arg_pre), basename);
	res = fuse_opt_add_arg(args, arg);
	free(arg);
end:
	return res;
}

int _env_init_nfs(struct nfsdata *_d, struct fuse_args *args)
{
	int res = 0;

	if (!(_d->v_nfs = nfs_init_context()))
	{
		fprintf(stderr, "Failed to init context\n");
		res = -3;
		goto out_free;
	}

	if (!(_d->v_urls = nfs_parse_url_dir(_d->v_nfs, _d->fsname)))
	{
		fprintf(stderr, "Failed to parse url : %s\n", nfs_get_error(d.v_nfs));
		res = -4;
		goto out_free;
	}

	_d->custom_uid = -1U;
	_d->custom_gid = -1U;

	char *url_params = strchr(_d->fsname, '?');
	if (url_params)
	{
		uint32_t _id, *_sid = &_d->custom_uid;
		char *_url2, *__p[] = {"uid=", "gid=", NULL}, **_p = __p;
		do
		{
			if ((_url2 = strstr(url_params, *_p)) &&
				sscanf(_url2 + strlen(*_p), "%u", &_id) > 0)
				*_sid = _id;
		} while (++_sid, *++_p);
	}

	update_rpc_credentials();
	if (nfs_mount(_d->v_nfs, _d->nfsurls->server, _d->nfsurls->path))
	{
		fprintf(stderr, "Failed to mount nfs share : %s\n", nfs_get_error(d.v_nfs));
		res = -5;
		goto out_free;
	}
	_d->destory = destroy;

out_free:
	return res;
}

/* Ubuntu 16.04下测试结果：
NFS>
	单线程下[vers:3]：
		上传平均速度可达2.6MB/s
		下载平均速度可达26MB/s
	多线程下[vers:3]，由于阻塞问题，性能不如单线程，且还可能出现异常：
		上传平均速度可达
		下载平均速度可达
* 当前建议开启单线程运行程序

SMB>
	单线程下[vers:3]：
		上传平均速度可达2.8MB/s
		下载平均速度可达38MB/s
	多线程下[vers:3]，由于阻塞问题，性能不如单线程，且还可能出现异常：
		上传平均速度可达1.2MB/s
		下载平均速度可达1.2MB/s
* 当前建议开启单线程运行程序
*/
int main(int argc, char *argv[])
{
#ifdef DEBUG
	for (int i = 0; i < argc; ++i)
		printf("%s\n", *(argv + i));
#endif
	int res = 0;
	struct fuse_args args = FUSE_ARGS_INIT(argc, argv);

	const struct fuse_opt nfs_opts[] = {
		//[common option] key=KEY_HELP
		FUSE_OPT_KEY("-h", KEY_HELP),
		FUSE_OPT_KEY("--help", KEY_HELP),
		{"fsname=%s", offsetof(struct nfsdata, fsname), 0},
		{"subtype=", offsetof(struct nfsdata, flag_subtype), 1},
		{"-s", offsetof(struct nfsdata, flag_singlethread), 1},
		/*-fusebind-*/
		FUSE_OPT_KEY("modules=", FUSE_OPT_KEY_DISCARD),
		FUSE_OPT_KEY("subdir=", FUSE_OPT_KEY_DISCARD),
		//custom opt:
		{"logfile=%s", offsetof(struct nfsdata, logfile), 0},
		//wait KEEP opt:
		FUSE_OPT_KEY("subtype=", FUSE_OPT_KEY_KEEP),
		FUSE_OPT_KEY("-s", FUSE_OPT_KEY_KEEP),
		FUSE_OPT_END};

	int res2 = fuse_opt_parse(&args, &d, nfs_opts, nfs_opt_proc);
	if (res2 == -1)
		goto show_help;

	if (!d.fsname)
	{
		fprintf(stderr, "The required option parameter 'fsname' is missing\n");
		res = -2;
		goto out_free;
	}

	d.type = E_FSTYPE_INVALID;
	if (strlen(d.fsname) >= 1 && !strncmp(d.fsname, "/", 1))
		d.type = E_FSTYPE_BIND;
	else if (strlen(d.fsname) >= 3)
	{
		if (!strncmp(d.fsname, "nfs", 3))
			d.type = E_FSTYPE_NFS;
		else if (!strncmp(d.fsname, "smb", 3))
			d.type = E_FSTYPE_SMB;
	}

	if (d.type == E_FSTYPE_INVALID)
	{
		fprintf(stderr, "fusenfs: invalid fstype\n");
		res = -2;
		goto out_free;
	}

	d.urllen = strlen(d.fsname);

	if (d.type == E_FSTYPE_NFS)
		res = _env_init_nfs(&d, &args);
	else if (d.type == E_FSTYPE_SMB)
		res = _env_init_smb(&d, &args);
	else if (d.type == E_FSTYPE_BIND)
		res = _env_init_bind(&d, &args);
	if (res < 0)
		goto out_free;

#ifdef WIN32
	WSADATA wsaData;
	WSAStartup(MAKEWORD(2, 2), &wsaData);
#endif

	if (d.type != E_FSTYPE_BIND && set_fsname(&args))
	{
		res = -6;
		goto out_free;
	}
	if (!d.flag_subtype && add_default_subtype(&args))
	{
		res = -7;
		goto out_free;
	}

	//对于NFS和SMB目前始终开启单线程！
	if (!d.flag_singlethread && d.type != E_FSTYPE_BIND &&
		fuse_opt_add_arg(&args, "-s"))
	{
		res = -8;
		goto out_free;
	}

	umask(0);
	LOG("=======================================\n");
	LOG("Starting fuse_main()\n");
show_help:
	res = fuse_main(args.argc, args.argv, &nfs_oper, NULL);
	if (res2 == -1)
	{
		nfs_help();
		res = res2;
	}
out_free:
	if (d.destory)
		d.destory();

	if (args.allocated)
		free(args.argv);
	if (d.fsname)
		free(d.fsname);
	if (d.logfile)
		free(d.logfile);
	//pthread_exit(&res);
	return res;
}
