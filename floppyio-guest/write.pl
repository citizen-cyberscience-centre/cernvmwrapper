#!/usr/bin/perl
#######################################################################
#  Hypervisor-Virtual machine bi-directional communication
#  through floppy disk.
#######################################################################
#
#  This script implements the writing part for the guest-side.
#  Use the FloppyIO Class from the hypervisor-side to receive the data.
#
#  This script reads data from STDIN and writes error messages to STDOUT,
#  so you can use it like this:
#
#  echo "System ready" | ./write.sh
#
#  or like this:
#
#  cat /var/log/my.log | ./write.sh 2>/dev/null
#
#======================================================================
#  
#  Here is the layout of the floppy disk image (Example of 28k):
#
#  +-----------------+------------------------------------------------+
#  | 0x0000 - 0x37FF |  Hypervisor -> Guest Buffer                    |
#  | 0x3800 - 0x6FFE |  Guest -> Hypervisor Buffer                    |
#  |     0x6FFF      |  "Data available for guest" flag byte          |
#  |     0x7000      |  "Data available for hypervisor" flag byte     |
#  +-----------------+------------------------------------------------+
#
#======================================================================
#
# Created on November 24, 2011, 12:30 PM
# Author: Ioannis Charalampidis <ioannis.charalampidis AT cern DOT ch>
#
#######################################################################

use strict;
use warnings;

# ==[ CONFIGURATION ]====================
my $FLOPPY = "/dev/fd0";
my $FLOPPY_SIZE = 28672;
# =======================================

# Calculate buffer positions
my $OUT_SIZE=$FLOPPY_SIZE/2-1; my $OUT_OFS=$OUT_SIZE;

# Try to open file for input
open FD, "+<$FLOPPY" or die $!;
binmode FD;
seek FD, $OUT_OFS, 0;

# Process STDIN
my $dLenSent=0;
while (<STDIN>) {
    my $data = $_;
    my $dLen = length $data;
    if ($dLenSent + $dLen >= $OUT_SIZE-1) {
        my $leftLen = $OUT_SIZE-$dLenSent-1;
        $data = substr $data, 0, $leftLen;
        print FD $data;
        print STDERR "Reached EOF, cutting $leftLen out of $dLen\n";
        last;
    } else {
        print FD $data;
    }
    $dLenSent += $dLen;
}

# When done write zero
print FD "\0";

# Notify server that are data in buffer
# (Server should then clear this byte when it's read)
seek FD, $FLOPPY_SIZE-1,0; # -2=out (client), -1=in (server) (Control bytes)
print FD "\x01";

# Close FD when done
close FD;
