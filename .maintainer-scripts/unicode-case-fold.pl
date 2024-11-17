#!/usr/bin/perl -w
use strict;
use warnings;

# Input data: https://www.unicode.org/Public/UCD/latest/ucd/CaseFolding.txt

my %map;

while (<>) {
    chomp;
    next if /^(#|\s*$)/;
    my($char, $status, $fold, $comment) = split /\s*;\s*/;
    if ($status =~ /^[CS]$/) {
        $comment =~ s/^#\s*//;
        # print "    case 0x$char: return 0x$fold; // [$status] $comment\n";
        $map{hex($char)} = hex($fold);
    }
}

my @valid_code_points = (0..0xD7FF, 0xE000..0x10FFFF);

sub cp_to_str {
  my $cp = shift;
  my $fmt = $cp < 0x10000 ? "\\u%04X" : "\\U%08X";
  return sprintf $fmt, $cp;
}

while (@valid_code_points) {
  my @cps = splice @valid_code_points, 0, 256;
  my $orig;
  my $folded;
  for my $cp (@cps) {
    my $fold = $map{$cp} // $cp;
    $orig .= cp_to_str($cp);
    $folded .= cp_to_str($fold);
  }
  print "    {u8\"$orig\"sv, u8\"$folded\"sv},\n" if $orig ne $folded;
}
