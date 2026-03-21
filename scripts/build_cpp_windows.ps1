# Build the C++ inference app on Windows from a normal PowerShell (e.g. in Cursor).
# This script loads the VS 2022 x64 environment (vcvars64) then runs CMake + Ninja,
# so you don't need to open "x64 Native Tools Command Prompt" manually.
#
# Usage: from repo root, run:  .\scripts\build_cpp_windows.ps1
#
# Optional env vars (set before running):
#   TENSORRT_DIR     - TensorRT 10.x (default: C:\TensorRT-10.15.1.29)
#   CUDAToolkit_ROOT - CUDA toolkit (default: C:\Program Files\...\CUDA\v13.1)
#   OPENCV_DIR       - OpenCV build dir containing OpenCVConfig.cmake
#
# (Cache is always cleared so the x64 toolchain is detected correctly.)

param([switch]$Clean) # ignored; cache is always cleared

$ErrorActionPreference = "Stop"
$repoRoot = (Get-Item $PSScriptRoot).Parent.FullName
$buildDir = Join-Path $repoRoot "cpp\build"
$cppDir   = Join-Path $repoRoot "cpp"

# Defaults (override with env vars if needed)
$cudaRoot = if ($env:CUDAToolkit_ROOT) { $env:CUDAToolkit_ROOT } else { "C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA\v13.1" }
$opencvDir = if ($env:OPENCV_DIR) { $env:OPENCV_DIR } else { "C:\Users\conno\Downloads\opencv\build\x64\vc16\lib" }
$tensorRtDir = if ($env:TENSORRT_DIR) { $env:TENSORRT_DIR } else { "C:\TensorRT-10.15.1.29" }
$nvcc = Join-Path $cudaRoot "bin\nvcc.exe"

if (-not (Test-Path $nvcc)) {
    Write-Error "nvcc not found at $nvcc. Set CUDAToolkit_ROOT or install CUDA."
}
if (-not (Test-Path $opencvDir)) {
    Write-Error "OpenCV dir not found at $opencvDir. Set OPENCV_DIR or fix path in script."
}
if (-not (Test-Path $tensorRtDir)) {
    Write-Error "TensorRT dir not found at $tensorRtDir. Set TENSORRT_DIR (e.g. C:\TensorRT-10.15.1.29)."
}

# Find VS 2022 vcvars64.bat (x64 toolchain). Prefer 2022 over "latest" so CUDA 13.x gets a supported host compiler.
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vcvars64 = $null
if (Test-Path $vswhere) {
    $vsPath = & $vswhere -version "[17.0,18.0)" -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath 2>$null | Select-Object -First 1
    if ($vsPath) {
        $vcvars64 = Join-Path $vsPath "VC\Auxiliary\Build\vcvars64.bat"
    }
}
if (-not $vcvars64 -or -not (Test-Path $vcvars64)) {
    $vcvars64 = "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
    if (-not (Test-Path $vcvars64)) {
        $vcvars64 = "C:\Program Files\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
    }
}
if (-not (Test-Path $vcvars64)) {
    Write-Error "vcvars64.bat not found. Install Visual Studio 2022 (or Build Tools) with 'Desktop development with C++'."
}

# Ensure build dir exists
if (-not (Test-Path $buildDir)) { New-Item -ItemType Directory -Path $buildDir -Force | Out-Null }

$cudaRootSlash = $cudaRoot -replace '\\','/'
$opencvSlash = $opencvDir -replace '\\','/'
$tensorRtSlash = $tensorRtDir -replace '\\','/'
$nvccSlash = $nvcc -replace '\\','/'

# Use Ninja (not Visual Studio generator) to avoid "No CUDA toolset found" with VS 2025/2026.
# Explicitly set CUDA compiler and toolkit so CMake finds them.
$cmakeArgs = ".. -G Ninja -DCMAKE_BUILD_TYPE=Release"
$cmakeArgs += " -DCMAKE_CUDA_COMPILER=`"$nvccSlash`""
$cmakeArgs += " -DCUDAToolkit_ROOT=`"$cudaRootSlash`""
$cmakeArgs += " -DCUDA_TOOLKIT_ROOT_DIR=`"$cudaRootSlash`""
$cmakeArgs += " -DOpenCV_DIR=`"$opencvSlash`""
$cmakeArgs += " -DTENSORRT_DIR=`"$tensorRtSlash`""

# Always clear CMake cache so the compiler/linker are detected from the current x64 env (avoids reusing an x86 cache).
Remove-Item -Path (Join-Path $buildDir "CMakeCache.txt") -ErrorAction SilentlyContinue
Remove-Item -Path (Join-Path $buildDir "CMakeFiles") -Recurse -Force -ErrorAction SilentlyContinue

Write-Host "================================================================" -ForegroundColor Cyan
Write-Host " Building golf_sim (C++ inference)" -ForegroundColor Cyan
Write-Host "================================================================" -ForegroundColor Cyan
Write-Host " Generator:   Ninja (avoids VS CUDA toolset issues)"
Write-Host " CUDA:        $cudaRoot"
Write-Host " TensorRT:    $tensorRtDir"
Write-Host " OpenCV:      $opencvDir"
Write-Host "================================================================" -ForegroundColor Cyan

# Run configure + build in one cmd session with x64 env (vcvars64 sets PATH, LIB, INCLUDE)
$cmd = "call `"$vcvars64`" && cd /d `"$buildDir`" && cmake $cmakeArgs && cmake --build . --config Release"
& cmd /c $cmd
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
Write-Host "Build succeeded. Binary: $buildDir\golf_sim.exe" -ForegroundColor Green
