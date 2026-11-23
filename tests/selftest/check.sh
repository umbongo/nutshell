#!/bin/sh
# check.sh <off|on> <report-file>
# Asserts on the report nutshell.exe --self-report wrote. AI off: nothing of
# the assistant was requested from Windows. AI on (the control): the two
# RichEdit libraries were requested, so a pass of the "off" run means the
# hook really sees loads.
mode=$1
report=$2
[ -s "$report" ] || { echo "FAIL ($mode): no report at $report"; exit 1; }
req() { grep -qixF "requested: $1" "$report"; }
mod() { grep -qiF "module: $1" "$report"; }
rc=0
case $mode in
off)
    for d in winhttp.dll riched20.dll msftedit.dll; do
        if req "$d"; then echo "FAIL (off): $d was requested"; rc=1; fi
    done
    if mod winhttp.dll; then echo "FAIL (off): winhttp.dll is a loaded module"; rc=1; fi
    # the record must not be empty, or the assertion above proves nothing
    grep -qi '^requested: ' "$report" || { echo "FAIL (off): no 'requested:' line at all"; rc=1; }
    ;;
on)
    for d in winhttp.dll riched20.dll msftedit.dll; do
        req "$d" || { echo "FAIL (on): $d was not requested"; rc=1; }
    done
    ;;
*) echo "usage: check.sh off|on report"; exit 2 ;;
esac
[ $rc -eq 0 ] && echo "PASS ($mode): $(grep -ci '^requested: ' "$report") requested, $(grep -ci '^module: ' "$report") modules"
exit $rc
