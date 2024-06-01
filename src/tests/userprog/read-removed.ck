# -*- perl -*-
use strict;
use warnings;
use tests::tests;
check_expected ([<<'EOF']);
(read-removed) begin
(read-removed) temp.txt
(read-removed) temp.txt
(read-removed) Bytes read for removed file: 10
(read-removed) end
read-removed: exit(0)
EOF
pass;
