#!/bin/bash
# build on the BIGR cluster

# pam and pam-devel are not currently installed on the cluster
mkdir extra-lib/
cd extra-lib/
wget http://mirror.nucleus.be/scientific/6x/x86_64/os/Packages/pam-1.1.1-20.el6.x86_64.rpm
wget http://mirror.nucleus.be/scientific/6x/x86_64/os/Packages/pam-devel-1.1.1-20.el6.x86_64.rpm
rpm2cpio pam-1.1.1-20.el6.x86_64.rpm | cpio -ivdm
rpm2cpio pam-devel-1.1.1-20.el6.x86_64.rpm | cpio -ivdm
cd ../

extra_lib_dir=$(pwd)/extra-lib
export LIBRARY_PATH="$LIBRARY_PATH:${extra_lib_dir}/usr/lib64/"
export LD_LIBRARY_PATH="$LD_LIBRARY_PATH:${extra_lib_dir}/usr/lib64/"
export C_INCLUDE_PATH="$C_INCLUDE_PATH:${extra_lib_dir}/usr/include/"

./aimk -only-depend -no-java -no-jni -no-secure -no-dump
scripts/zerodepend
./aimk -no-java -no-jni -no-secure -only-core -no-dump depend
./aimk -no-java -no-jni -no-secure -only-core -no-dump

