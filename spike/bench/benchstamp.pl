#!/usr/bin/perl
# Box-side wrapper for tools/bench.sh. stage-app.sh runs `env $APP_ENV ./TigerBrowser2 URL
# SECS ...`; bench.sh puts this script last in APP_ENV, so env runs it with the app's argv.
# It runs the app with stdout+stderr into a pipe and prints every line with the seconds
# since launch (the format tools/tiger-profile.py reads), and at BENCH_PS_AT (default
# "30 88") seconds it adds the process table as "BENCH-PS <t>: <ps line>" lines.
use strict;
use Time::HiRes qw(time sleep);
$| = 1;
my $t0 = time;
pipe(my $r, my $w) or die "pipe: $!";
my $app = fork();
if (!$app) {
    close $r;
    open(STDOUT, '>&', $w); open(STDERR, '>&', $w);
    exec(@ARGV) or die "exec $ARGV[0]: $!";
}
my $ps = fork();
if (!$ps) {
    close $r;
    select($w); $| = 1;
    for my $at (split ' ', ($ENV{BENCH_PS_AT} || '30 88')) {
        my $d = $t0 + $at - time;
        sleep($d) if $d > 0;
        last if !kill(0, $app);
        for (`ps -axo pid,ppid,rss,vsz,%cpu,time,command`) {
            next unless m{Tiger|tigeraudio};
            next if m{grep|benchstamp|Applications/TigerBrowser};
            print "BENCH-PS $at: $_";
        }
        my $load = `sysctl -n vm.loadavg`; chomp $load;
        print "BENCH-PS $at: load $load\n";
    }
    exit 0;
}
close $w;
while (my $line = <$r>) {
    printf "%8.3f %s", time - $t0, $line;
}
waitpid($app, 0);
kill 'TERM', $ps;
