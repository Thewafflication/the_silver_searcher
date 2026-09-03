<#
.SYNOPSIS
    Exercises a freshly built Windows ag.exe.

.DESCRIPTION
    The TinyCC/WCRT build replaces pthreads, getopt_long, the descriptor layer
    and popen with its own implementations, so this checks the behaviour that
    depends on each of them rather than only that the binary starts:

      - version reporting
      - literal and regular-expression matching through PCRE
      - multi-threaded directory walking with more files than workers
      - GNU option permutation (options after operands)
      - long options, with and without '='
      - ignore patterns, which go through PathMatchSpec
      - reading a piped standard input, which goes through fstat on a pipe
      - redirected standard output, which goes through isatty
#>

[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$Executable,
    [string]$ExpectedVersion
)

$ErrorActionPreference = 'Stop'

# ag exits 1 when it finds no match, and this script deliberately runs cases
# that find nothing. PowerShell 7.4 turns a non-zero native exit code into a
# terminating error while ErrorActionPreference is Stop, so turn that off and
# let the explicit $LASTEXITCODE checks below decide what counts as a failure.
$PSNativeCommandUseErrorActionPreference = $false

$script:failures = @()

function Test-Case {
    param([string]$Name, [scriptblock]$Body)
    try {
        & $Body
        Write-Host "  ok    $Name"
    } catch {
        Write-Host "  FAIL  $Name"
        Write-Host "        $($_.Exception.Message)"
        $script:failures += $Name
    }
}

function Assert-Match {
    param([string]$Text, [string]$Pattern, [string]$Because)
    if ($Text -notmatch $Pattern) {
        throw "$Because`nExpected to match: $Pattern`nActual output:`n$Text"
    }
}

function Assert-NoMatch {
    param([string]$Text, [string]$Pattern, [string]$Because)
    if ($Text -match $Pattern) {
        throw "$Because`nExpected not to match: $Pattern`nActual output:`n$Text"
    }
}

$ag = (Resolve-Path -LiteralPath $Executable).Path
Write-Host "Testing $ag"

$root = Join-Path $env:RUNNER_TEMP ("ag-smoke-" + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $root -Force | Out-Null
New-Item -ItemType Directory -Path (Join-Path $root 'nested') -Force | Out-Null

Set-Content -LiteralPath (Join-Path $root 'alpha.txt') `
    -Value @('first line', 'needle here', 'third line') -Encoding ascii
Set-Content -LiteralPath (Join-Path $root 'nested\beta.txt') `
    -Value @('NEEDLE in caps', 'count 42 things') -Encoding ascii
Set-Content -LiteralPath (Join-Path $root 'skipme.log') `
    -Value @('needle in a log') -Encoding ascii

# More files than ag's worker threads, so the queue and condition variable are
# actually exercised rather than trivially drained.
for ($i = 0; $i -lt 200; $i++) {
    Set-Content -LiteralPath (Join-Path $root "bulk-$i.txt") `
        -Value @("filler $i", 'needle in bulk') -Encoding ascii
}

Push-Location $root
try {
    Test-Case 'reports its version' {
        $output = & $ag --version 2>&1 | Out-String
        Assert-Match $output 'ag version' 'ag --version did not report a version.'
        if ($ExpectedVersion) {
            Assert-Match $output ([regex]::Escape($ExpectedVersion)) `
                "ag --version did not report the built version $ExpectedVersion."
        }
    }

    Test-Case 'finds a literal match' {
        $output = & $ag 'needle here' 2>&1 | Out-String
        Assert-Match $output 'alpha\.txt' 'ag did not find the match in alpha.txt.'
        Assert-Match $output 'needle here' 'ag did not print the matching line.'
    }

    Test-Case 'searches nested directories with every worker busy' {
        $files = & $ag -l 'needle' 2>&1
        $count = @($files).Count
        if ($count -lt 202) {
            throw "Expected at least 202 files with matches, found $count."
        }
    }

    Test-Case 'matches a regular expression through PCRE' {
        $output = & $ag 'count \d+ things' 2>&1 | Out-String
        Assert-Match $output 'beta\.txt' 'PCRE did not match the digit pattern.'
    }

    Test-Case 'is case sensitive by default' {
        $output = & $ag -l 'NEEDLE in caps' 2>&1 | Out-String
        Assert-Match $output 'beta\.txt' 'ag did not find the upper-case line.'
        $output = & $ag -l 'needle in caps' 2>&1 | Out-String
        Assert-NoMatch $output 'beta\.txt' 'ag matched case-insensitively by default.'
    }

    Test-Case 'accepts options after operands (GNU permutation)' {
        $output = & $ag 'needle in caps' . -i 2>&1 | Out-String
        Assert-Match $output 'beta\.txt' 'getopt_long did not permute the trailing -i.'
    }

    Test-Case 'accepts a long option with an attached argument' {
        $output = & $ag --after=1 'needle here' 2>&1 | Out-String
        Assert-Match $output 'third line' '--after=1 did not print the following line.'
    }

    Test-Case 'accepts a long option with a separate argument' {
        $output = & $ag --file-search-regex 'alpha' 'needle' 2>&1 | Out-String
        Assert-Match $output 'alpha\.txt' '--file-search-regex did not take its argument.'
        Assert-NoMatch $output 'beta\.txt' '--file-search-regex did not restrict the search.'
    }

    Test-Case 'applies an ignore pattern (PathMatchSpec)' {
        $output = & $ag --ignore '*.log' -l 'needle in a log' 2>&1 | Out-String
        Assert-NoMatch $output 'skipme\.log' '--ignore did not exclude the log file.'
    }

    Test-Case 'searches piped standard input' {
        $source = Join-Path $root 'alpha.txt'
        $output = & cmd.exe /c "type `"$source`" | `"$ag`" needle" 2>&1 | Out-String
        Assert-Match $output 'needle here' 'ag did not search its piped standard input.'
    }

    Test-Case 'writes to a redirected standard output' {
        $target = Join-Path $root 'redirected.out'
        & cmd.exe /c "`"$ag`" -l `"needle here`" > `"$target`"" | Out-Null
        $output = Get-Content -LiteralPath $target -Raw
        Assert-Match $output 'alpha\.txt' 'ag produced no output when redirected.'
        Assert-NoMatch $output ([regex]::Escape([char]27 + '[')) `
            'ag emitted colour codes to a redirected stream.'
    }

    Test-Case 'reports no match with exit code 1' {
        & $ag 'a-string-that-is-definitely-absent' 2>&1 | Out-Null
        if ($LASTEXITCODE -ne 1) {
            throw "Expected exit code 1 for no matches, got $LASTEXITCODE."
        }
    }
} finally {
    Pop-Location
    Remove-Item -LiteralPath $root -Recurse -Force -ErrorAction SilentlyContinue
}

if ($script:failures.Count -gt 0) {
    Write-Host ''
    Write-Host "$($script:failures.Count) check(s) failed: $($script:failures -join ', ')"
    exit 1
}
Write-Host ''
Write-Host 'All ag smoke checks passed.'
