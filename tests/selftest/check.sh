#!/bin/sh
# check.sh <off|voiceoff|on> <report-file>
# Asserts on the report nutshell.exe --self-report wrote. AI off: nothing of
# the assistant was requested from Windows (winhttp.dll, riched20.dll and
# msftedit.dll) and no `module: winhttp.dll` is loaded. AI on (the control):
# winhttp.dll, riched20.dll and msftedit.dll were all requested, so a pass of
# the "off" run means the hook really sees loads.
# Voice (needs the AI assistant): the "off" run has voice ticked in its config
# and must still never have requested mmdevapi.dll; "voiceoff" (AI on, voice
# off) neither; "on" (both on) must have requested mmdevapi.dll.
mode=$1
report=$2
[ -s "$report" ] || { echo "FAIL ($mode): no report at $report"; exit 1; }
req() { grep -qixF "requested: $1" "$report"; }
mod() { grep -qiF "module: $1" "$report"; }
rc=0
case $mode in
off)
    for d in winhttp.dll riched20.dll msftedit.dll mmdevapi.dll; do
        if req "$d"; then echo "FAIL (off): $d was requested"; rc=1; fi
    done
    if mod winhttp.dll; then echo "FAIL (off): winhttp.dll is a loaded module"; rc=1; fi
    for d in mmdevapi.dll audioses.dll; do
        if mod "$d"; then echo "FAIL (off): $d is a loaded module"; rc=1; fi
    done
    # the record must not be empty, or the assertion above proves nothing
    grep -qi '^requested: ' "$report" || { echo "FAIL (off): no 'requested:' line at all"; rc=1; }
    ;;
voiceoff)
    for d in winhttp.dll riched20.dll msftedit.dll; do
        req "$d" || { echo "FAIL (voiceoff): $d was not requested"; rc=1; }
    done
    if req mmdevapi.dll; then echo "FAIL (voiceoff): mmdevapi.dll was requested"; rc=1; fi
    for d in mmdevapi.dll audioses.dll; do
        if mod "$d"; then echo "FAIL (voiceoff): $d is a loaded module"; rc=1; fi
    done
    grep -qi '^requested: ' "$report" || { echo "FAIL (voiceoff): no 'requested:' line at all"; rc=1; }
    ;;
on)
    for d in winhttp.dll riched20.dll msftedit.dll mmdevapi.dll; do
        req "$d" || { echo "FAIL (on): $d was not requested"; rc=1; }
    done
    ;;
*) echo "usage: check.sh off|voiceoff|on report"; exit 2 ;;
esac
[ $rc -eq 0 ] && echo "PASS ($mode): $(grep -ci '^requested: ' "$report") requested, $(grep -ci '^module: ' "$report") modules"
exit $rc
