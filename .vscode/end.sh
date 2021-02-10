#!/bin/bash
sleep 1
# 解除挂载
__file="/mnt/nfs_test /mnt/fusesubdir_dest"
for __i in $__file;do

    fuser -k $__i
    ../libfuse/util/fusermount -uz $__i
done

