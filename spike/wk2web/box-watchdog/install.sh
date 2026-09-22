#!/bin/sh
# Run ON THE BOX as an admin (asks for the password once):
#   sudo sh /tmp/box-watchdog/install.sh
set -e
# retire any earlier install, whatever it was labelled
for old in /Library/LaunchDaemons/*.sshd-watchdog.plist; do
    [ -f "$old" ] && { launchctl unload "$old" 2>/dev/null || true; rm -f "$old"; }
done
cp /tmp/box-watchdog/sshd-watchdog.sh /Library/Scripts/sshd-watchdog.sh
chmod 755 /Library/Scripts/sshd-watchdog.sh
cp /tmp/box-watchdog/local.sshd-watchdog.plist /Library/LaunchDaemons/
chown root:wheel /Library/LaunchDaemons/local.sshd-watchdog.plist /Library/Scripts/sshd-watchdog.sh
launchctl load /Library/LaunchDaemons/local.sshd-watchdog.plist
echo "watchdog installed: resident loop, reboot -q after 90 s without an sshd greeting"
