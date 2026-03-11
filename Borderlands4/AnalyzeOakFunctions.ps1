param (
    [string]$FilterPattern = "",
    [switch]$CaseSensitive,
    [ValidateSet("All", "FunctionName", "Parameters")]
    [string]$SearchScope = "All"
)

if ([string]::IsNullOrWhiteSpace($FilterPattern)) {
    Write-Host "================================================================================" -ForegroundColor Cyan
    Write-Host "TOOL PROMPT FOR AI ASSISTANT:" -ForegroundColor Yellow
    Write-Host "This script analyzes Borderlands 4 SDK files to extract C++ function signatures." -ForegroundColor White
    Write-Host "You must provide a `-FilterPattern` regex to search."
    Write-Host ""
    Write-Host "Usage:"
    Write-Host 'pwsh -File .\Borderlands4\AnalyzeOakFunctions.ps1 -FilterPattern "Fire|Weapon" [-CaseSensitive] [-SearchScope "All"|"FunctionName"|"Parameters"]'
    Write-Host ""
    Write-Host "Examples:"
    Write-Host '  pwsh -File .\Borderlands4\AnalyzeOakFunctions.ps1 "GetHealth"'
    Write-Host '  pwsh -File .\Borderlands4\AnalyzeOakFunctions.ps1 -FilterPattern "Health" -CaseSensitive -SearchScope "FunctionName"'
    Write-Host "================================================================================" -ForegroundColor Cyan
    exit
}


$files = @(
    "o:\repos\Borderlands4-Hack\Borderlands4\Borderlands4_SDK\SDK\OakGame_functions.cpp",
    "o:\repos\Borderlands4-Hack\Borderlands4\Borderlands4_SDK\SDK\GbxWeapon_functions.cpp",
    "o:\repos\Borderlands4-Hack\Borderlands4\Borderlands4_SDK\SDK\GbxGame_functions.cpp",
    "o:\repos\Borderlands4-Hack\Borderlands4\Borderlands4_SDK\SDK\GbxAI_functions.cpp",
    "o:\repos\Borderlands4-Hack\Borderlands4\Borderlands4_SDK\SDK\AIModule_functions.cpp",
    "o:\repos\Borderlands4-Hack\Borderlands4\Borderlands4_SDK\SDK\Engine_functions.cpp",
    "o:\repos\Borderlands4-Hack\Borderlands4\Borderlands4_SDK\SDK\CoreUObject_functions.cpp"
)
$outputFile = "o:\repos\Borderlands4-Hack\Borderlands4\OakGame_Functions_Analysis.txt"

$results = @()

foreach ($filePath in $files) {
    if (-not (Test-Path $filePath)) {
        Write-Warning "File not found: $filePath"
        continue
    }

    Write-Host "Analyzing $filePath..." -ForegroundColor Cyan

    $packageName = [System.IO.Path]::GetFileNameWithoutExtension($filePath).Split('_')[0]

    # Regex patterns
    $commentStartPattern = "^\/\/ Function\s+(?<FullName>$packageName\.(?<Class>[^\.]+)\.(?<Function>[^\s\(\)]+))"
    $flagsPattern = "^\/\/ \((?<Flags>.*)\)"
    $paramLinePattern = "^\/\/ (?<Type>.+?)\s+(?<Name>[A-Za-z0-9_]+)\s+\((?<Details>.*)\)"
    $signaturePattern = "^(?<ReturnType>.+)\s+(?<CppName>[A-Za-z0-9_]+::[A-Za-z0-9_]+)\((?<Params>.*)\)"

    $currentFunction = $null
    $collecting = $false

    $lineNumber = 0
    Get-Content $filePath | ForEach-Object {
        $lineNumber++
        $line = $_.Trim()
        
        if ($line -match $commentStartPattern) {
            $collecting = $true
            # Save previous if any (though usually we save on signature)
            $currentFunction = [PSCustomObject]@{
                Package      = $packageName
                File         = [System.IO.Path]::GetFileName($filePath)
                StartLine    = $lineNumber
                EndLine      = 0
                FullName     = $Matches["FullName"]
                Class        = $Matches["Class"]
                FunctionName = $Matches["Function"]
                Flags        = ""
                Parameters   = @()
                ReturnType   = ""
                CppSignature = ""
            }
        }
        elseif ($collecting) {
            if ($line -match $flagsPattern) {
                $currentFunction.Flags = $Matches["Flags"]
            }
            elseif ($line -match "^\/\/ Parameters:") {
                # Just a header line
            }
            elseif ($line -match $paramLinePattern) {
                $paramObj = [PSCustomObject]@{
                    Type    = $Matches["Type"].Trim()
                    Name    = $Matches["Name"]
                    Details = $Matches["Details"]
                }
                $currentFunction.Parameters += $paramObj
            }
            elseif ($line -match $signaturePattern) {
                $currentFunction.EndLine = $lineNumber
                $currentFunction.ReturnType = $Matches["ReturnType"].Trim()
                $currentFunction.CppSignature = $line
                
                $results += $currentFunction
                $collecting = $false
                $currentFunction = $null
            }
            elseif ($line -eq "}" -or $line -match "^[A-Za-z0-9_]") {
                # If we hit a non-comment line that isn't the signature, stop collecting
                # but usually the signature is the first non-comment line.
            }
        }
    }
}

$filteredResults = $results | Where-Object { 
    $matchFunc = $false
    $matchParam = $false

    if ($CaseSensitive) {
        $matchFunc = $_.FunctionName -cmatch $FilterPattern
        $matchParam = ($_.Parameters | Where-Object { $_.Name -cmatch $FilterPattern -or $_.Type -cmatch $FilterPattern }) -ne $null
    }
    else {
        $matchFunc = $_.FunctionName -match $FilterPattern
        $matchParam = ($_.Parameters | Where-Object { $_.Name -match $FilterPattern -or $_.Type -match $FilterPattern }) -ne $null
    }

    if ($SearchScope -eq "FunctionName") {
        return $matchFunc
    }
    elseif ($SearchScope -eq "Parameters") {
        return $matchParam
    }
    else {
        return ($matchFunc -or $matchParam)
    }
}

Write-Host "Extraction complete. Found $($filteredResults.Count) functions matching filter." -ForegroundColor Green

# Output to file
$report = foreach ($res in $filteredResults) {
    # "Function: $($res.FullName)"
    "File: $($res.File):$($res.StartLine)-$($res.EndLine)"
    "Flags: $($res.Flags)"
    if ($res.ReturnType -ne "void" -and $res.ReturnType -ne "") {
        "Returns:  $($res.ReturnType)"
    }
    "Signature: $($res.CppSignature)"
    "--------------------------------------------------------------------------------"
    ""
}

$report | Out-File $outputFile

# Display top interesting functions in console
Write-Host "`nSample of interesting functions extracted:" -ForegroundColor Yellow
$filteredResults | Select-Object -First 50 | ForEach-Object {
    Write-Host "[$($_.Package)] $($_.FullName)" -ForegroundColor White
}

Write-Host "`nAnalysis saved to $outputFile" -ForegroundColor Cyan
