use Test::More;

BEGIN {
      plan skip_all => 'This test is only run for the module author'
           unless -d '.git' || $ENV{IS_MAINTAINER};
}
use Test::Kwalitee 'kwalitee_ok';
kwalitee_ok();
done_testing;
