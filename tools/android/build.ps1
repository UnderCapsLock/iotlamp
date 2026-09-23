# Build LightPlus.apk without Android Studio.
# Requires: JDK 21 + Android cmdline-tools at $env:USERPROFILE\android-sdk (see README).

param(
    [string]$SdkRoot = "$env:USERPROFILE\android-sdk\sdk",
    [string]$Jdk     = "$env:USERPROFILE\android-sdk\jdk",
    [string]$Out     = "$PSScriptRoot\..\..\dist\LightPlus.apk"
)
$ErrorActionPreference = "Continue"
$src = $PSScriptRoot
$env:JAVA_HOME = $Jdk
$env:PATH = "$Jdk\bin;$env:PATH"

function Invoke-Step([string]$name, [scriptblock]$block) {
    & $block
    if ($LASTEXITCODE -and $LASTEXITCODE -ne 0) { throw "$name failed (exit $LASTEXITCODE)" }
}

$bt = Get-ChildItem "$SdkRoot\build-tools" -Directory | Sort-Object Name -Descending | Select-Object -First 1
$platform = Get-ChildItem "$SdkRoot\platforms" -Directory | Select-Object -First 1
if (-not $bt -or -not $platform) { throw "Android SDK not found at $SdkRoot" }
$androidJar = Join-Path $platform.FullName "android.jar"

$work = Join-Path $env:TEMP "lightplus-apk"
Remove-Item $work -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Path "$work\classes", "$work\dex" -Force | Out-Null
New-Item -ItemType Directory -Path (Split-Path $Out) -Force | Out-Null

Write-Host "1/6 compiling resources..."
& "$($bt.FullName)\aapt2.exe" compile --dir "$src\res" -o "$work\res.zip"
& "$($bt.FullName)\aapt2.exe" link -o "$work\app.unsigned.apk" -I $androidJar `
    --manifest "$src\AndroidManifest.xml" -R "$work\res.zip" --auto-add-overlay

Write-Host "2/6 compiling java..."
$files = Get-ChildItem "$src\src" -Recurse -Filter *.java | ForEach-Object { $_.FullName }
Invoke-Step "javac" { & "$Jdk\bin\javac.exe" --release 8 -nowarn -classpath $androidJar -d "$work\classes" $files }

Write-Host "3/6 dexing..."
& "$Jdk\bin\jar.exe" cf "$work\classes.jar" -C "$work\classes" .
& "$($bt.FullName)\d8.bat" --lib $androidJar --release --output "$work\dex" "$work\classes.jar"
Push-Location "$work\dex"
& "$Jdk\bin\jar.exe" uf "$work\app.unsigned.apk" classes.dex
Pop-Location

Write-Host "4/6 aligning..."
& "$($bt.FullName)\zipalign.exe" -f 4 "$work\app.unsigned.apk" "$work\app.aligned.apk"

Write-Host "5/6 signing..."
if (-not (Test-Path "$src\debug.keystore")) {
    & "$Jdk\bin\keytool.exe" -genkeypair -keystore "$src\debug.keystore" -storepass android `
        -alias androiddebugkey -keypass android -dname "CN=Android Debug,O=Android,C=US" `
        -keyalg RSA -keysize 2048 -validity 10000
}
& "$($bt.FullName)\apksigner.bat" sign --ks "$src\debug.keystore" --ks-pass pass:android `
    --key-pass pass:android --ks-key-alias androiddebugkey --out $Out "$work\app.aligned.apk"

Write-Host "6/6 verifying..."
& "$($bt.FullName)\apksigner.bat" verify --verbose $Out | Select-Object -First 4
Write-Host "APK ready: $((Resolve-Path $Out).Path)"
