#!/bin/sh
# check.sh <off|voiceoff|on|conv|sapi|ai|dictai> <report-file>
# Asserts on the report nutshell.exe --self-report wrote. AI off: nothing of
# the assistant was requested from Windows (winhttp.dll, riched20.dll and
# msftedit.dll) and no `module: winhttp.dll` is loaded. AI on (the control):
# winhttp.dll, riched20.dll and msftedit.dll were all requested, so a pass of
# the "off" run means the hook really sees loads.
# Voice (needs the AI assistant): the "off" run has voice ticked in its config
# and must still never have requested mmdevapi.dll; "voiceoff" (AI on, voice
# off) neither; "on" (both on) must have requested mmdevapi.dll.
# "on" (Dictation) and "conv" (Conversation, voice on) are idle launches: neither
# sapi.dll (speech) nor wtsapi32.dll (session lock watch) is requested or mapped
# until the feature is used; "conv" also requested mmdevapi.dll. "sapi" is the
# control: --self-report-sapi holds an SpVoice, so sapi.dll is requested and
# mapped, asserted even when `sapi-control: unavailable` (Wine has a partial
# SAPI); SKIP only when it is unavailable and sapi.dll was never requested.
# "ai" (Conversation, AI speech configured, nothing spoken yet) requests
# winhttp, riched20, msftedit and mmdevapi (mapped), never sapi/wtsapi32, and
# opens no render client and no speech request: `voice-resources: 0`.
# "dictai" (Dictation, Voice on, AI speech configured, nothing pressed, run with
# -nc so no AI panel and no Read aloud button exist): an idle launch with AI
# speech configured creates no speech context (`voice-speech-contexts: 0`) and
# requests neither sapi.dll nor wtsapi32.dll. AI speech contexts take no
# resource-ledger entry, so this line is the only thing in the report that would
# show one. At launch Dictation never reads the speech engine setting, so this
# follows the same code path as "on"; that only a press starts speech is proven
# by the voice_read_win unit tests, not by this run.
# Every mode is an idle launch: the report has a `voice-net-opens: N` line and N
# is 0, and a `voice-speech-contexts: N` line (the number of speech contexts
# held, Windows voice plus AI speech) and N is 0, except "sapi": its control holds
# an SpVoice through a context while the report is written, so N is 1 when the
# report says `sapi-control: held` and 0 when it says `sapi-control: unavailable`
# (the failed hold has already quit and freed its slot).
mode=$1
report=$2
[ -s "$report" ] || { echo "FAIL ($mode): no report at $report"; exit 1; }
req() { grep -qixF "requested: $1" "$report"; }
mod() { grep -qiF "module: $1" "$report"; }
rc=0
# an idle voice launch never asked for speech or the session-lock watcher
no_speech() {
    for d in sapi.dll wtsapi32.dll; do
        if req "$d"; then echo "FAIL ($1): $d was requested"; rc=1; fi
        if mod "$d"; then echo "FAIL ($1): $d is a loaded module"; rc=1; fi
    done
}
# the voice opener's count is printed in every mode, and an idle launch (nothing
# recorded or sent) has opened none
if ! grep -qx 'voice-net-opens: [0-9][0-9]*' "$report"; then
    echo "FAIL ($mode): no 'voice-net-opens: N' line"; rc=1
elif ! grep -qx 'voice-net-opens: 0' "$report"; then
    echo "FAIL ($mode): an idle launch opened a voice connection: $(grep '^voice-net-opens: ' "$report")"; rc=1
fi
# the resource ledger's sum: every mode but the sapi control (which holds an
# SpVoice on purpose) is idle and holds nothing
if ! grep -qx 'voice-resources: [0-9][0-9]*' "$report"; then
    echo "FAIL ($mode): no 'voice-resources: N' line"; rc=1
elif [ "$mode" != sapi ] && ! grep -qx 'voice-resources: 0' "$report"; then
    echo "FAIL ($mode): an idle launch holds a voice resource: $(grep '^voice-resources: ' "$report")"; rc=1
fi
# the speech contexts held (Windows voice plus AI speech): every mode holds none,
# except the sapi control, whose hold is a context while the report is written:
# 1 when held, 0 when unavailable (the failed hold has quit and freed its slot)
if ! grep -qx 'voice-speech-contexts: [0-9][0-9]*' "$report"; then
    echo "FAIL ($mode): no 'voice-speech-contexts: N' line"; rc=1
elif [ "$mode" = sapi ]; then
    if grep -qx 'sapi-control: held' "$report"; then want_ctx=1; else want_ctx=0; fi
    if ! grep -qx "voice-speech-contexts: $want_ctx" "$report"; then
        echo "FAIL (sapi): expected voice-speech-contexts: $want_ctx: $(grep '^voice-speech-contexts: ' "$report")"; rc=1
    fi
elif ! grep -qx 'voice-speech-contexts: 0' "$report"; then
    echo "FAIL ($mode): a speech context exists before any speech was asked for: $(grep '^voice-speech-contexts: ' "$report")"; rc=1
fi
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
    # the enumerator was really created: Wine maps the module in (checked on
    # the "on" report of this run), so it is a loaded module as well
    mod mmdevapi.dll || { echo "FAIL (on): mmdevapi.dll is not a loaded module"; rc=1; }
    no_speech on
    ;;
conv)
    for d in winhttp.dll riched20.dll msftedit.dll mmdevapi.dll; do
        req "$d" || { echo "FAIL (conv): $d was not requested"; rc=1; }
    done
    mod mmdevapi.dll || { echo "FAIL (conv): mmdevapi.dll is not a loaded module"; rc=1; }
    no_speech conv
    ;;
ai)
    for d in winhttp.dll riched20.dll msftedit.dll mmdevapi.dll; do
        req "$d" || { echo "FAIL (ai): $d was not requested"; rc=1; }
    done
    mod mmdevapi.dll || { echo "FAIL (ai): mmdevapi.dll is not a loaded module"; rc=1; }
    no_speech ai
    ;;
dictai)
    for d in winhttp.dll riched20.dll msftedit.dll mmdevapi.dll; do
        req "$d" || { echo "FAIL (dictai): $d was not requested"; rc=1; }
    done
    mod mmdevapi.dll || { echo "FAIL (dictai): mmdevapi.dll is not a loaded module"; rc=1; }
    no_speech dictai
    ;;
sapi)
    unavailable=0
    grep -qx 'sapi-control: unavailable' "$report" && unavailable=1
    if [ $unavailable -eq 1 ] && ! req sapi.dll; then
        echo "SKIP (sapi): sapi.dll was never requested and sapi-control: unavailable; the Windows hand check carries the claim"
        exit 0
    fi
    if [ $unavailable -eq 0 ]; then
        grep -qx 'sapi-control: held' "$report" || { echo "FAIL (sapi): no 'sapi-control: held|unavailable' line"; rc=1; }
    fi
    req sapi.dll || { echo "FAIL (sapi): sapi.dll was not requested"; rc=1; }
    mod sapi.dll || { echo "FAIL (sapi): sapi.dll is not a loaded module"; rc=1; }
    if req wtsapi32.dll || mod wtsapi32.dll; then echo "FAIL (sapi): wtsapi32.dll was loaded"; rc=1; fi
    if [ $unavailable -eq 1 ] && [ $rc -eq 0 ]; then
        echo "note (sapi): sapi-control: unavailable (this Wine has a partial SAPI: SpVoice is created but SetInterest/SetNotifyWin32Event fail); the DLL lines were asserted"
    fi
    ;;
*) echo "usage: check.sh off|voiceoff|on|conv|sapi|ai|dictai report"; exit 2 ;;
esac
[ $rc -eq 0 ] && echo "PASS ($mode): $(grep -ci '^requested: ' "$report") requested, $(grep -ci '^module: ' "$report") modules"
exit $rc
