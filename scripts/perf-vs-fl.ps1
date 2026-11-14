# perf-vs-fl.ps1: one measured run of a DAW on Windows (LYNX, FL Studio, Ableton Live), printed as one JSON line.
# Round 190gd, built on round 116 / 165i / 174c's bench.ps1. Process metrics only: no injected input, no focus change
# requested (Start-Process shows the window the normal way; the caller holds the GUI lock and the user-idle gate).
#
#   -exe <path> [-argline "<args>"] [-title <text the ready main window's title contains>] [-mode open|play]
#   [-envs "K=V;K=V"] [-closeTitles "a;b"]  dialogs of this process whose title contains one of these get WM_CLOSE
#   [-quietPct 35] [-idleS 15] [-settleS 8] [-sampleS 20] [-space 1] [-watch <dupmon.exe>] [-shot <bmp>]
#   [-vmmap <vmmap64.exe> -vmmapOut <file.csv>]  Sysinternals VMMap snapshot of the idle process (open mode)
#   [-keep 0]  1 = leave the process running and print its pid (the caller stops it by that pid)
#
# Measures:
#   window_s       launch -> biggest visible top-level window >= 900x500 whose title contains -title and that answers
#                  WM_NULL within 250 ms (LYNX loads a project on the message thread, so it cannot answer before)
#   settled_s      after the window: first moment the process stays under -quietPct % of one core for 1.5 s
#   idle_*_pct     CPU over -idleS s after settling, % of ONE logical core (100 = one core); main process and tree
#   gpu_pct        3D engine utilisation of the process over the idle window (GPU Engine counters), %
#   mem            ws = working set, pws = private working set, priv = private bytes (commit charge), peak_ws,
#                  all MB, main process and whole process tree; handles, threads, gdi/user objects
#   play mode      posts Space to the main window (PostMessage, no global input), waits -settleS, then process CPU and
#                  per-thread CPU over -sampleS s (thread named by its start module); with -watch, dupmon counts the
#                  screen updates of the window's rect during 10 s of playback (frames/s, gaps)
#   sys_cpu_pct    Windows total CPU % in the 2 s before the launch (machine load at measurement time)
param(
  [string]$exe, [string]$argline = "", [string]$title = "", [string]$mode = "open",
  [double]$quietPct = 35, [double]$readyTimeoutS = 30, [double]$idleS = 15, [double]$timeoutS = 240,
  [double]$settleS = 8, [double]$sampleS = 20, [int]$space = 0, [string]$closeTitles = "", [int]$keep = 0,
  [string]$shot = "", [string]$envs = "", [string]$watch = "", [string]$dupmon = "", [string]$vmmap = "", [string]$vmmapOut = ""
)
$ErrorActionPreference = 'Continue'
foreach ($kv in ($envs -split ';')) { if ($kv -match '^([^=]+)=(.*)$') { [Environment]::SetEnvironmentVariable($matches[1], $matches[2]) } }
Add-Type @"
using System; using System.Text; using System.Collections.Generic; using System.Runtime.InteropServices;
public class W {
 public delegate bool P(IntPtr h, IntPtr l);
 [DllImport("user32.dll")] public static extern bool EnumWindows(P p, IntPtr l);
 [DllImport("user32.dll")] public static extern int GetWindowText(IntPtr h, StringBuilder s, int n);
 [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint p);
 [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
 [DllImport("user32.dll")] public static extern bool IsIconic(IntPtr h);
 [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
 [DllImport("user32.dll")] public static extern IntPtr SendMessageTimeout(IntPtr h, uint m, IntPtr w, IntPtr l, uint f, uint t, out IntPtr r);
 [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h, uint m, IntPtr w, IntPtr l);
 [DllImport("user32.dll")] public static extern uint GetGuiResources(IntPtr h, uint flags);
 public struct RECT { public int L, T, R, B; }
 public class Win { public IntPtr h; public string t; public int x, y, w, ht; }
 public static List<Win> Wins(uint pid) { var l = new List<Win>(); EnumWindows((h, x) => { uint p; GetWindowThreadProcessId(h, out p);
   if (p == pid && IsWindowVisible(h)) { var s = new StringBuilder(512); GetWindowText(h, s, 512); RECT r; GetWindowRect(h, out r);
     l.Add(new Win { h = h, t = s.ToString(), x = r.L, y = r.T, w = r.R - r.L, ht = r.B - r.T }); } return true; }, IntPtr.Zero); return l; }
 public static bool Answers(IntPtr h) { IntPtr r; return SendMessageTimeout(h, 0, IntPtr.Zero, IntPtr.Zero, 2, 250, out r) != IntPtr.Zero; }
 [DllImport("ntdll.dll")] public static extern int NtQueryInformationThread(IntPtr t, int c, out IntPtr i, int l, IntPtr r);
 [DllImport("kernel32.dll")] public static extern IntPtr OpenThread(uint a, bool i, uint id);
 [DllImport("kernel32.dll")] public static extern bool CloseHandle(IntPtr h);
 [DllImport("kernel32.dll")] public static extern int GetThreadDescription(IntPtr t, out IntPtr d);
 [DllImport("kernel32.dll")] public static extern IntPtr LocalFree(IntPtr m);
 public static string ThreadName(uint tid) { var h = OpenThread(0x0800, false, tid); if (h == IntPtr.Zero) return ""; IntPtr d; string s = "";
   if (GetThreadDescription(h, out d) >= 0 && d != IntPtr.Zero) { s = Marshal.PtrToStringUni(d); LocalFree(d); } CloseHandle(h); return s; }
 public static long StartAddr(uint tid) { var h = OpenThread(0x0040, false, tid); if (h == IntPtr.Zero) return 0; IntPtr a; NtQueryInformationThread(h, 9, out a, IntPtr.Size, IntPtr.Zero); CloseHandle(h); return a.ToInt64(); }
}
"@
function SysCpu { try { $v = @(1..3 | ForEach-Object { (Get-CimInstance Win32_PerfFormattedData_PerfOS_Processor -Filter "Name='_Total'" -ea Stop).PercentProcessorTime; Start-Sleep -Milliseconds 600 }); return [math]::Round(($v | Measure-Object -Average).Average, 1) } catch { return -1 } } # (WMI: the counter names are localized)
function Tree([int]$root) {
  $all = Get-CimInstance Win32_Process -Property ProcessId, ParentProcessId, CreationDate | Where-Object { $_.CreationDate -ge $script:startTime }
  $ids = @($root); $grew = $true
  while ($grew) { $grew = $false; foreach ($p in $all) { if ($ids -contains $p.ParentProcessId -and -not ($ids -contains $p.ProcessId)) { $ids += [int]$p.ProcessId; $grew = $true } } }
  return $ids }
function TreeCpu($ids) { $h = @{}; foreach ($i in $ids) { $q = Get-Process -Id $i -ea 0; if ($q) { $h[$i] = $q.TotalProcessorTime.TotalSeconds } }; return $h }
function CpuDelta($a, $b) { $s = 0.0; foreach ($k in $b.Keys) { if ($a.ContainsKey($k)) { $s += $b[$k] - $a[$k] } }; return $s }
function Mem($ids, $r) {
  $m = @{ ws = 0; pws = 0; priv = 0; peak = 0 }; $t = @{ ws = 0; pws = 0; priv = 0; peak = 0 }
  foreach ($i in $ids) {
    $q = Get-Process -Id $i -ea 0; if (-not $q) { continue }
    $c = Get-CimInstance Win32_PerfRawData_PerfProc_Process -Filter "IDProcess=$i" -ea 0
    $v = @{ ws = $q.WorkingSet64 / 1MB; pws = $(if ($c) { [double]$c.WorkingSetPrivate / 1MB } else { 0 }); priv = $q.PrivateMemorySize64 / 1MB; peak = $q.PeakWorkingSet64 / 1MB }
    foreach ($k in @('ws', 'pws', 'priv', 'peak')) { $t[$k] += $v[$k]; if ($i -eq $ids[0]) { $m[$k] = $v[$k] } } }
  $r.ws_mb = [math]::Round($m.ws); $r.pws_mb = [math]::Round($m.pws); $r.priv_mb = [math]::Round($m.priv); $r.peak_ws_mb = [math]::Round($m.peak)
  $r.ws_tree_mb = [math]::Round($t.ws); $r.pws_tree_mb = [math]::Round($t.pws); $r.priv_tree_mb = [math]::Round($t.priv)
  $q = Get-Process -Id $ids[0] -ea 0
  if ($q) { $r.threads = $q.Threads.Count; $r.handles = $q.HandleCount; $r.gdi = [W]::GetGuiResources($q.Handle, 0); $r.user = [W]::GetGuiResources($q.Handle, 1) }
}
function Gpu([int]$procId, [int]$secs) {
  try {
    $n = [math]::Max(1, $secs)
    $s = Get-Counter -Counter "\GPU Engine(pid_$($procId)_*engtype_3D)\Utilization Percentage" -SampleInterval 1 -MaxSamples $n -ea Stop
    $tot = 0.0; foreach ($set in $s) { foreach ($c in $set.CounterSamples) { $tot += $c.CookedValue } }
    return [math]::Round($tot / $n, 1)
  } catch { return -1 } }
function Main($p) { $best = $null; foreach ($w in [W]::Wins([uint32]$p.Id)) { if ($w.w -ge 900 -and $w.ht -ge 500 -and ($best -eq $null -or $w.w * $w.ht -gt $best.w * $best.ht)) { $best = $w } }; return $best }
$closeList = @($closeTitles -split ';' | Where-Object { $_ -ne "" })
$script:closed = @()
function CloseDialogs($p) { foreach ($w in [W]::Wins([uint32]$p.Id)) { foreach ($c in $closeList) { if ($w.t -like "*$c*") { [void][W]::PostMessage($w.h, 0x10, [IntPtr]::Zero, [IntPtr]::Zero); if (-not ($script:closed -contains $w.t)) { $script:closed += $w.t } } } } }

$r = [ordered]@{}
$r.sys_cpu_pct = SysCpu
$script:startTime = (Get-Date).AddSeconds(-1)
$sw = [Diagnostics.Stopwatch]::StartNew()
$p = if ($argline -ne "") { Start-Process -FilePath $exe -ArgumentList $argline -WorkingDirectory (Split-Path $exe) -PassThru } else { Start-Process -FilePath $exe -WorkingDirectory (Split-Path $exe) -PassThru }
try { $p.PriorityClass = 'AboveNormal' } catch {}
$null = $p.Handle
$r.pid = $p.Id
$readyAt = -1; $last = ""; $firstWin = -1
while ($sw.Elapsed.TotalSeconds -lt $timeoutS) {
  Start-Sleep -Milliseconds 50
  if ($p.HasExited) { $r.exited = $true; break }
  CloseDialogs $p
  $m = Main $p
  if ($m -ne $null) { if ($firstWin -lt 0) { $firstWin = $sw.Elapsed.TotalSeconds }; $last = $m.t; if (($title -eq "" -or $m.t -like "*$title*") -and [W]::Answers($m.h)) { $readyAt = $sw.Elapsed.TotalSeconds; break } }
}
$r.first_window_s = [math]::Round($firstWin, 2)
$r.window_s = [math]::Round($readyAt, 2); $r.title = $last
$p.Refresh(); $r.cpu_at_window_s = [math]::Round($p.TotalProcessorTime.TotalSeconds, 2)
if ($readyAt -ge 0 -and $mode -eq "open") {
  $lastCpu = $p.TotalProcessorTime.TotalSeconds; $lastT = $sw.Elapsed.TotalSeconds; $quietSince = -1; $settled = -1
  while ($sw.Elapsed.TotalSeconds -lt $readyAt + $readyTimeoutS) {
    Start-Sleep -Milliseconds 250; CloseDialogs $p; $p.Refresh()
    $c = $p.TotalProcessorTime.TotalSeconds; $t = $sw.Elapsed.TotalSeconds; $use = 100 * ($c - $lastCpu) / ($t - $lastT); $lastCpu = $c; $lastT = $t
    if ($use -lt $quietPct) { if ($quietSince -lt 0) { $quietSince = $t - 0.25 }; if ($t - $quietSince -ge 1.5) { $settled = $quietSince; break } } else { $quietSince = -1 }
  }
  $r.settled_s = [math]::Round($settled, 2)
  $p.Refresh(); $r.cpu_at_settled_s = [math]::Round($p.TotalProcessorTime.TotalSeconds, 2)
  $ids = Tree $p.Id; $c0 = TreeCpu $ids; $t0 = $sw.Elapsed.TotalSeconds
  $r.gpu_pct = Gpu $p.Id ([int]$idleS)
  $el = $sw.Elapsed.TotalSeconds - $t0; if ($el -lt $idleS) { Start-Sleep -Milliseconds ([int](1000 * ($idleS - $el))) }
  $ids = Tree $p.Id; $c1 = TreeCpu $ids; $t1 = $sw.Elapsed.TotalSeconds
  $r.idle_main_cpu_pct = [math]::Round(100 * ($c1[$p.Id] - $c0[$p.Id]) / ($t1 - $t0), 1)
  $r.idle_cpu_pct = [math]::Round(100 * (CpuDelta $c0 $c1) / ($t1 - $t0), 1)
  Mem $ids $r
  $r.procs = (@($ids | ForEach-Object { (Get-Process -Id $_ -ea 0).ProcessName }) -join ",")
  if ($vmmap -ne "" -and $vmmapOut -ne "") { $v = Start-Process -FilePath $vmmap -ArgumentList "-accepteula", "-p", "$($p.Id)", "`"$vmmapOut`"" -PassThru -WindowStyle Hidden; if (-not $v.WaitForExit(60000)) { Stop-Process -Id $v.Id -Force -ea 0 } }
  if ($shot -ne "" -and $dupmon -ne "") { & $dupmon shot $shot | Out-Null }
}
if ($readyAt -ge 0 -and $mode -eq "play") {
  Start-Sleep -Seconds 2
  $m = Main $p
  if ($space -eq 1) { [void][W]::PostMessage($m.h, 0x100, [IntPtr]0x20, [IntPtr]0x00390001); [void][W]::PostMessage($m.h, 0x101, [IntPtr]0x20, [IntPtr]0xC0390001) }
  Start-Sleep -Seconds $settleS
  $mods = @{}; foreach ($mo in (Get-Process -Id $p.Id).Modules) { $mods[$mo.ModuleName] = @($mo.BaseAddress.ToInt64(), ($mo.BaseAddress.ToInt64() + $mo.ModuleMemorySize)) }
  $p.Refresh(); $th0 = @{}; foreach ($t in $p.Threads) { try { $th0[$t.Id] = $t.TotalProcessorTime.TotalSeconds } catch {} }
  $c0 = $p.TotalProcessorTime.TotalSeconds; $t0 = $sw.Elapsed.TotalSeconds
  # round 190gn: the process's page faults (soft + hard) while it plays, from the raw counter (a running total)
  $pf0 = (Get-CimInstance Win32_PerfRawData_PerfProc_Process -Filter "IDProcess=$($p.Id)" -ea 0).PageFaultsPersec
  if ($watch -ne "" -and $m -ne $null) {
    # screen updates inside the window (primary monitor part) during 10 s of playback
    $x = [math]::Max(0, $m.x + 8); $y = [math]::Max(0, $m.y + 8); $w = [math]::Min(1920 - $x, $m.w - 16); $h = [math]::Min(1080 - $y, $m.ht - 16)
    $o = (& $dupmon watch 10 $x $y $w $h $watch 2>&1 | Out-String).Trim()
    if ($o -match 'fps=([\d.]+) med_ms=([\d.]+) p95_ms=([\d.]+) p99_ms=([\d.]+) max_ms=([\d.]+) gaps_gt33=(\d+) gaps_gt50=(\d+)') {
      $r.ui_fps = [double]$matches[1]; $r.ui_p95_ms = [double]$matches[3]; $r.ui_max_ms = [double]$matches[5]; $r.ui_gaps50 = [int]$matches[7] } }
  $r.gpu_pct = Gpu $p.Id 5
  $el = $sw.Elapsed.TotalSeconds - $t0; if ($el -lt $sampleS) { Start-Sleep -Milliseconds ([int](1000 * ($sampleS - $el))) }
  $p.Refresh(); $c1 = $p.TotalProcessorTime.TotalSeconds; $t1 = $sw.Elapsed.TotalSeconds
  $r.play_cpu_pct = [math]::Round(100 * ($c1 - $c0) / ($t1 - $t0), 1)
  $pf1 = (Get-CimInstance Win32_PerfRawData_PerfProc_Process -Filter "IDProcess=$($p.Id)" -ea 0).PageFaultsPersec
  if ($pf0 -ne $null -and $pf1 -ne $null) { $r.play_faults = [int64]$pf1 - [int64]$pf0; $r.play_faults_s = [math]::Round(($t1 - $t0), 1) }
  $rows = @()
  foreach ($t in $p.Threads) { try { if ($th0.ContainsKey($t.Id)) { $d = 100 * ($t.TotalProcessorTime.TotalSeconds - $th0[$t.Id]) / ($t1 - $t0)
        if ($d -ge 0.5) { $a = [W]::StartAddr([uint32]$t.Id); $mn = "{0:x}" -f $a; foreach ($k in $mods.Keys) { if ($a -ge $mods[$k][0] -and $a -lt $mods[$k][1]) { $mn = "$k+0x{0:x}" -f ($a - $mods[$k][0]) } }
          $rows += [ordered]@{ tid = $t.Id; pct = [math]::Round($d, 1); start = $mn; name = [W]::ThreadName([uint32]$t.Id); prio = "$($t.PriorityLevel)" } } } } catch {} }
  $r.threads_busy = @($rows | Sort-Object { - $_.pct } | Select-Object -First 12)
  $ids = Tree $p.Id; Mem $ids $r
  if ($shot -ne "" -and $dupmon -ne "") { & $dupmon shot $shot | Out-Null }
}
if ($script:closed.Count -gt 0) { $r.closed = $script:closed }
if ($readyAt -ge 0) { $kids = @(); foreach ($i in (Tree $p.Id)) { if ($i -ne $p.Id) { $c = Get-CimInstance Win32_Process -Filter "ProcessId=$i" -ea 0; if ($c) { $kids += "$($c.Name)" } } }; $r.children = $kids }
if ($keep -eq 1) { $r.kept = $true }
elseif (-not $p.HasExited) { foreach ($i in (Tree $p.Id)) { if ($i -ne $p.Id) { Stop-Process -Id $i -Force -ea 0 } }; Stop-Process -Id $p.Id -Force -ea 0 }
$r | ConvertTo-Json -Compress -Depth 4
