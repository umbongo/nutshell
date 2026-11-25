# 97-voice.ps1 -- voice cases fed from a WAV file instead of a microphone (spec
# docs/superpowers/specs/2026-11-24-voice-live-conversation-design.md section 12,
# plan Task 16). WRITTEN, NOT RUN: no pwsh exists on automaton, so this file has
# not even been parsed -- the first Windows run is its first test.
#
# The release exe has no way to read a WAV. These cases run build\win\nutshell-it.exe
# (`make harness-exe`: the same sources compiled with -DNS_VOICE_WAV_SEAM), found
# beside -Exe, and launch it with --voice-wav=<file>: every capture, dictation and
# conversation, then reads that 16 kHz mono 16-bit file in 20 ms chunks paced in real
# time (silence after the end) in place of WASAPI. The fixture is synthesised here with
# System.Speech, so no binary file is committed. The seam build also keeps two small
# files beside the WAV, because the capture and the header are not readable otherwise:
#   <wav>.capture  "open" while a capture is open, "closed" after (the release exe's
#                  self-report is a one-shot at startup, so it cannot answer mid-run)
#   <wav>.header   every conversation header the controller showed, one per line
# Both cases are tier "ai": they need a model key (as the other ai_* cases) and a
# speech-to-text key in $env:NUTSHELL_IT_STT_KEY (an OpenAI key; its live model gives the
# live text). Without either, or without nutshell-it.exe, they are skipped.
# Dot-sourced by Run-Integration.ps1; depends on its Invoke-Case/Assert-True/$Exe/
# $AiKey/$AiSettings/$ActiveTiers/$Only/$Artifacts/$WM_COMMAND.

$script:VoiceItExe = Join-Path (Split-Path -Parent $Exe) "nutshell-it.exe"
$script:IDC_CHAT_MIC_IT = 4016     # src/ui/ai_chat_internal.h
$script:IDC_CHAT_SEND_IT = 4003
$script:IDC_CHAT_INPUT_IT = 4002

function New-VoiceFixtureWav {
    <# Speak -Text into -Path as a 16 kHz mono 16-bit WAV, with 700 ms of silence first
       (the listener needs a quiet start to judge the noise). Dispose flushes the header. #>
    param([Parameter(Mandatory)] [string] $Text, [Parameter(Mandatory)] [string] $Path)
    Add-Type -AssemblyName System.Speech
    $synth = New-Object System.Speech.Synthesis.SpeechSynthesizer
    try {
        $fmt = New-Object System.Speech.AudioFormat.SpeechAudioFormatInfo(
            16000,
            [System.Speech.AudioFormat.AudioBitsPerSample]::Sixteen,
            [System.Speech.AudioFormat.AudioChannel]::Mono)
        $synth.SetOutputToWaveFile($Path, $fmt)
        $pb = New-Object System.Speech.Synthesis.PromptBuilder
        $pb.AppendBreak([TimeSpan]::FromMilliseconds(700))
        $pb.AppendText($Text)
        $synth.Speak($pb)
    } finally {
        $synth.Dispose()
    }
}

function Get-VoiceSeamFile {
    <# The text of "<wav>.<ext>" the seam build writes, or "" while it does not exist. #>
    param([Parameter(Mandatory)] [string] $Wav, [Parameter(Mandatory)] [string] $Ext)
    $f = "$Wav.$Ext"
    if (-not (Test-Path $f)) { return "" }
    try {
        $fs = [IO.File]::Open($f, 'Open', 'Read', 'ReadWrite')
        try { return (New-Object IO.StreamReader($fs)).ReadToEnd() } finally { $fs.Dispose() }
    } catch { return "" }
}

function Wait-VoiceSeamFile {
    <# Poll until "<wav>.<ext>" matches -Pattern (regex). $true/$false. #>
    param([Parameter(Mandatory)] [string] $Wav, [Parameter(Mandatory)] [string] $Ext,
          [Parameter(Mandatory)] [string] $Pattern, [int] $TimeoutSec = 30)
    $deadline = (Get-Date).AddSeconds($TimeoutSec)
    while ((Get-Date) -lt $deadline) {
        if ((Get-VoiceSeamFile -Wav $Wav -Ext $Ext) -match $Pattern) { return $true }
        Start-Sleep -Milliseconds 200
    }
    return $false
}

function Invoke-VoiceSeamCase {
    <# One WAV-fed case. Skips (not fails) without nutshell-it.exe, a model key or an
       STT key. The fixture is spoken into artifacts\voice\<Name>.wav before the launch;
       the exe is the seam build for this case only (Invoke-Case reads $Exe from the
       caller's dynamic scope, so the local assignment below is what it copies). #>
    param([string] $Name, [string] $Mode, [string] $Phrase, [scriptblock] $Body)
    if ($ActiveTiers -notcontains "ai") { return }
    if ($Only.Count -gt 0 -and $Only -notcontains $Name) { return }
    if (-not $AiKey) { Write-Host ("[SKIP] " + $Name + " -- no model key"); return }
    if (-not $env:NUTSHELL_IT_STT_KEY) { Write-Host ("[SKIP] " + $Name + " -- set NUTSHELL_IT_STT_KEY (an OpenAI key) for speech to text"); return }
    if (-not (Test-Path $script:VoiceItExe)) { Write-Host ("[SKIP] " + $Name + " -- build\win\nutshell-it.exe missing (make harness-exe)"); return }

    $dir = Join-Path $Artifacts "voice"
    New-Item -ItemType Directory -Force $dir | Out-Null
    $wav = Join-Path $dir ($Name + ".wav")
    foreach ($e in @("capture", "header")) { Remove-Item -Force "$wav.$e" -ErrorAction SilentlyContinue }
    New-VoiceFixtureWav -Text $Phrase -Path $wav

    $Exe = $script:VoiceItExe
    $settings = @{}
    foreach ($k in $AiSettings.Keys) { $settings[$k] = $AiSettings[$k] }
    $settings.voice_enabled = $true
    $settings.voice_mode = $Mode
    $settings.voice_stt_use_ai = $false
    $settings.voice_stt_provider = "openai"
    $settings.stt_api_key = $env:NUTSHELL_IT_STT_KEY
    $settings.voice_pause_ms = 1200
    $settings.voice_defaults_rev = 3
    $caseBody = { param($s) & $Body $s $wav }.GetNewClosure()
    Invoke-Case -Name $Name -Settings $settings -Body $caseBody -CaseTier "ai" `
        -ExtraArgs @("-sn", "it", "--voice-wav=$wav")
}

Invoke-VoiceSeamCase -Name "voice_live_dictation_fixture" -Mode "dictation" `
    -Phrase "check the disk space on this machine" -Body {
    param($s, $wav)
    Start-NutshellLogging -Session $s | Out-Null
    Wait-NutshellShell -Session $s
    $p = Open-NutshellAiPanel -Session $s
    $inputBox = [NutshellNative]::GetDlgItem($p, $script:IDC_CHAT_INPUT_IT)
    $send = [NutshellNative]::GetDlgItem($p, $script:IDC_CHAT_SEND_IT)
    Assert-True ([NutshellNative]::GetDlgItem($p, $script:IDC_CHAT_MIC_IT) -ne [IntPtr]::Zero) "the AI panel has no mic button with voice on"

    # Start dictating: the mic button, posted like a click (BN_CLICKED is 0).
    [NutshellNative]::PostMessage($p, $WM_COMMAND, [IntPtr]$script:IDC_CHAT_MIC_IT, [IntPtr]::Zero) | Out-Null
    Assert-True (Wait-VoiceSeamFile -Wav $wav -Ext "capture" -Pattern "open" -TimeoutSec 15) "no capture opened after the mic click"

    # The words arrive in the box while the capture is still open (live text).
    $deadline = (Get-Date).AddSeconds(45)
    $text = ""
    while ((Get-Date) -lt $deadline) {
        $text = Get-NutshellControlText -Control $inputBox
        if ($text -match "(?i)disk") { break }
        Start-Sleep -Milliseconds 400
    }
    Save-NutshellScreenshot -Session $s -Path (Join-Path $Artifacts "voice_live_dictation.png") | Out-Null
    Assert-True ($text -match "(?i)disk") "live text never reached the box (it holds '$text')"
    Assert-True ((Get-VoiceSeamFile -Wav $wav -Ext "capture") -match "open") "the capture closed before the mic was clicked again: the live text came from a finished recording"

    # Nothing is sent until Send: the Send button is idle and the box keeps its text.
    Assert-True ((Get-NutshellControlText -Control $send) -eq ">") "the chat went busy before Send was pressed"
    [NutshellNative]::PostMessage($p, $WM_COMMAND, [IntPtr]$script:IDC_CHAT_MIC_IT, [IntPtr]::Zero) | Out-Null
    Assert-True (Wait-VoiceSeamFile -Wav $wav -Ext "capture" -Pattern "closed" -TimeoutSec 15) "the capture stayed open after the second mic click"
    Start-Sleep -Seconds 2
    $after = Get-NutshellControlText -Control $inputBox
    Assert-True ($after -match "(?i)disk") "the box lost its text when dictation stopped (it holds '$after')"
    Assert-True ((Get-NutshellControlText -Control $send) -eq ">") "the chat went busy without Send"

    # Send: the box empties and the chat goes busy.
    [NutshellNative]::PostMessage($p, $WM_COMMAND, [IntPtr]$script:IDC_CHAT_SEND_IT, [IntPtr]::Zero) | Out-Null
    $deadline = (Get-Date).AddSeconds(15)
    $cleared = $false
    while ((Get-Date) -lt $deadline) {
        if ((Get-NutshellControlText -Control $inputBox) -eq "") { $cleared = $true; break }
        Start-Sleep -Milliseconds 300
    }
    Assert-True $cleared "the box still holds its text 15 s after Send"
    "live text '$text' reached the box with the capture open, nothing was sent until Send, and Send emptied the box"
}

Invoke-VoiceSeamCase -Name "voice_conversation_card_fixture" -Mode "conversation" `
    -Phrase "list the files in my home directory" -Body {
    param($s, $wav)
    Start-NutshellLogging -Session $s | Out-Null
    Wait-NutshellShell -Session $s
    $p = Open-NutshellAiPanel -Session $s
    # The default policy (read-only, Auto-approve off) holds a run_command card for approval.
    Assert-True ((Get-NutshellAiPolicy -Session $s).Auto -eq $false) "Auto-approve is on: the card would not wait"

    [NutshellNative]::PostMessage($p, $WM_COMMAND, [IntPtr]$script:IDC_CHAT_MIC_IT, [IntPtr]::Zero) | Out-Null
    Assert-True (Wait-VoiceSeamFile -Wav $wav -Ext "capture" -Pattern "open" -TimeoutSec 15) "the conversation opened no capture"

    # The turn goes after the pause: Hearing, then Sending, then (the model replies with a
    # run_command card) the approval header. The header lines are the controller's own.
    $order = "(?s)Hearing you.*Sending.*Waiting for your approval"
    $ok = Wait-VoiceSeamFile -Wav $wav -Ext "header" -Pattern $order -TimeoutSec 120
    Save-NutshellScreenshot -Session $s -Path (Join-Path $Artifacts "voice_conversation_card.png") | Out-Null
    $hdr = (Get-VoiceSeamFile -Wav $wav -Ext "header") -replace "`r?`n", " | "
    Assert-True $ok "the headers never ran Hearing, Sending, Waiting for your approval within 120 s (saw: $hdr)"

    # The card waits and no microphone is open while it does (the seam's equivalent of the
    # self-report's capture count: the release exe's report is a one-shot at startup).
    Start-Sleep -Seconds 2
    Assert-True ((Get-VoiceSeamFile -Wav $wav -Ext "capture") -match "closed") "a capture is open while the approval card waits"
    Assert-True ((Get-NutshellAiPolicy -Session $s).Auto -eq $false) "Auto-approve switched on by itself"

    # Approve it (IDC_CMD_APPROVE_SEL, as ai_prompt_while_approval_pending does): the card
    # is the pending batch, and running it is what the wait was for.
    [NutshellNative]::PostMessage($p, $WM_COMMAND, [IntPtr]3045, [IntPtr]::Zero) | Out-Null
    Start-Sleep -Seconds 3
    "turn sent after the pause (headers: $hdr); the run_command card waited with the capture closed, then ran on Run selected"
}
