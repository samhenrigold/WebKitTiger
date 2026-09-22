#!/bin/sh
# Tiger box watchdog: if the local ssh daemon stops greeting, the machine has wedged the
# way it did on 2026-09-21 (pings fine, Finder fine, sshd and the Sharing pane hung).
# Three consecutive failures, 30 s apart, and it reboots. Runs from launchd every 30 s.
STATE=/var/run/sshd-watchdog.fails
# A banner within 8 s means sshd is alive. nc on 10.4 has no -w banner read; use perl.
if perl -e 'use IO::Socket::INET; $s=IO::Socket::INET->new(PeerAddr=>"127.0.0.1:22",Timeout=>8) or exit 1; $SIG{ALRM}=sub{exit 1}; alarm 8; $l=<$s>; exit(($l=~/^SSH-/)?0:1)'; then
    rm -f "$STATE"; exit 0
fi
n=$(( $(cat "$STATE" 2>/dev/null || echo 0) + 1 )); echo $n > "$STATE"
logger -t sshd-watchdog "sshd not greeting, failure $n/3"
if [ "$n" -ge 3 ]; then
    logger -t sshd-watchdog "rebooting"
    rm -f "$STATE"
    sync; /sbin/reboot
fi
