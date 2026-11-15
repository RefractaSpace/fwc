# FWC 간편 설치기 — GD 프레임 윈도우 카운터 (앱 + 지오드 모드)
# 하는 일: (1) 앱을 내 계정 폴더에 복사  (2) 바탕 화면 바로 가기  (3) 지오드가 깔린 지메를 찾아 모드 파일을 geode\mods 에 복사
# 안 하는 일: 게임 원본·저장 파일 수정, 파일 삭제, 인터넷 다운로드, 관리자 권한 요구
param(
    [string]$InstallDir = (Join-Path $env:LOCALAPPDATA "Programs\GD_FWC"),
    [string[]]$SteamRoots = @(),      # 시험용: 스팀 폴더를 직접 지정
    [string]$Choice = "",             # 시험용: 지메가 여러 개일 때 고를 번호 ("all" = 전부)
    [switch]$NoShortcut,
    [switch]$NoPause
)
$ErrorActionPreference = "Stop"
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$payload = Join-Path $here "payload"
$appSrc = Join-Path $payload "GD_FWC"
$modSrc = Join-Path $payload "fwc.window-counter.geode"
function Done($code) { if (-not $NoPause) { Write-Host ""; Read-Host "엔터를 누르면 닫힙니다" | Out-Null }; exit $code }

Write-Host "=== GD 프레임 윈도우 카운터 설치 ==="
if (-not (Test-Path (Join-Path $appSrc "GD_FWC.exe"))) {
    Write-Host "[오류] payload\GD_FWC\GD_FWC.exe 가 없어요. zip을 '모두 압축 풀기' 한 폴더에서 실행했는지 확인하세요."; Done 1
}

# ---- 1. 앱 복사 (기존 파일은 덮어쓰고, 지우지는 않음)
Write-Host ""
Write-Host "[1/3] 앱 복사 → $InstallDir"
New-Item -ItemType Directory -Force $InstallDir | Out-Null
Copy-Item -Path (Join-Path $appSrc "*") -Destination $InstallDir -Recurse -Force
$exe = Join-Path $InstallDir "GD_FWC.exe"
if (-not (Test-Path $exe)) { Write-Host "[오류] 복사 실패: $exe 가 없어요."; Done 1 }
$n = (Get-ChildItem $InstallDir -Recurse -File | Measure-Object).Count
Write-Host "      파일 $n 개 복사됨"

# ---- 2. 바로 가기
Write-Host ""
if ($NoShortcut) { Write-Host "[2/3] 바로 가기: 건너뜀" }
else {
    $desk = [Environment]::GetFolderPath("Desktop")
    $lnk = Join-Path $desk "GD 프레임 윈도우 카운터.lnk"
    $ws = New-Object -ComObject WScript.Shell
    $sc = $ws.CreateShortcut($lnk); $sc.TargetPath = $exe; $sc.WorkingDirectory = $InstallDir; $sc.Save()
    Write-Host "[2/3] 바탕 화면 바로 가기 만듦: $lnk"
}

# ---- 3. 지오드 모드
Write-Host ""
Write-Host "[3/3] 지오드 모드 설치"
if (-not (Test-Path $modSrc)) { Write-Host "      모드 파일이 설치기에 없어서 건너뜀 (앱만 설치됨)"; Done 0 }

# 스팀 폴더 후보: 기본 위치 + libraryfolders.vdf 에 적힌 다른 보관함
$roots = @()
if ($SteamRoots.Count -gt 0) { $roots = $SteamRoots }
else {
    foreach ($r in @((Join-Path ${env:ProgramFiles(x86)} "Steam"), (Join-Path $env:ProgramFiles "Steam"))) { if ($r -and (Test-Path $r)) { $roots += $r } }
    foreach ($r in @($roots)) {
        $vdf = Join-Path $r "steamapps\libraryfolders.vdf"
        if (Test-Path $vdf) {
            foreach ($line in Get-Content $vdf) {
                if ($line -match '"path"\s+"(.+)"') { $p = $Matches[1] -replace '\\\\', '\'; if ((Test-Path $p) -and ($roots -notcontains $p)) { $roots += $p } }
            }
        }
    }
}
# 지메 폴더 후보: 보통 설치본 + 내려받은 옛 버전(depot)
$cands = @()
foreach ($r in $roots) {
    $cands += (Join-Path $r "steamapps\common\Geometry Dash")
    $dep = Join-Path $r "steamapps\content\app_322170"
    if (Test-Path $dep) { $cands += (Get-ChildItem $dep -Directory -Filter "depot_*" | ForEach-Object { $_.FullName }) }
}
$games = @()
foreach ($c in $cands) {
    if (-not (Test-Path $c)) { continue }
    $x = Get-ChildItem $c -Filter "GeometryDash*.exe" -File -ErrorAction SilentlyContinue | Select-Object -First 1
    if (-not $x) { continue }
    $games += [pscustomobject]@{ Dir = $c; Exe = $x.Name; Geode = (Test-Path (Join-Path $c "geode\mods")) }
}
if ($games.Count -eq 0) {
    Write-Host "      지오메트리 대시를 찾지 못했어요. 모드는 건너뜀 (앱만 설치됨)."
    Write-Host "      나중에 직접 넣으려면: payload\fwc.window-counter.geode 를 <지메 폴더>\geode\mods 에 복사"
    Done 0
}
$i = 0
foreach ($g in $games) { $i++; $tag = "지오드 없음 → 설치 불가"; if ($g.Geode) { $tag = "지오드 있음" }; Write-Host ("      {0}) {1}\{2}   [{3}]" -f $i, $g.Dir, $g.Exe, $tag) }
$ok = @($games | Where-Object { $_.Geode })
if ($ok.Count -eq 0) {
    Write-Host "      지오드 로더가 깔린 지메가 없어요. 지오드(v5.11.2, 지메 2.2081용)를 먼저 설치한 뒤 다시 실행하세요. (앱은 설치됨)"
    Done 0
}
$targets = $ok
if ($ok.Count -gt 1) {
    $ans = $Choice
    if (-not $ans) { $ans = Read-Host "      모드를 넣을 지메 번호 (전부는 all, 건너뛰기는 그냥 엔터)" }
    if (-not $ans) { Write-Host "      모드 건너뜀"; Done 0 }
    if ($ans -ne "all") {
        $k = 0; if (-not [int]::TryParse($ans, [ref]$k) -or $k -lt 1 -or $k -gt $games.Count -or -not $games[$k - 1].Geode) { Write-Host "      [오류] 고를 수 없는 번호: $ans"; Done 1 }
        $targets = @($games[$k - 1])
    }
}
foreach ($g in $targets) {
    $dst = Join-Path $g.Dir "geode\mods\fwc.window-counter.geode"
    Copy-Item $modSrc $dst -Force
    Write-Host "      모드 넣음: $dst"
}
if (Get-Process -Name "GeometryDash*" -ErrorAction SilentlyContinue) { Write-Host "      지메가 켜져 있어요. 껐다가 다시 켜야 모드가 보입니다." }
Write-Host ""
Write-Host "설치 끝. 모드는 지오드 v5.11.2 이상 + 지메 2.2081 에서만 켜집니다."
Write-Host "지우는 법: $InstallDir 폴더, 바탕 화면 바로 가기, geode\mods\fwc.window-counter.geode 를 지우면 됩니다."
Done 0
