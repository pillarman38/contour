#!/usr/bin/env bash
# ─────────────────────────────────────────────────────────────────────────────
# convert_onnx_to_trt.sh
#
# Converts a YOLOv10 ONNX model to a TensorRT engine.
#
# Prerequisites:
#   - TensorRT installed (trtexec on PATH)
#   - CUDA toolkit installed
#
# Usage:
#   ./convert_onnx_to_trt.sh <input.onnx> [output.engine] [options]
#
# Examples:
#   ./convert_onnx_to_trt.sh models/golf_yolov10.onnx
#   ./convert_onnx_to_trt.sh models/golf_yolov10.onnx models/golf.engine --fp16
# ─────────────────────────────────────────────────────────────────────────────
set -euo pipefail

# ── Defaults ─────────────────────────────────────────────────────────────────
PRECISION="fp16"          # fp32 | fp16 | int8
WORKSPACE_MB=4096         # max GPU workspace in MiB
BATCH_SIZE=1
INPUT_NAME="images"
INPUT_SHAPE="1x3x640x640"
VERBOSE=false
CALIBRATION_CACHE=""

usage() {
    cat <<EOF
Usage: $(basename "$0") <input.onnx> [output.engine] [OPTIONS]

Options:
  --fp32                Use FP32 precision (default: FP16)
  --fp16                Use FP16 precision
  --int8                Use INT8 precision (requires calibration data)
  --calib-cache FILE    INT8 calibration cache file
  --workspace MB        GPU workspace size in MiB (default: ${WORKSPACE_MB})
  --batch-size N        Max batch size (default: ${BATCH_SIZE})
  --input-name NAME     ONNX input tensor name (default: ${INPUT_NAME})
  --input-shape SHAPE   Input shape, e.g. 1x3x640x640 (default: ${INPUT_SHAPE})
  --verbose             Enable verbose logging
  -h, --help            Show this help
EOF
    exit 0
}

# ── Argument parsing ─────────────────────────────────────────────────────────
if [[ $# -lt 1 ]]; then
    usage
fi

ONNX_PATH="$1"; shift
ENGINE_PATH=""

# If second positional arg doesn't start with '--', treat it as output path
if [[ $# -gt 0 && ! "$1" =~ ^-- ]]; then
    ENGINE_PATH="$1"; shift
fi

while [[ $# -gt 0 ]]; do
    case "$1" in
        --fp32)         PRECISION="fp32";       shift ;;
        --fp16)         PRECISION="fp16";       shift ;;
        --int8)         PRECISION="int8";       shift ;;
        --calib-cache)  CALIBRATION_CACHE="$2"; shift 2 ;;
        --workspace)    WORKSPACE_MB="$2";      shift 2 ;;
        --batch-size)   BATCH_SIZE="$2";        shift 2 ;;
        --input-name)   INPUT_NAME="$2";        shift 2 ;;
        --input-shape)  INPUT_SHAPE="$2";       shift 2 ;;
        --verbose)      VERBOSE=true;           shift ;;
        -h|--help)      usage ;;
        *)
            echo "[ERROR] Unknown option: $1" >&2
            usage
            ;;
    esac
done

# ── Validate ─────────────────────────────────────────────────────────────────
if [[ ! -f "${ONNX_PATH}" ]]; then
    echo "[ERROR] ONNX file not found: ${ONNX_PATH}" >&2
    exit 1
fi

if ! command -v trtexec &>/dev/null; then
    echo "[ERROR] trtexec not found. Make sure TensorRT is installed and on PATH." >&2
    echo "        Typical location: /usr/src/tensorrt/bin/trtexec" >&2
    exit 1
fi

# Default engine path: same name, .engine extension
if [[ -z "${ENGINE_PATH}" ]]; then
    ENGINE_PATH="${ONNX_PATH%.onnx}.engine"
fi

# ── Build trtexec command ────────────────────────────────────────────────────
CMD=(
    trtexec
    --onnx="${ONNX_PATH}"
    --saveEngine="${ENGINE_PATH}"
    --memPoolSize=workspace:"${WORKSPACE_MB}MiB"
    --optShapes="${INPUT_NAME}:${INPUT_SHAPE}"
    --minShapes="${INPUT_NAME}:${INPUT_SHAPE}"
    --maxShapes="${INPUT_NAME}:${INPUT_SHAPE}"
)

case "${PRECISION}" in
    fp16) CMD+=(--fp16) ;;
    int8)
        CMD+=(--int8)
        if [[ -n "${CALIBRATION_CACHE}" ]]; then
            CMD+=(--calib="${CALIBRATION_CACHE}")
        fi
        ;;
    fp32) ;; # default precision, nothing to add
esac

if [[ "${VERBOSE}" == true ]]; then
    CMD+=(--verbose)
fi

# ── Run ──────────────────────────────────────────────────────────────────────
echo "════════════════════════════════════════════════════════════════════════"
echo " ONNX → TensorRT Conversion"
echo "════════════════════════════════════════════════════════════════════════"
echo " Input:      ${ONNX_PATH}"
echo " Output:     ${ENGINE_PATH}"
echo " Precision:  ${PRECISION}"
echo " Workspace:  ${WORKSPACE_MB} MiB"
echo " Batch size: ${BATCH_SIZE}"
echo " Input:      ${INPUT_NAME} [${INPUT_SHAPE}]"
echo "════════════════════════════════════════════════════════════════════════"
echo ""
echo "[INFO] Running: ${CMD[*]}"
echo ""

"${CMD[@]}"

echo ""
echo "════════════════════════════════════════════════════════════════════════"
echo " ✓ Conversion complete: ${ENGINE_PATH}"
echo "   Size: $(du -h "${ENGINE_PATH}" | cut -f1)"
echo "════════════════════════════════════════════════════════════════════════"
