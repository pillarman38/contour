# convert_onnx_to_trt.ps1
# Converts a YOLOv10 ONNX model to a TensorRT engine (Windows).
#
# Prerequisites:
#   - TensorRT installed (set TENSORRT_DIR or edit $trtDir below)
#   - CUDA 11.x or 12.x (TensorRT 8.6 does NOT support CUDA 13.x).
#     If you have CUDA 13 only, install CUDA 11.8 or 12.x alongside it and set
#     CUDAToolkit_ROOT to that version before running.
#
# Usage: .\scripts\convert_onnx_to_trt.ps1 <input.onnx> [output.engine] [--fp16|--fp32]
#
# Examples:
#   .\scripts\convert_onnx_to_trt.ps1 python\runs\detect\runs\train\golf_ball_detector\weights\best.onnx --fp16
#   .\scripts\convert_onnx_to_trt.ps1 best.onnx best.engine --fp32

param(
    [Parameter(Mandatory=$true, Position=0)] [string]$OnnxPath,
    [Parameter(Position=1)] [string]$EnginePath = "",
    [switch]$fp16,
    [switch]$fp32,
    [int]$WorkspaceMB = 4096,
    [switch]$TrtVerbose
)

$ErrorActionPreference = "Stop"

# TensorRT location (override with $env:TENSORRT_DIR)
# Prefer TensorRT 10.x (supports CUDA 13.1); fallback to 8.6 if 10 not found
$trtDir = if ($env:TENSORRT_DIR) { $env:TENSORRT_DIR } else {
    if (Test-Path "C:\TensorRT-10.15.1.29") { "C:\TensorRT-10.15.1.29" }
    elseif (Test-Path "C:\TensorRT-10.14.1.48") { "C:\TensorRT-10.14.1.48" }
    else { "C:\Users\conno\Downloads\TensorRT-8.6.1.6" }
}
$trtexec = Join-Path $trtDir "bin\trtexec.exe"
$trtBin = Join-Path $trtDir "bin"

# CUDA: TensorRT 10 supports CUDA 13.1; TensorRT 8.6 requires CUDA 11 or 12
$cudaRoot = if ($env:CUDAToolkit_ROOT) { $env:CUDAToolkit_ROOT } else {
    $c13 = "C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA\v13.1"
    $c12 = "C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA\v12.4"
    $c11 = "C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA\v11.8"
    if (Test-Path $c13) { $c13 } elseif (Test-Path $c12) { $c12 } elseif (Test-Path $c11) { $c11 } else {
        $c13  # fallback; user should set CUDAToolkit_ROOT for TensorRT 8.6
    }
}
$cudaBin = Join-Path $cudaRoot "bin"

if (-not (Test-Path $OnnxPath)) {
    Write-Error "ONNX file not found: $OnnxPath"
}
if (-not (Test-Path $trtexec)) {
    Write-Error "trtexec not found at $trtexec. Set TENSORRT_DIR or install TensorRT."
}

if ([string]::IsNullOrWhiteSpace($EnginePath)) {
    $EnginePath = [System.IO.Path]::ChangeExtension($OnnxPath, ".engine")
}

$precision = if ($fp32) { "fp32" } else { "fp16" }

# Ensure PATH includes TensorRT and CUDA so DLLs are found
$oldPath = $env:PATH
$env:PATH = "$trtBin;$cudaBin;$env:PATH"

Write-Host "================================================================" -ForegroundColor Cyan
Write-Host " ONNX -> TensorRT Conversion" -ForegroundColor Cyan
Write-Host "================================================================" -ForegroundColor Cyan
Write-Host " Input:      $OnnxPath"
Write-Host " Output:     $EnginePath"
Write-Host " Precision:  $precision"
Write-Host " CUDA:       $cudaRoot"
Write-Host " TensorRT:   $trtDir"
Write-Host "================================================================" -ForegroundColor Cyan
Write-Host ""

$trtArgs = @(
    "--onnx=$OnnxPath",
    "--saveEngine=$EnginePath",
    "--memPoolSize=workspace:${WorkspaceMB}M"
)
if ($precision -eq "fp16") { $trtArgs += "--fp16" }
if ($TrtVerbose) { $trtArgs += "--verbose" }

& $trtexec @trtArgs
$exit = $LASTEXITCODE
$env:PATH = $oldPath

if ($exit -eq 0 -and (Test-Path $EnginePath)) {
    $size = (Get-Item $EnginePath).Length / 1MB
    Write-Host ""
    Write-Host "Conversion complete: $EnginePath ($([math]::Round($size, 2)) MB)" -ForegroundColor Green
} else {
    Write-Host ""
    Write-Host "If you see 'cublas64_11.dll not found' or 0xC0000135: TensorRT 8.6 requires CUDA 11 or 12." -ForegroundColor Yellow
    Write-Host "Install CUDA 11.8 or 12.x, then set: `$env:CUDAToolkit_ROOT = 'C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA\v11.8'" -ForegroundColor Yellow
}
exit $exit
