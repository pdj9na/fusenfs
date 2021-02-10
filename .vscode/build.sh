#!/bin/bash

. ../libfuse/.vscode/build_util.sh

__ACFILES_="configure.ac\
 .vscode/build.sh"

__AMFILES_=" Makefile.am\
 doc/Makefile.am\
 fuse/Makefile.am"

fun_changes "$__ACFILES_ $__AMFILES_" -f

# 删除之前运行 configure 的状态，使再次运行 configure 能够立即应用新的参数
rm -f config.status
if fun_isChangeFromMulti configure.ac; then
	type autoreconf &>/dev/null && autoreconf -vi
fi

#if fun_isChangeFromMulti configure.ac;then
if fun_isChangeFromMulti "$__ACFILES_"; then
	export LDFLAGS="-L$(readlink -f ../libfuse/lib/.libs)\
 -L$(readlink -f ../libnfs/lib/.libs)\
 -L$(readlink -f ../libsmb2/lib/.libs)"

	export CPPFLAGS="-I$(readlink -f ../libfuse/include)\
 -I$(readlink -f ../libnfs/include)\
 -I$(readlink -f ../libsmb2/include)"

	# 参数不能使用 local 变量，否则AC_ARG_ENABLE 不能解析参数
	#使目标执行程序执行时不输出： unused DT entry: type 0xf arg 0x1ee
	_args="--disable-rpath"

	if type busybox &>/dev/null && test `busybox uname -o` = Android ||
	`uname -m` = aarch64 || `uname -m` = aarch;then
		export CONFIG_SHELL=/system/bin/sh
		
		#https://blog.csdn.net/abcdu1/article/details/86083295
		#如果库目录存在动态库文件，就会默认加载动态库，并无视Makefile.am
		#	中LIBADD显式指定静态库，如“-l:libandroid_support.a”
		#只有库目录不存在动态库文件，才会加载静态库，且不需要设置 LD_LIBRARY_PATH
		# 否则加载动态库需要设置 LD_LIBRARY_PATH
		# 目标库使用configure选项 --disable-shared 禁止生成动态库
		#export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:`readlink -f ../libandroid_support/lib/.libs`
		export LDFLAGS=$LDFLAGS" -L`readlink -f ../libandroid_support/lib/.libs`"
		export CPPFLAGS=$CPPFLAGS" -I`readlink -f ../libandroid_support/include`"

		test `uname -m` = aarch && _args="--target=aarch-linux-android "$_args
		test `uname -m` = aarch64 && _args="--target=aarch64-linux-android "$_args
	else :
		#_args=$_args" --enable-asan"
	fi
	#_args=$_args" --enable-debug"
	#_args=$_args" --enable-staticlink" #" --enable-staticlinkfuse"

	sh ./configure $_args
fi

#fun_whereMakeClean "configure.ac $__AMFILES_"
fun_whereMakeClean "$__ACFILES_ $__AMFILES_"

make -j4

find fuse/.libs -name *.so* -exec rm {} \;

ln -s ../../../libnfs/lib/.libs/libnfs.so.13 fuse/.libs/libnfs.so.13
ln -s ../../../libfuse/lib/.libs/libfuse.so.2 fuse/.libs/libfuse.so.2
ln -s ../../../libfuse/lib/.libs/libulockmgr.so.1 fuse/.libs/libulockmgr.so.1
ln -s ../../../libsmb2/lib/.libs/libsmb2.so.3 fuse/.libs/libsmb2.so.3

# 对于ln支持relation选项-r 的,可以采用以下方式：
# ln -sr ../libnfs/lib/.libs/libnfs.so.13 fuse/.libs/libnfs.so.13

if test ! -x fuse/.libs/fusenfs -a -f fuse/fusenfs; then
	rm -f fuse/.libs/fusenfs
	ln -s ../fusenfs fuse/.libs/fusenfs
fi

if test ! -x fuse/.libs/fuse_nfs -a -f fuse/fuse_nfs; then
	rm -f fuse/.libs/fuse_nfs
	ln -s ../fuse_nfs fuse/.libs/fuse_nfs
fi
