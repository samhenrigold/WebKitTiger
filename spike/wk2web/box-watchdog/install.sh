#!/bin/sh
# Run ON THE BOX as an admin (asks for the password once):
#   sudo sh /tmp/box-watchdog/install.sh
set -e
cp /tmp/box-watchdog/sshd-watchdog.sh /Library/Scripts/sshd-watchdog.sh
chmod 755 /Library/Scripts/sshd-watchdog.sh
cp /tmp/box-watchdog/ai.portola.sshd-watchdog.plist /Library/LaunchDaemons/
chown root:wheel /Library/LaunchDaemons/ai.portola.sshd-watchdog.plist /Library/Scripts/sshd-watchdog.sh
launchctl load /Library/LaunchDaemons/ai.portola.sshd-watchdog.plist
echo "watchdog installed: reboots after 90 s without an sshd greeting"
