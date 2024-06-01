# -*- perl -*-
use strict;
use warnings;
use tests::tests;
check_expected ([<<'EOF']);
(seek-simple) begin
(seek-simple) temp.txt
(seek-simple) Bytes read should be: 5
(seek-simple) Bytes read beyond boundary should be: 0
(seek-simple) end
seek-simple: exit(0)
EOF
pass;
