#!/bin/sh

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

echo -e "Title:\t\t$EXTNAM.tcz" > $TCZINFO
echo -e "Description:\tSqueezeButtonPi." >> $TCZINFO
echo -e "Version:\t$(grep VERSION $SRCDIR/sbpd.h | awk -F' ' '{printf "%s", $3}')" >> $TCZINFO
echo -e "Authors:\tJoerg Schwieder" >> $TCZINFO
echo -e "Original-site:\thttps://github.com/coolio107/SqueezeButtonPi-Daemon" >> $TCZINFO
echo -e "Copying-policy:\tGPLv3" >> $TCZINFO
echo -e "Size:\t\t$(ls -lk $EXTNAM.tcz | awk '{print $5}')" >> $TCZINFO
echo -e "Extension_by:\tpiCorePlayer team: http://www.picoreplayer.org/" >> $TCZINFO
echo -e "\t\tCompiled for piCore 15.x" >> $TCZINFO
