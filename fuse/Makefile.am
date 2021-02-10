AM_CPPFLAGS = -I$(top_srcdir)/include $(WARN_CFLAGS)
bin_PROGRAMS = fuse_nfs fusenfs

#--
fuse_nfs_SOURCES = fuse-nfs.c
if FLAG_STATIC_LINK
fuse_nfs_LDADD = -l:libnfs.a

if FLAG_STATIC_LINKFUSE

if ANDROID
    fuse_nfs_LDADD += -l:libfuse.a
else
    fuse_nfs_LDADD += -lfuse
    fuse_nfs_LDFLAGS = -static
endif

else
fuse_nfs_LDADD += -lfuse
endif

else
fuse_nfs_LDADD = -lnfs -lfuse
endif

#--
fusenfs_SOURCES = fusenfs.c fusesmb.c fusebind.c
if FLAG_STATIC_LINK

fusenfs_LDADD = -l:libnfs.a -l:libsmb2.a

if FLAG_STATIC_LINKFUSE

if ANDROID
    fusenfs_LDADD += -l:libulockmgr.a -l:libfuse.a
else
    fusenfs_LDADD += -l:libulockmgr.a -lfuse
    fusenfs_LDFLAGS = -static
endif

else
fusenfs_LDADD += -lulockmgr -lfuse
endif

else
fusenfs_LDADD = -lnfs -lsmb2 -lulockmgr -lfuse
endif
