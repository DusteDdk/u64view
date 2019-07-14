#!/bin/bash

V=$1

if [ -z $V ]
then
  echo Need version.
  exit 1
fi

# Windows
win=u64view-windows_$V
mkdir -p $win/docs

cp windows/*.txt windows/*.md LICENSE $win/docs/
cp windows/*.exe $win/

zip -r -9 $win.zip $win
rm -R $win

# Linux
lin=u64view-linux_$V
mkdir $lin

cp README.md $lin
cp u64view $lin
cp LICENSE $lin
tar Jcf $lin.tar.xz $lin
rm -R $lin
