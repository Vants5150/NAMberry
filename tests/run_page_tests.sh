#!/bin/bash
# Tests of the web page (Node.js): every test_*.js loads src/control_page.h and checks one function of the page
cd "$(dirname "$0")/.." || exit 1
python3 - <<'EOF'
import re
t = open('src/control_page.h', encoding='utf-8').read()
m = re.search(r'<script>(.*)</script>', t, re.S)
open('/tmp/page_check.js', 'w').write(m.group(1).replace('@@TOKEN@@', 'abc'))
EOF
node --check /tmp/page_check.js && echo "page script: syntax ok"
for t in tests/test_*.js; do r=$(node $t src/control_page.h 2>&1 | tail -1); echo "$t: $r"; done