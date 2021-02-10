AC_PREREQ(2.50)
AC_INIT([fuse-nfs], [1.0.0], [ronniesahlberg@gmail.com])
AC_CONFIG_MACRO_DIR([m4])
AC_CANONICAL_TARGET
AC_CONFIG_HEADERS(config.h)
AM_INIT_AUTOMAKE([-Wall foreign])

case $target_os in
     *android)	arch_sub=android;;
esac

#debug options support
AC_ARG_ENABLE([debug],
    [AS_HELP_STRING([--enable-debug],[debug program by full level(default is no)])])

AC_ARG_ENABLE([release],
    [AS_HELP_STRING([--enable-release],[release program(default is no)])])

AC_ARG_ENABLE([asan],
    [AS_HELP_STRING([--enable-asan],[asan program by full level(default is no)])])

AC_ARG_ENABLE([staticlink],
    [AS_HELP_STRING([--enable-staticlink],[static link all not fuse libraries to program(default is no)])])

AC_ARG_ENABLE([staticlinkfuse],
    [AS_HELP_STRING([--enable-staticlinkfuse],[static link fuse libraries to program(default is no)])])

# Check if rpath is disabled
AC_MSG_CHECKING(whether to use rpath)
AC_ARG_ENABLE(rpath,
	[AS_HELP_STRING([--disable-rpath],[Patches libtool to not use rpath in the libraries produced.])])
AC_MSG_RESULT($enable_rpath)

AM_CONDITIONAL(FLAG_STATIC_LINK,test x$enable_staticlink = xyes)
AM_CONDITIONAL(FLAG_STATIC_LINKFUSE,test x$enable_staticlinkfuse = xyes)

if test x$enable_staticlinkfuse = xyes; then
AC_DEFINE(FLAG_STATIC_LINKFUSE,[ ],[ ])
fi

dnl  We always want 64 bit file offsets
CPPFLAGS="-D_FILE_OFFSET_BITS=64 $CPPFLAGS"

if test "$ac_env_CFLAGS_set" != set; then
    if test x$enable_release = xyes;then
	    CFLAGS="-O2"
    elif test x$enable_debug = xyes;then
        CFLAGS="-g3 -ggdb -O0";
        AC_DEFINE(DEBUG,[ ],[ ])
    else
	    CFLAGS="-g -O2"
    fi

    if test x$enable_asan = xyes;then
    CFLAGS="$CFLAGS -fsanitize=address -fsanitize-recover=all\
 -fno-omit-frame-pointer -fno-stack-protector"
    fi

	if test "$arch_sub" = android;then
		CFLAGS="$CFLAGS -fPIE -pie"
	fi
fi

echo "------------CFLAGS:$CFLAGS"
echo "------------CPPFLAGS:$CPPFLAGS"



m4_pattern_allow([AM_PROG_AR])
AM_PROG_AR
AC_PROG_CC
AC_PROG_LIBTOOL
AM_PROG_CC_C_O

# Patch libtool to not use rpath if requested.
#
AC_CONFIG_COMMANDS([libtool-rpath-patch],
[if test "$libtool_patch_use_rpath" = "no"; then
   echo config.status: patching libtool to not use rpath
   sed < libtool > libtool-2 's/^hardcode_libdir_flag_spec.*$'/'hardcode_libdir_flag_spec=" -D__LIBTOOL_NO_RPATH__ "/'
   mv libtool-2 libtool
   chmod 755 libtool
fi],
[libtool_patch_use_rpath=$enable_rpath])

if test "$ac_cv_prog_gcc" = yes; then
   WARN_CFLAGS="-Wall -W -Wno-unused-parameter -Wno-stringop-overflow"
fi
AC_SUBST(WARN_CFLAGS)

AM_CONDITIONAL(ANDROID,test "$arch_sub" = android)

AC_HEADER_ASSERT
AC_CHECK_HEADER([fuse.h], [], [AC_MSG_ERROR([fuse.h is missing. You need to install libfuse-dev]);exit 1], [])
AC_CHECK_HEADER([nfsc/libnfs.h], [], [AC_MSG_ERROR([libnfs.h is missing. You need to install libnfs-dev]);exit 1], [])
AC_CHECK_HEADER([smb2/libsmb2.h], [], [AC_MSG_ERROR([libsmb2.h is missing. You need to install libsmb2-dev]);exit 1], [])

AC_CACHE_CHECK([for st_atim support],libiscsi_cv_HAVE_ST_ATIM,[
AC_TRY_COMPILE([
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>],
[struct stat st; st.st_atim.tv_sec = 0;],
libiscsi_cv_HAVE_ST_ATIM=yes,libiscsi_cv_HAVE_ST_ATIM=no)])
if test x"$libiscsi_cv_HAVE_ST_ATIM" = x"yes"; then
    AC_DEFINE(HAVE_ST_ATIM,1,[Whether we have st_atim support])
fi

AC_SEARCH_LIBS([fuse_get_context], [fuse dokanfuse1.dll dokanfuse2.dll], [], [
  AC_MSG_ERROR([fuse library unavailable]);exit 1
])
echo '---------------------'$LIBS
LIBS=

AC_CHECK_FUNCS([fork setxattr fdatasync splice vmsplice utimensat])
AC_CHECK_FUNCS([posix_fallocate])
AC_CHECK_FUNC(gethostbyname,[SOCKETS_AVAILABLE=1],[exit 1])
AS_IF([test "$SOCKETS_AVAILABLE" = ""],[
  OLD_LIBS=$LIBS
  LIBS+=" -lws2"
  AC_LINK_IFELSE([AC_LANG_PROGRAM([[#include <winsock2.h>]], [[gethostbyname ("");]])], [SOCKETS_AVAILABLE=1], [LIBS=$OLD_LIBS])
])
AS_IF([test "$SOCKETS_AVAILABLE" = ""],[
  OLD_LIBS=$LIBS
  LIBS+=" -lws2_32"
  AC_LINK_IFELSE([AC_LANG_PROGRAM([[#include <winsock2.h>]], [[gethostbyname ("");]])], [SOCKETS_AVAILABLE=1], [LIBS=$OLD_LIBS])
])

#output
AC_CONFIG_FILES([Makefile]
                [doc/Makefile]
                [fuse/Makefile]
               )

AC_OUTPUT([fuse-nfs.pc])
