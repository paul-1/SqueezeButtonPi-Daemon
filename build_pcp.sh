#!/bin/sh
# SPDX-License-Identifier: BSD-3-Clause

tce-load -wil compiletc curl-dev libgpiod-dev squashfs-tools

EXTNAM=pcp-sbpd
TMPDIR=/tmp/$EXTNAM
TCZINFO=$EXTNAM.tcz.info
TCZDEP=$EXTNAM.tcz.dep

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
mkdir -p $TMPDIR/usr/local/share/$EXTNAM
cp sbpd $TMPDIR/usr/local/bin/
cp sbpd_commands.cfg $TMPDIR/usr/local/share/$EXTNAM/
cp LICENSE $TMPDIR/usr/local/share/$EXTNAM/
cd $TMPDIR
find * -not -type d > $EXTNAM.tcz.list
cd ..
mksquashfs $EXTNAM $EXTNAM.tcz -b 16384
md5sum $EXTNAM.tcz > $EXTNAM.tcz.md5.txt

cd $EXTNAM
mv ../$EXTNAM.tcz*

echo -e "curl.tcz" > $TCZDEP
echo -e "libgpiod.tcz" > $TCZDEP


echo -e "Title:          $EXTNAM.tcz" > $TCZINFO
echo -e "Description:    SqueezeButtonPi." >> $TCZINFO
echo -e "Version:        $(grep VERSION $SRCDIR/sbpd.h | awk -F' ' '{printf "%s", $3}')" >> $TCZINFO
echo -e "Authors:        Joerg Schwieder, Paul Hermann" >> $TCZINFO
echo -e "Original-site:  https://github.com/coolio107/SqueezeButtonPi-Daemon" >> $TCZINFO
echo -e "Copying-policy: BSD-3-Clause" >> $TCZINFO
echo -e "Size:           $(ls -lk $EXTNAM.tcz | awk '{print $5}')" >> $TCZINFO
echo -e "Extension_by:   piCorePlayer team: http://www.picoreplayer.org/" >> $TCZINFO
echo -e "Change-log:     2017/02/17 Initial version" >> $TCZINFO
echo -e "                2017/03/04 Added short/long button press handling" >> $TCZINFO
echo -e "                2018/04/14 Added configuration-file support and per-button long-press commands" >> $TCZINFO
echo -e "                2018/04/28 Released version 2.0" >> $TCZINFO
echo -e "                2019/02/02 Added previous/next track control via encoder" >> $TCZINFO
echo -e "                2019/09/25 Added pigpio GPIO support and uinput keyboard controls" >> $TCZINFO
echo -e "                2025/02/02 Restored WiringPi support and added piCorePlayer packaging" >> $TCZINFO
echo -e "Current:        2026/11/06 Version 4.0.0: event-driven libgpiod GPIO; Raspberry Pi 5/RP1 support" >> $TCZINFO
