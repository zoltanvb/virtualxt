#!/bin/sh

APP_DIR="$TRAVIS_BUILD_DIR/tools/package/appimage/virtualxt-appimage-build"
rm -rf $APP_DIR VirtualXT-x86_64.AppImage

cp -r "$TRAVIS_BUILD_DIR/tools/package/appimage/virtualxt-appimage" $APP_DIR
mkdir -p $APP_DIR/lib/x86_64-linux-gnu $APP_DIR/bios

# Should locate this in some way.
cp /usr/lib/x86_64-linux-gnu/libSDL2-2.0.so.0 $APP_DIR/lib/x86_64-linux-gnu/
cp /usr/lib/x86_64-linux-gnu/libsndio.so.6.1 $APP_DIR/lib/x86_64-linux-gnu/

cp virtualxt $APP_DIR/
cp bios/pcxtbios.bin $APP_DIR/bios/
cp doc/icon/icon.png $APP_DIR/virtualxt-icon.png

curl -L -o apptool.AppImage https://github.com/AppImage/AppImageKit/releases/download/continuous/appimagetool-x86_64.AppImage
chmod +x apptool.AppImage

./apptool.AppImage $APP_DIR

rm apptool.AppImage
rm -rf $APP_DIR