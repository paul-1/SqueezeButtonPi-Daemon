#!/bin/sh
# SPDX-License-Identifier: BSD-3-Clause

EXTNAM=pcp-sbpd
TMPDIR=/tmp/$EXTNAM
TCZINFO=$EXTNAM.tcz.info

rm -rf $TMPDIR

echo "Building sbpd for pCP"

case $(find /lib | grep ld-linux) in
    *armhf*)
       export CFLAGS="-Os -pipe -march=armv6zk -mtune=arm1176jzf-s -mfpu=vfp"
       export CXXFLAGS="-Os -pipe -fno-exceptions -fno-rtti -march=armv6zk -mtune=arm1176jzf-s -mfpu=vfp"
       BIT32="linux32"
    ;;
    *aarch64*)
       export CFLAGS="-Os -pipe -march=armv8-a+crc -mtune=cortex-a72"
       export CXXFLAGS="-Os -pipe -fno-exceptions -fno-rtti -march=armv8-a+crc -mtune=cortex-a72"
       BIT32=""
    ;;
esac

export PKG_CONFIG_PATH=/usr/local/lib/pkgconfig:/usr/lib/pkgconfig

make clean
make

SRCDIR=$(pwd)

mkdir -p $TMPDIR/usr/local/bin
mkdir -p $TMPDIR/usr/local/share/$TMPDIR
cp sbpd $TMPDIR/usr/local/bin/
cp sbpd_commands.cfg $TMPDIR/usr/local/share/$TMPDIR/
cp LICENSE $TMPDIR/usr/local/share/$TMPDIR/
cd $TMPDIR
find * -not -type d > $EXTNAM.tcz.list
cd ..
mksquashfs $EXTNAM $EXTNAM.tcz -b 16384
md5sum $EXTNAM.tcz > $EXTNAM.tcz.md5.txt

cd $EXTNAM
mv ../$EXTNAM.tcz* .

echo -e "Title:          $EXTNAM.tcz" > $TCZINFO
echo -e "Description:    SqueezeButtonPi." >> $TCZINFO
echo -e "Version:        $(grep VERSION $SRCDIR/sbpd.h | awk -F' ' '{printf "%s", $3}')" >> $TCZINFO
echo -e "Authors:        Joerg Schwieder, Paul Hermann" >> $TCZINFO
echo -e "Original-site:  https://github.com/coolio107/SqueezeButtonPi-Daemon" >> $TCZINFO
echo -e "Copying-policy: BSD-3-Clause" >> $TCZINFO
echo -e "Size:           $(ls -lk $EXTNAM.tcz | awk '{print $5}')" >> $TCZINFO
echo -e "Extension_by:   piCorePlayer team: http://www.picoreplayer.org/" >> $TCZINFO
echo -e "Change-log:     2017/02/17 Initial version" >> $TCZINFO
echo -e "                2018/04/10 Add short/long button press and config file suppurt" >> $TCZINFO
echo -e "Current:        Update to Version 4.0.0 - Migrate to kernel gpio events" >> $TCZINFO
