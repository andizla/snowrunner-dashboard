# map_names.ps1: the names the game gives its maps, from its own files, for the [Map:<name>] sections of Gauges.ini.
# It reads the game's .pak files (zip archives) for the level names and the English texts that go with them, and the
# save files for what they say about the map last driven on. It only reads; it writes out\map_names.txt.
#   map_names.bat [the game's folder]
# Without a folder the Steam default is used.
param([string]$Game = 'C:\Program Files (x86)\Steam\steamapps\common\Snowrunner')

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.IO.Compression.FileSystem
$Game = $Game.TrimEnd('\')
$outDir = Join-Path (Split-Path $PSScriptRoot) 'out'
New-Item -ItemType Directory -Force -Path $outDir | Out-Null
$outFile = Join-Path $outDir 'map_names.txt'
$report = New-Object System.Collections.Generic.List[string]
function Say([string]$s) { $report.Add($s); Write-Host $s }

if (-not (Test-Path -LiteralPath (Join-Path $Game 'Sources\Bin\SnowRunner.exe'))) {
    Write-Host "There is no Sources\Bin\SnowRunner.exe under $Game."
    Write-Host 'Give the game''s folder: map_names.bat "D:\Games\Snowrunner"'
    exit 1
}

# a level's name, as in level_us_01_01, and a region's or a map's key in the texts, as in US_01 or US_01_01
$levelRx = [regex]'(?i)level_[a-z]{2}_\d{2}_\d{2}[a-z0-9_]*'
$keyRx = [regex]'(?i)(?<![a-z0-9])(us|ru)_\d{2}(_\d{2})?(?![0-9])'
$levels = @{}
$texts = New-Object System.Collections.Generic.List[string]
$textEntries = New-Object System.Collections.Generic.List[string]

# Text from bytes, UTF-16 where the bytes say so, else UTF-8.
function TextOf([byte[]]$b) {
    if ($b.Length -ge 2 -and $b[0] -eq 0xFF -and $b[1] -eq 0xFE) { return [Text.Encoding]::Unicode.GetString($b, 2, $b.Length - 2) }
    if ($b.Length -ge 4 -and $b[1] -eq 0 -and $b[3] -eq 0) { return [Text.Encoding]::Unicode.GetString($b) }
    return [Text.Encoding]::UTF8.GetString($b)
}

function BytesOf([System.IO.Stream]$s) {
    $ms = New-Object System.IO.MemoryStream
    try { $s.CopyTo($ms); return , $ms.ToArray() } finally { $ms.Dispose() }
}

Say "The game's map names, from its files in $Game"
Say ''
Say '== the .pak files'
$paks = Get-ChildItem -LiteralPath $Game -Recurse -Filter *.pak -File -ErrorAction SilentlyContinue | Sort-Object FullName
foreach ($pak in $paks) {
    $rel = $pak.FullName.Substring($Game.Length).TrimStart('\')
    try {
        $zip = [IO.Compression.ZipFile]::OpenRead($pak.FullName)
        try {
            $named = 0
            foreach ($e in $zip.Entries) {
                foreach ($m in $levelRx.Matches($e.FullName)) {
                    $id = $m.Value.ToLower()
                    if (-not $levels.ContainsKey($id)) { $levels[$id] = New-Object System.Collections.Generic.List[string] }
                    if (-not $levels[$id].Contains($rel)) { $levels[$id].Add($rel) }
                    $named++
                }
                # the texts, where the maps' and regions' names are: all of them listed, the English ones read
                if ($e.FullName -notmatch '(?i)string|\.str$|locali') { continue }
                $english = $e.FullName -match '(?i)engl|_en\b|\ben\b'
                $textEntries.Add(('{0} : {1} ({2:N0} bytes){3}' -f $rel, $e.FullName, $e.Length, $(if ($english) { ', read' } else { '' })))
                if ($english -and $e.Length -gt 0 -and $e.Length -lt 64MB) {
                    $s = $e.Open()
                    try { $text = TextOf (BytesOf $s) } finally { $s.Dispose() }
                    foreach ($line in ($text -split "`r?`n")) {
                        if ($levelRx.IsMatch($line) -or $keyRx.IsMatch($line)) { $texts.Add(('{0}: {1}' -f $e.Name, $line.Trim())) }
                    }
                }
            }
            Say ('{0}  {1:N0} MB, {2} entries, {3} level names in them' -f $rel, ($pak.Length / 1MB), $zip.Entries.Count, $named)
        }
        finally { $zip.Dispose() }
    }
    catch { Say ('{0}  {1:N0} MB, not read as a zip: {2}' -f $rel, ($pak.Length / 1MB), $_.Exception.Message) }
}

Say ''
Say ('== the level names ({0})' -f $levels.Count)
foreach ($id in ($levels.Keys | Sort-Object)) { Say ('{0}   in {1}' -f $id, (($levels[$id] | Select-Object -First 3) -join ', ')) }

Say ''
Say ('== the text files ({0}, at most 200 here; the English ones are read)' -f $textEntries.Count)
foreach ($t in ($textEntries | Select-Object -First 200)) { Say $t }
Say ''
Say ('== the lines in them with a map''s or a region''s key ({0}, at most 3000 here)' -f $texts.Count)
foreach ($t in ($texts | Select-Object -First 3000)) { $report.Add($t) }
Write-Host '(in the file)'

# The saves: whether they say which map was driven on last. Steam keeps them in userdata, the other stores in
# Documents\My Games\SnowRunner.
Say ''
Say '== the save files'
$steam = Split-Path (Split-Path (Split-Path $Game))
$saves = @()
if (Test-Path -LiteralPath (Join-Path $steam 'userdata')) {
    $saves += Get-ChildItem -LiteralPath (Join-Path $steam 'userdata') -Recurse -Filter 'CompleteSave*' -File -ErrorAction SilentlyContinue
}
$docs = [Environment]::GetFolderPath('MyDocuments')
if ($docs) { $docs = Join-Path $docs 'My Games\SnowRunner' }
if ($docs -and (Test-Path -LiteralPath $docs)) {
    $saves += Get-ChildItem -LiteralPath $docs -Recurse -Filter 'CompleteSave*' -File -ErrorAction SilentlyContinue
}
foreach ($save in $saves) {
    Say ('{0}  {1:N0} KB, saved {2}' -f $save.FullName, ($save.Length / 1KB), $save.LastWriteTime)
    if ($save.Length -gt 64MB) { continue }
    $text = TextOf ([IO.File]::ReadAllBytes($save.FullName))
    $seen = @{}
    foreach ($m in $levelRx.Matches($text)) {
        $id = $m.Value.ToLower()
        if (-not $seen.ContainsKey($id)) { $seen[$id] = 0 }
        $seen[$id]++
        if ($seen[$id] -gt 2) { continue }
        $from = [Math]::Max(0, $m.Index - 60)
        $around = $text.Substring($from, [Math]::Min($text.Length - $from, $m.Index - $from + $m.Length + 20)) -replace '\s+', ' '
        $report.Add(('    {0}: ...{1}...' -f $id, $around))
    }
    Say ('    {0} level names in it: {1}' -f $seen.Count, (($seen.Keys | Sort-Object | ForEach-Object { '{0} x{1}' -f $_, $seen[$_] }) -join ', '))
}
if ($saves.Count -eq 0) { Say 'none found' }

$report | Set-Content -LiteralPath $outFile -Encoding UTF8
Write-Host ''
Write-Host "Written to $outFile. Send me that file."
