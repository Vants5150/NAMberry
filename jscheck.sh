#!/bin/bash
# syntax check of the page's JavaScript (node)
cd ~/circle-work/circle/addon/wlan/sample/pisound-wifi-test
python3 - <<'EOF'
import re
t = open('control_page.h', encoding='utf-8').read()
m = re.search(r'<script>(.*)</script>', t, re.S)
open('/tmp/page_check.js', 'w').write(m.group(1).replace('@@TOKEN@@', 'abc'))
EOF
node --check /tmp/page_check.js && echo JS-OK
