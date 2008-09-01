use Test::More;

my $has_test_nowarnings = 1;
eval "require Test::NoWarnings";
$has_test_nowarnings = undef if $@;

END {
    Test::NoWarnings::had_no_warnings()
          if ($has_test_nowarnings);
}

# I need to include Test::NoWarnings to pass cpants but that adds a test
# so that prevents you using all_pod_files_ok as that sets the Test::More plan.
# 
my $tests = 3;
$tests += 1 if $has_test_nowarnings;
eval "use Test::Pod 1.00 tests => $tests";
plan skip_all => "Test::Pod 1.00 required for testing POD" if $@;
my @pods = all_pod_files();
foreach my $pod (@pods) {
    next if $pod !~ /(ODBC.pm)|(FAQ.pm)|(Changes.pm)/;
    pod_file_ok($pod);
}
