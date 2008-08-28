#!perl

use Test::More;
use Test::NoWarnings;
plan tests => 1;
eval "use Test::Pod::Coverage 1.04";
diag("Test::Pod::Coverage 1.04 required for testing POD coverage") if $@;
#all_pod_coverage_ok();
