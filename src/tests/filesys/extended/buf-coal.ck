# -*- perl -*-
use strict;
use warnings;
use tests::tests;
use tests::random;
check_expected (IGNORE_EXIT_CODES => 1, [<<'EOF']);
(buf-coal) begin
(buf-coal) making temp_file
(buf-coal) creating temp_file
(buf-coal) opening temp_file
(buf-coal) correct number of device writes
(buf-coal) closing temp_file
(buf-coal) end
EOF
pass;