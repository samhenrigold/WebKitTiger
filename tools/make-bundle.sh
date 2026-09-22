#!/bin/sh
# Assemble a double-clickable TigerBrowser.app from the main build dirs and install it on the
# box. A Finder launch (unlike ssh) gives the app a pasteboard server, so copy/paste is real.
#   tools/make-bundle.sh            # build bundle into build/TigerBrowser.app and rsync to the box
#   INSTALL=0 tools/make-bundle.sh  # bundle only
set -e
WKT=/Users/shg/Developer/WebKitTiger
APP=$WKT/build/TigerBrowser.app
UI=$WKT/build/tiger-ui-port/bin
WEB=$WKT/build/tiger-web-port/bin
GPU=$WKT/build/tiger-gpu/bin
for f in $UI/TigerBrowser2 $WEB/TigerWebProcess $WEB/TigerNetworkProcess $GPU/TigerGPUProcess $WKT/build/tigeraudio32 \
         $WKT/logs/tiger-fonts.json $WKT/deps/src/cacert.pem $WKT/spike/CAHost/Frameworks/QuartzCore.framework; do
    [ -e "$f" ] || { echo "make-bundle: missing $f"; exit 1; }
done
rm -rf "$APP"
mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Resources" "$APP/Contents/Frameworks"
cp $UI/TigerBrowser2 $WEB/TigerWebProcess $WEB/TigerNetworkProcess $GPU/TigerGPUProcess $WKT/build/tigeraudio32 "$APP/Contents/MacOS/"
cp $WKT/logs/tiger-fonts.json $WKT/deps/src/cacert.pem "$APP/Contents/Resources/"
cp -R $WKT/spike/CAHost/Frameworks/QuartzCore.framework "$APP/Contents/Frameworks/"
cat > "$APP/Contents/MacOS/TigerBrowser" <<'SH'
#!/bin/sh
# Launcher: the processes find fonts, the CA bundle and each other through these.
DIR=$(cd "$(dirname "$0")" && pwd)
RES="$DIR/../Resources"
export TIGER_FONT_MANIFEST="$RES/tiger-fonts.json"
export TIGER_CA_BUNDLE="$RES/cacert.pem"
export WEBKIT_TIGER_HELPER_DIR="$DIR"
# TIGER_FAITHFUL=1 in the environment (or a file next to the app) selects the Core Animation path.
[ -f "$RES/faithful" ] && export TIGER_FAITHFUL=1
exec "$DIR/TigerBrowser2" "${TIGER_HOME_URL:-https://x.com/}" > /tmp/TigerBrowser.log 2>&1
SH
chmod +x "$APP/Contents/MacOS/TigerBrowser"
cat > "$APP/Contents/Info.plist" <<'PL'
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
	<key>CFBundleDevelopmentRegion</key>	<string>English</string>
	<key>CFBundleExecutable</key>		<string>TigerBrowser</string>
	<key>CFBundleIdentifier</key>		<string>org.webkittiger.TigerBrowser</string>
	<key>CFBundleInfoDictionaryVersion</key><string>6.0</string>
	<key>CFBundleName</key>			<string>TigerBrowser</string>
	<key>CFBundlePackageType</key>		<string>APPL</string>
	<key>CFBundleSignature</key>		<string>????</string>
	<key>CFBundleShortVersionString</key>	<string>0.1</string>
	<key>CFBundleVersion</key>		<string>BUILDSTAMP</string>
	<key>LSMinimumSystemVersion</key>	<string>10.4</string>
	<key>NSPrincipalClass</key>		<string>NSApplication</string>
</dict>
</plist>
PL
sed -i '' "s/BUILDSTAMP/$(date +%Y%m%d.%H%M)/" "$APP/Contents/Info.plist"
printf 'APPL????' > "$APP/Contents/PkgInfo"
du -sh "$APP" | cut -f1
if [ "${INSTALL:-1}" = 1 ]; then
    ssh tiger-eth 'mkdir -p /Users/shg/Applications'
    rsync -rtl -z --delete --partial --inplace --bwlimit=20000 "$APP" tiger-eth:/Users/shg/Applications/
    echo "installed: /Users/shg/Applications/TigerBrowser.app on the box (also drag it to the Dock)"
fi
