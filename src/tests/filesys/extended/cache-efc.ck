# -*- perl -*-
use strict;
use warnings;
use tests::tests;
use tests::random;
check_expected (IGNORE_EXIT_CODES => 1, [<<'EOF']);
(cache-efc) begin
(cache-efc) making temp_file
(cache-efc) creating temp_file
(cache-efc) opening temp_file
(cache-efc) closing temp_file
(cache-efc) clearing cache
(cache-efc) opening temp_file
(cache-efc) closing temp_file
(cache-efc) opening temp_file
(cache-efc) closing temp_file
(cache-efc) New hit rate is higher than old
(cache-efc) end
EOF
pass;