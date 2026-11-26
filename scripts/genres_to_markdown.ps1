# Renders resources/tags/genres.json as docs/GENRES.md (for the owner's review).
# Run from the repository root:  powershell -ExecutionPolicy Bypass -File scripts/genres_to_markdown.ps1
param(
    [string]$Json = "resources/tags/genres.json",
    [string]$Out = "docs/GENRES.md"
)
$utf8 = New-Object System.Text.UTF8Encoding($false)
$doc = [System.IO.File]::ReadAllText((Resolve-Path $Json), $utf8) | ConvertFrom-Json
$nl = [string][char]10
$sb = New-Object System.Text.StringBuilder
function Add([string]$s) { [void]$sb.Append($s + $nl) }

Add "# Genre vocabulary (draft for review)"
Add ""
Add "Generated from ``resources/tags/genres.json`` by ``scripts/genres_to_markdown.ps1`` - edit the JSON, not this file."
Add ""
Add "**What to check**"
Add "- Is every genre you want here, and is nothing here that you do not want? (``key`` is the permanent database key; names can be changed freely, keys should not.)"
Add "- The **ru / zh_CN / ja names are machine drafts**."
Add "- **related** (+0.20 in the recommender) and **opposite** (-0.30) relations are symmetric; the file is refused at start-up if they are not."
Add "- Aliases are the spellings found in real tags; every spelling must mean exactly one genre. All four names and the ID3v1 names also work as aliases."
Add ""
Add ("Genres: " + $doc.genres.Count)
Add ""
Add "## Genres"
Add ""
Add "| key | en | ru | zh_CN | ja | related | opposite |"
Add "|---|---|---|---|---|---|---|"
foreach ($g in $doc.genres) {
    $rel = ($g.related -join ", ")
    $opp = ($g.opposite -join ", ")
    Add ("| ``" + $g.key + "`` | " + $g.names.en + " | " + $g.names.ru + " | " + $g.names.zh_CN + " | " + $g.names.ja + " | " + $rel + " | " + $opp + " |")
}
Add ""
Add "## Aliases"
Add ""
Add "| key | aliases |"
Add "|---|---|"
foreach ($g in $doc.genres) {
    if ($g.aliases.Count -gt 0) { Add ("| ``" + $g.key + "`` | " + ($g.aliases -join "; ") + " |") }
}
Add ""
Add "## ID3v1 genre numbers (0-191)"
Add ""
Add "Numbers 0-79 are the original ID3v1 list, 80-191 the Winamp extensions. An empty ""maps to"" means: no sensible genre in our vocabulary, the value is ignored."
Add ""
Add "| # | ID3v1 name | maps to |"
Add "|---|---|---|"
foreach ($e in $doc.id3v1) {
    $to = ""
    if ($e.key -ne "") { $to = "``" + $e.key + "``" }
    Add ("| " + $e.id + " | " + $e.name + " | " + $to + " |")
}
[System.IO.File]::WriteAllText($Out, $sb.ToString(), $utf8)
"written " + $Out
