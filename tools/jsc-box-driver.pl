#!/usr/bin/perl
# Runs on the box (perl 5.8.6), from tools/run-jsc-tests-box.sh. Executes the test_script_N
# files run-jsc-stress-tests generated, N at a time, nice'd, each in its own process group
# with a hard timeout, and writes one "P|F|T index seconds name" line per test.
#   perl jsc-box-driver.pl <runner dir> <list file> <results file> <jobs> <timeout secs>
# Box rules: kills every child group on exit/signal/orphaning, pauses while load > 4.
use strict;
use POSIX qw(setsid WNOHANG);
use Time::HiRes qw(time sleep);

my ($dir, $list, $results, $jobs, $timeout) = @ARGV;
chdir $dir or die "cd $dir: $!";
open my $lf, '<', $list or die "$list: $!";
my @queue = map { chomp; $_ } grep { /\S/ } <$lf>;
close $lf;
open my $out, '>>', $results or die "$results: $!";
select((select($out), $| = 1)[0]);
open my $log, '>>', "driver.log" or die;
select((select($log), $| = 1)[0]);

my %running;    # pid -> [script, start]
sub killall { kill 'KILL', -$_ for keys %running; }
$SIG{$_} = sub { killall(); exit 1 } for qw(INT TERM HUP PIPE);
END { killall() }

sub name_of {
    my ($script) = @_;
    open my $f, '<', $script or return $script;
    my $l = <$f>;
    close $f;
    $l =~ /^echo Running (\S+)/ ? $1 : $script;
}

sub load1 { my $l = `sysctl -n vm.loadavg`; $l =~ /([\d.]+)/; $1 || 0 }

my $total = @queue;
my $done = 0;
while (@queue || %running) {
    exit 1 if getppid() == 1;    # the ssh session died: do not outlive it
    while (@queue && keys(%running) < $jobs) {
        if (load1() > 4) {
            print $log "load > 4, pausing\n";
            sleep 30;
            last;
        }
        my $script = shift @queue;
        my $pid = fork;
        die "fork: $!" unless defined $pid;
        if (!$pid) {
            setsid();
            open STDOUT, '>', "$script.log";
            open STDERR, '>&STDOUT';
            exec 'nice', '-n', '10', 'sh', $script;
            POSIX::_exit(127);
        }
        $running{$pid} = [$script, time];
    }
    my $pid = waitpid(-1, WNOHANG);
    if ($pid > 0 && $running{$pid}) {
        my ($script, $start) = @{delete $running{$pid}};
        my $st = $? >> 8;
        # The script records its own verdict in test_results; we only need P/F from it.
        my $verdict = 'F';
        if (open my $l, '<', "$script.log") {
            local $/;
            my $text = <$l>;
            $verdict = 'P' unless $text =~ /^FAIL: /m;
        }
        $verdict = 'F' if $st;
        printf $out "%s %s %.1f %s\n", $verdict, $script, time - $start, name_of($script);
        $done++;
        print $log "$done/$total\n" unless $done % 100;
        next;
    }
    for my $p (keys %running) {
        my ($script, $start) = @{$running{$p}};
        next if time - $start < $timeout;
        kill 'KILL', -$p;
        waitpid($p, 0);
        delete $running{$p};
        printf $out "T %s %.1f %s\n", $script, time - $start, name_of($script);
        $done++;
    }
    sleep 0.05;
}
print $log "done $done/$total\n";
