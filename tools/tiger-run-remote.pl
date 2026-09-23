#!/usr/bin/perl
# Tiger Perl 5.8. No Python, timeout, launchd or process-name-based killing.
use strict;
use POSIX qw(setsid);
use Time::HiRes qw(time sleep);
$| = 1;
my ($mode, $dir, $seconds, @command) = @ARGV;
die "bad run directory\n" unless defined($dir) && $dir =~ m{^/Users/shg/wk2/runs/[A-Za-z0-9-]+$};

sub recorded_group {
    my ($directory) = @_;
    open(my $record, '<', "$directory/process-group") or return;
    my $pid = <$record>; close $record;
    chomp $pid;
    return unless $pid =~ /^\d+$/ && $pid > 1;
    my $command = `ps -p $pid -ww -o command`;
    return $pid if index($command, "$directory/bin/") >= 0;
    return;
}

if ($mode eq 'cleanup') {
    my $pid = recorded_group($dir);
    if ($pid) {
        kill 'TERM', -$pid;
        sleep 0.5;
        kill 'KILL', -$pid;
    }
    exit 0;
}
die "bad mode or duration\n" unless $mode eq 'run' && defined($seconds) && $seconds =~ /^\d+$/ && $seconds > 0 && @command;
chdir "$dir/bin" or die "chdir: $!\n";
pipe(my $ready_r, my $ready_w) or die "pipe: $!\n";
my $child = fork();
die "fork: $!\n" unless defined $child;
my ($cleaned, $child_status) = (0, 0);
END { cleanup() if $child && !$cleaned; }
if (!$child) {
    close $ready_r;
    setsid() >= 0 or die "setsid: $!\n";
    open(STDOUT, '>', "$dir/app.log") or die "log: $!\n";
    open(STDERR, '>&STDOUT') or die "stderr: $!\n";
    print $ready_w "ready\n";
    close $ready_w;
    exec @command;
    die "exec: $!\n";
}
close $ready_w;
my $ready = <$ready_r>; close $ready_r;
die "child could not create process group\n" unless $ready;
open(my $record, '>', "$dir/process-group") or die "record: $!\n";
print $record "$child\n"; close $record;
sub cleanup {
    return if $cleaned++;
    # Leave the leader unreaped until signaling; its PID cannot be reused, even
    # after an early app exit. Helpers inherit the new group through fork/exec.
    kill 'TERM', -$child;
    sleep 0.5;
    kill 'KILL', -$child;
    waitpid($child, 0);
    $child_status = $?;
    unlink "$dir/process-group";
}
$SIG{HUP} = $SIG{INT} = $SIG{TERM} = sub { cleanup(); exit 130; };
$SIG{ALRM} = sub { cleanup(); die "run watchdog expired\n"; };
alarm($seconds + 20);
my $start = time;
my $capture_at = $seconds > 3 ? $seconds - 3 : $seconds / 2;
my ($captured, $reported, $capture_status) = (0, 0, 0);
while (time - $start < $seconds + 3) {
    my $elapsed = time - $start;
    if (!$reported && $elapsed >= 8) {
        $reported = 1;
        print "== procs at 8s\n";
        for (`ps -axww -o pid,rss,%cpu,command`) { print if index($_, "$dir/bin/") >= 0; }
        for (`netstat -m`) { print if /clusters in use/; }
    }
    if (!$captured && $elapsed >= $capture_at) {
        $captured = 1;
        $capture_status = system('screencapture', '-x', "$dir/shot.png");
    }
    sleep 0.1;
}
cleanup();
alarm 0;
print "== after (this run's process group cleaned)\n";
for (`netstat -m`) { print if /clusters in use|denied/; }
exit 1 if $capture_status;
# A process still running at the deadline is deliberately terminated. Otherwise
# preserve an early exec/app failure instead of calling a blank screenshot success.
my $signal = $child_status & 127;
exit($signal && $signal != 9 && $signal != 15 ? 128 + $signal : $child_status >> 8);
