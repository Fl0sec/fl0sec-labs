$ErrorActionPreference = "Stop"

$labRoot = $PSScriptRoot
$sourceDirectory = Join-Path $labRoot "src"
$artifactDirectory = Join-Path $labRoot "build"
$output = Join-Path $artifactDirectory "TBMTrainer.exe"
$mainObject = Join-Path $artifactDirectory "trainer_main.obj"
$trainerObject = Join-Path $artifactDirectory "game_trainer.obj"
$pdb = Join-Path $artifactDirectory "TBMTrainer.pdb"

$vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
if (-not (Test-Path -LiteralPath $vswhere)) {
    throw "Visual Studio 2022 Build Tools were not found."
}

$installationPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $installationPath) {
    throw "The Visual C++ x64 build tools were not found."
}

$vsDevCmd = Join-Path $installationPath "Common7\Tools\VsDevCmd.bat"
$mainSource = Join-Path $sourceDirectory "trainer_main.cpp"
$trainerSource = Join-Path $sourceDirectory "game_trainer.cpp"

New-Item -ItemType Directory -Force -Path $artifactDirectory | Out-Null

$arguments = @(
    "call `"$vsDevCmd`" -no_logo -arch=x64 -host_arch=x64 &&",
    "cl.exe /nologo /std:c++latest /EHsc /W4 /WX /O2 /GS /guard:cf /utf-8 /c",
    "`"$mainSource`" /Fo`"$mainObject`" &&",
    "cl.exe /nologo /std:c++latest /EHsc /W4 /WX /O2 /GS /guard:cf /utf-8 /c",
    "`"$trainerSource`" /Fo`"$trainerObject`" &&",
    "link.exe /nologo `"$mainObject`" `"$trainerObject`" /OUT:`"$output`"",
    "/Brepro /DYNAMICBASE /HIGHENTROPYVA /NXCOMPAT /GUARD:CF /MANIFEST:EMBED",
    "/MANIFESTUAC:`"level='asInvoker' uiAccess='false'`" /PDB:`"$pdb`"",
    "shell32.lib advapi32.lib user32.lib"
) -join " "

cmd.exe /d /s /c $arguments
if ($LASTEXITCODE -ne 0) {
    throw "x64 trainer build failed with exit code $LASTEXITCODE"
}

Get-Item -LiteralPath $output
