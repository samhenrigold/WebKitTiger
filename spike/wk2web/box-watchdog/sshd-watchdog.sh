#!/bin/sh
# Tiger box watchdog. On 10.4 launchd owns port 22 and forks sshd per connection, so
# "connect works, no SSH- banner" means launchd itself is wedged (2026-09-21/22: pings fine,
# Finder fine, sshd and the Sharing pane hung). The first version of this was a launchd
# StartInterval job, which a wedged launchd never runs. Now: one resident loop, started
# once at boot, that never asks launchd for anything again. Three misses 30 s apart and it
# calls reboot -q, the raw syscall path that skips the launchd/process-teardown dance.
fails=0
while :; do
    if perl -e 'use IO::Socket::INET; $SIG{ALRM}=sub{exit 1}; alarm 8; $s=IO::Socket::INET->new(PeerAddr=>"127.0.0.1:22",Timeout=>8) or exit 1; $l=<$s>; exit(($l=~/^SSH-/)?0:1)'; then
        fails=0
    else
        fails=$((fails + 1))
        logger -t sshd-watchdog "sshd not greeting, failure $fails/3" &
        if [ "$fails" -ge 3 ]; then
            logger -t sshd-watchdog "rebooting" &
            sleep 1
            /sbin/reboot -q
        fi
    fi
    sleep 30
done
