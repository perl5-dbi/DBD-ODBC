use Test::More;
use Test::NoWarnings;
# I need to include Test::NoWarnings to pass cpants but that adds a test
# so that prevents you using all_pod_files_ok as that sets the Test::More plan.
# 
eval "use Test::Pod 1.00 tests => 4";
plan skip_all => "Test::Pod 1.00 required for testing POD" if $@;
my @pods = all_pod_files();
foreach my $pod (@pods) {
    next if $pod !~ /(ODBC.pm)|(FAQ.pm)|(Changes.pm)/;
    pod_file_ok($pod);
}
