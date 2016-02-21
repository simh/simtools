#!/usr/pkg/bin/perl
#
# Checker script - checks if a bunch of macro11 object files have
# undefined symbols left over.
#
# Uses the dumpobj tool to parse the object files.
#
use strict;
#use Data::Dumper;

my %symtab;

# Symbols are stored in the symbol table.
# They have 2 fields:
# - a list of definitions (hopefully 1 entry long)
# - a list of references

sub read_object_file {
    my $fn = shift;

    if ($fn =~ /\.def$/i) {
	open OBJ, "<", $fn;
    } else {
	open OBJ, "dumpobj '".$fn."' |";
    }

    my $gsd = 0;
    my $line = <OBJ>;

    while (defined $line) {
	# print $line;
	if ($line =~ /^GSD:/) {
	    $gsd = 1;
	    # print "gsd = 1\n";
	} elsif ($line =~ /^ENDGSD/) {
	    $gsd = 0;
	    # print "gsd = 0\n";
	} elsif ($gsd) {
	    #print $line;
	    #  GLOBAL AT$CDT=0 REF ABS flags=100
	    if ($line =~ /GLOBAL ([A-Z0-9.\$]*)=[0-9]* (.*)/) {
		my $symbol = $1;
		my $flags = $2;

		#print "flags: $flags\n";
		my $key = 'refs';
		if ($flags =~ /\bDEF\b/) {
		    $key = 'defs';
		}

		if (!defined $symtab{$symbol}) {
		     $symtab{$symbol} = {
			 'defs' => [],
			 'refs' => []
		     };
		}

		push @{$symtab{$symbol}->{$key}}, $fn;
	    }
	}
	$line = <OBJ>;
    }

    close OBJ;
}

# Read all object files

foreach my $fn (@ARGV) {
    read_object_file($fn);

    # print Dumper(\%symtab);
}

# Check which symbols have no definition

print "Symbols with no definition:\n";
my $undefs = 0;

foreach my $key (sort keys %symtab) {
    #print $key, " : ", Dumper($entry);
    my $entry = %symtab{$key};

    my @defs = @{$entry->{defs}};
    #print (@defs), "\n";
    #print scalar(@defs), "\n";

    if (scalar(@defs) == 0) {
	$undefs++;
	print $key, ":";
	for my $ref (@{$entry->{refs}}) {
	    print " ", $ref;
	}
	print "\n";
    }
}

if ($undefs == 0) {
    print "(none)\n";
}
