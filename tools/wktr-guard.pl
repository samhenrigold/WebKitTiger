#!/usr/bin/perl
# On the box: run a command (WebKitTestRunner) in its own process group, with our stdin/
# stdout, and kill the whole group -- WKTR and the web, network and GPU processes it
# launched -- when our ssh session goes away or we are signalled. perl 5.8.6.
use strict;
use POSIX qw(setsid WNOHANG);
my $pid = fork;
die "fork: $!" unless defined $pid;
if (!$pid) { setsid(); exec @ARGV; POSIX::_exit(127); }
my $kill = sub { kill 'KILL', -$pid; exit 1 };
$SIG{$_} = $kill for qw(INT TERM HUP PIPE);
for (;;) {
    my $w = waitpid($pid, WNOHANG);
    if ($w == $pid) { my $st = $?; kill 'KILL', -$pid; exit($st & 127 ? 128 + ($st & 127) : $st >> 8); }
    $kill->() if getppid() == 1;
    select(undef, undef, undef, 0.5);
}
