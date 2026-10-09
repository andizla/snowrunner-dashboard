# map_names.ps1: the names the game gives its maps and regions, from its own files, for the [Map:<name>] sections of
# Gauges.ini. It reads the English texts in the game's .pak files (zip archives) for the maps' and the regions' names,
# and the save files for what they say about the map last driven on. It only reads the game's files; it writes
# out\map_names.txt, and the English texts whole as out\strings_english.txt.
#   map_names.bat [the game's folder]
# Without a folder the Steam default is used.
param([string]$Game = 'C:\Program Files (x86)\Steam\steamapps\common\Snowrunner')

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.IO.Compression.FileSystem
$Game = $Game.TrimEnd('\')
$outDir = Join-Path (Split-Path $PSScriptRoot) 'out'
New-Item -ItemType Directory -Force -Path $outDir | Out-Null
$outFile = Join-Path $outDir 'map_names.txt'
$stringsFile = Join-Path $outDir 'strings_english.txt'
$report = New-Object System.Collections.Generic.List[string]
function Say([string]$s) { $report.Add($s); Write-Host $s }

if (-not (Test-Path -LiteralPath (Join-Path $Game 'Sources\Bin\SnowRunner.exe'))) {
    Write-Host "There is no Sources\Bin\SnowRunner.exe under $Game."
    Write-Host 'Give the game''s folder: map_names.bat "D:\Games\Snowrunner"'
    exit 1
}

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

# ---- the .pak files: the maps they hold, and the English texts
$paks = Get-ChildItem -LiteralPath $Game -Recurse -Filter *.pak -File -ErrorAction SilentlyContinue | Sort-Object FullName
$levels = New-Object System.Collections.Generic.List[string]
$pakLines = New-Object System.Collections.Generic.List[string]
$english = $null
$englishSize = 0
foreach ($pak in $paks) {
    $rel = $pak.FullName.Substring($Game.Length).TrimStart('\')
    if ($pak.BaseName -match '(?i)^level_(us|ru)_\d{2}_\d{2}') { $levels.Add($pak.BaseName.ToLower()) }
    try {
        $zip = [IO.Compression.ZipFile]::OpenRead($pak.FullName)
        try {
            # the game's own English texts: the biggest file of that name, as a mod's .pak may bring a small one
            foreach ($e in $zip.Entries) {
                if ($e.FullName -notmatch '(?i)(^|[\\/\]])strings_english\.str$' -or $e.Length -le $englishSize -or $e.Length -ge 64MB) { continue }
                $s = $e.Open()
                try { $english = TextOf (BytesOf $s) } finally { $s.Dispose() }
                $englishSize = $e.Length
                $englishFrom = ('the English texts: {0} : {1} ({2:N0} bytes)' -f $rel, $e.FullName, $e.Length)
            }
        }
        finally { $zip.Dispose() }
    }
    catch { $pakLines.Add(('{0} not read as a zip: {1}' -f $rel, $_.Exception.Message)) }
}
if ($null -ne $english) { $pakLines.Insert(0, $englishFrom) }

# The texts as key and value: KEY, some tabs, "value". The first of a key that is given twice is kept.
$texts = @{}
$order = New-Object System.Collections.Generic.List[string]
if ($null -ne $english) {
    [IO.File]::WriteAllText($stringsFile, $english, (New-Object Text.UTF8Encoding $true))
    foreach ($line in ($english -split "`r?`n")) {
        if ($line -notmatch '^\s*(\S+)\s+"(.*)"\s*$') { continue }
        $key = $Matches[1].ToUpper()
        if ($texts.ContainsKey($key)) { continue }
        $texts[$key] = $Matches[2] -replace '\\"', '"'
        $order.Add($key)
    }
}

Say "The game's map and region names, from its files in $Game"
foreach ($l in $pakLines) { Say $l }
if ($null -eq $english) { Say 'No strings_english.str was found in the .pak files.' }
else { Say ('{0:N0} texts in it; all of them are in {1}' -f $texts.Count, $stringsFile) }

# ---- the maps: each level's name, under US_01_01_NAME or LEVEL_US_01_01_NAME
Say ''
Say ('== the maps ({0} .pak files of a level): the level, its region, its English name' -f $levels.Count)
$mapRx = [regex]'(?i)^level_((us|ru)_\d{2})_(\d{2})'
foreach ($id in ($levels | Sort-Object -Unique)) {
    $m = $mapRx.Match($id)
    $key = '{0}_{1}' -f $m.Groups[1].Value.ToUpper(), $m.Groups[3].Value
    $name = $null
    foreach ($k in @("$($key)_NAME", "LEVEL_$($key)_NAME", "$($key)_NEW_NAME", "LEVEL_$($key)_NEW_NAME")) {
        if ($texts.ContainsKey($k) -and $texts[$k]) { $name = $texts[$k]; break }
    }
    if (-not $name) { $name = '(no name found under ' + $key + '_NAME)' }
    Say ('{0,-24} {1}  {2}' -f $id, $m.Groups[1].Value.ToLower(), $name)
}

# ---- the regions: the texts whose key names a region (US_01 with no map's number after it), or a region in words
Say ''
Say '== the regions: the texts whose key names a region but no map, or says REGION, COUNTRY or STATE (short values)'
$regionRx = [regex]'(?i)(^|_)(us|ru)_\d{2}(?!_?\d)'
$found = 0
foreach ($key in ($order | Sort-Object)) {
    $value = $texts[$key]
    if ($value.Length -eq 0 -or $value.Length -gt 48 -or $value -match '[<\n]') { continue }
    $byId = $regionRx.IsMatch($key) -and $key -notmatch '(?i)(us|ru)_\d{2}_\d{2}'
    $byWord = $key -match '(?i)REGION|COUNTRY|STATE_NAME|_STATE$'
    if (-not ($byId -or $byWord)) { continue }
    Say ('{0} = {1}' -f $key, $value)
    $found++
}
if ($found -eq 0) { Say 'none: send me out\strings_english.txt as well' }

# ---- the saves: whether they say which map was driven on last. Steam keeps them in userdata, the other stores in
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
if ($saves.Count -eq 0) { Say 'none found' }

# the folders the saves are in, every file with its time, the newest first: the map files written last
foreach ($dir in ($saves | ForEach-Object { $_.DirectoryName } | Sort-Object -Unique)) {
    Say ''
    Say ('{0}, the newest first:' -f $dir)
    foreach ($f in (Get-ChildItem -LiteralPath $dir -File | Sort-Object LastWriteTime -Descending | Select-Object -First 80)) {
        Say ('    {0:yyyy-MM-dd HH:mm:ss}  {1,10:N0} bytes  {2}' -f $f.LastWriteTime, $f.Length, $f.Name)
    }
}

# Every place in a save that holds a map's or a region's name as its value, with the names it holds. Lists become [],
# and an object's keys that are maps' names become {level}, so the same place is counted once.
$places = [ordered]@{}
function Walk($node, [string]$path, [int]$depth) {
    if ($depth -gt 40 -or $null -eq $node) { return }
    if ($node -is [string]) {
        if ($node -match '(?i)^(level_)?(us|ru)_\d{2}') {
            if (-not $places.Contains($path)) { $places[$path] = New-Object System.Collections.Generic.List[string] }
            if (-not $places[$path].Contains($node)) { $places[$path].Add($node) }
        }
        return
    }
    if ($node -is [System.Collections.IList]) {
        foreach ($item in $node) { Walk $item ($path + '[]') ($depth + 1) }
        return
    }
    if ($node -is [System.Management.Automation.PSCustomObject]) {
        foreach ($p in $node.PSObject.Properties) {
            $name = if ($p.Name -match '(?i)^(level_)?(us|ru)_\d{2}') { '{level}' } else { $p.Name }
            Walk $p.Value ($path + '.' + $name) ($depth + 1)
        }
    }
}

foreach ($save in $saves) {
    Say ''
    Say ('{0}  {1:N0} KB, saved {2:yyyy-MM-dd HH:mm:ss}' -f $save.FullName, ($save.Length / 1KB), $save.LastWriteTime)
    if ($save.Length -gt 64MB) { continue }
    $text = (TextOf ([IO.File]::ReadAllBytes($save.FullName))).Trim([char]0, ' ', "`r", "`n", "`t")
    # every key whose value is a map's name, read from the text as it stands, so this holds when the JSON does not
    $pairs = [ordered]@{}
    foreach ($m in [regex]::Matches($text, '"([^"]+)"\s*:\s*"(level_[^"]+)"')) {
        $k = $m.Groups[1].Value
        if (-not $pairs.Contains($k)) { $pairs[$k] = New-Object System.Collections.Generic.List[string] }
        if (-not $pairs[$k].Contains($m.Groups[2].Value)) { $pairs[$k].Add($m.Groups[2].Value) }
    }
    Say '    the keys whose value is a map''s name:'
    if ($pairs.Count -eq 0) { Say '      none' }
    foreach ($k in $pairs.Keys) { Say ('      {0} = {1}' -f $k, ($pairs[$k] -join ', ')) }
    try { $json = $text | ConvertFrom-Json }
    catch { Say ('    not read as JSON: {0}' -f $_.Exception.Message); continue }
    # the top of the save: each key, with its value when it is short
    $top = $json
    # a save is one object named after its file (CompleteSave, CompleteSave1) that holds SslValue
    $outer = @($top.PSObject.Properties)
    if ($outer.Count -eq 1 -and $outer[0].Value -is [System.Management.Automation.PSCustomObject]) { $top = $outer[0].Value }
    if ($top.PSObject.Properties['SslValue']) { $top = $top.SslValue }
    Say '    its keys:'
    foreach ($p in $top.PSObject.Properties) {
        $v = $p.Value
        $shown = if ($null -eq $v) { 'null' }
                 elseif ($v -is [System.Collections.IList]) { '[{0} items]' -f $v.Count }
                 elseif ($v -is [System.Management.Automation.PSCustomObject]) { '{{{0} keys}}' -f @($v.PSObject.Properties).Count }
                 else { $s = [string]$v; if ($s.Length -gt 60) { $s.Substring(0, 60) + '...' } else { $s } }
        Say ('      {0} = {1}' -f $p.Name, $shown)
    }
    $places.Clear()
    Walk $json '' 0
    Say '    the places that hold a map''s or a region''s name:'
    if ($places.Count -eq 0) { Say '      none' }
    foreach ($path in $places.Keys) {
        $names = $places[$path]
        $shown = ($names | Select-Object -First 12) -join ', '
        if ($names.Count -gt 12) { $shown += (', and {0} more' -f ($names.Count - 12)) }
        Say ('      {0} = {1}' -f $path.TrimStart('.'), $shown)
    }
}

$report | Set-Content -LiteralPath $outFile -Encoding UTF8
Write-Host ''
Write-Host "Written to $outFile. Send me that file."
