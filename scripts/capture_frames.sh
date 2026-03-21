#!/usr/bin/env bash
# ─────────────────────────────────────────────────────────────────────────────
# capture_frames.sh
#
# Captures every Nth frame from a camera or video using ffmpeg.
#
# Usage:
#   ./capture_frames.sh [OPTIONS]
#
# Examples:
#   ./capture_frames.sh --source /dev/video0 --every 3 --output data/images/train
#   ./capture_frames.sh --source recording.mp4 --every 5 --output data/images/val
# ─────────────────────────────────────────────────────────────────────────────
set -euo pipefail

SOURCE="/dev/video0"
FPS=120
RESOLUTION="1920x1080"
EVERY_N=3
OUTPUT_DIR="data/images/train"
PREFIX="frame"
FORMAT="png"
DURATION="15"

usage() {
    cat <<EOF
Usage: $(basename "$0") [OPTIONS]

Options:
  --source PATH     Camera device or video file (default: /dev/video0)
  --fps N           Camera capture framerate (default: 120)
  --resolution WxH  Capture resolution (default: 1920x1080)
  --every N         Capture every Nth frame, 3-5 recommended (default: 3)
  --output DIR      Output directory for frames (default: data/images/train)
  --prefix NAME     Filename prefix (default: frame)
  --format EXT      Image format: png, jpg (default: png)
  --duration SECS   Stop after N seconds (default: until Ctrl+C)
  -h, --help        Show this help
EOF
    exit 0
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --source)   SOURCE="$2";     shift 2 ;;
        --fps)      FPS="$2";            shift 2 ;;
        --resolution) RESOLUTION="$2"; shift 2 ;;
        --every)    EVERY_N="$2";    shift 2 ;;
        --output)   OUTPUT_DIR="$2"; shift 2 ;;
        --prefix)   PREFIX="$2";     shift 2 ;;
        --format)   FORMAT="$2";     shift 2 ;;
        --duration) DURATION="$2";   shift 2 ;;
        -h|--help)  usage ;;
        *)          echo "Unknown option: $1" >&2; usage ;;
    esac
done

mkdir -p "${OUTPUT_DIR}"

# Find the highest existing frame number so we don't overwrite
EXISTING_MAX=0
for f in "${OUTPUT_DIR}/${PREFIX}_"*".${FORMAT}" ; do
    [[ -e "$f" ]] || continue
    base="$(basename "$f" ".${FORMAT}")"
    num="${base##*_}"
    num=$((10#${num}))   # strip leading zeros
    if (( num > EXISTING_MAX )); then
        EXISTING_MAX=$num
    fi
done
START_NUMBER=$(( EXISTING_MAX + 1 ))

if (( EXISTING_MAX > 0 )); then
    echo "[INFO] Found existing frames up to ${PREFIX}_$(printf '%06d' ${EXISTING_MAX}).${FORMAT}"
    echo "[INFO] New frames will start at ${PREFIX}_$(printf '%06d' ${START_NUMBER}).${FORMAT}"
fi

# Build the ffmpeg command
CMD=(ffmpeg -y)

# Input source
if [[ "${SOURCE}" == /dev/video* ]]; then
    CMD+=(-f v4l2 -video_size "${RESOLUTION}" -framerate "${FPS}" -i "${SOURCE}")
else
    CMD+=(-i "${SOURCE}")
fi

# Duration limit (optional)
if [[ -n "${DURATION}" ]]; then
    CMD+=(-t "${DURATION}")
fi

# Select every Nth frame using the select filter.
# 'not(mod(n,N))' keeps every Nth frame (0, N, 2N, 3N, ...)
# -start_number ensures we don't overwrite existing files.
CMD+=(
    -vf "select=not(mod(n\,${EVERY_N}))"
    -vsync vfr
    -start_number "${START_NUMBER}"
    "${OUTPUT_DIR}/${PREFIX}_%06d.${FORMAT}"
)

echo "════════════════════════════════════════════════════════════════"
echo " Frame Capture"
echo "════════════════════════════════════════════════════════════════"
echo " Source:    ${SOURCE}"
echo " Resolution: ${RESOLUTION}"
echo " FPS:      ${FPS}"
echo " Every:    ${EVERY_N} frames"
echo " Output:   ${OUTPUT_DIR}/${PREFIX}_XXXXXX.${FORMAT}"
echo " Duration: ${DURATION:-until Ctrl+C}"
echo "════════════════════════════════════════════════════════════════"
echo ""
echo "[INFO] Running: ${CMD[*]}"
echo "[INFO] Press Ctrl+C to stop capture"
echo ""

for i in 5 4 3 2 1; do
    echo -ne "\r[INFO] Starting capture in ${i}..."
    sleep 1
done
echo -e "\r[INFO] Capture started!            "
echo ""

"${CMD[@]}"

COUNT=$(find "${OUTPUT_DIR}" -name "${PREFIX}_*.${FORMAT}" | wc -l)
echo ""
echo "════════════════════════════════════════════════════════════════"
echo " Done — captured ${COUNT} frames in ${OUTPUT_DIR}"
echo "════════════════════════════════════════════════════════════════"
