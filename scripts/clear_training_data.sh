#!/usr/bin/env bash
# ── Clear Training Data ──────────────────────────────────────────────────────
# Removes all captured images and labels from the data directory.
# Keeps the directory structure intact so capture/labeling scripts still work.
#
# Usage:
#   bash scripts/clear_training_data.sh          # interactive (asks for confirmation)
#   bash scripts/clear_training_data.sh --force   # skip confirmation

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
DATA_DIR="$PROJECT_ROOT/data"

FORCE=false
if [[ "${1:-}" == "--force" ]]; then
    FORCE=true
fi

# Directories to clear
DIRS=(
    "$DATA_DIR/images/train"
    "$DATA_DIR/images/val"
    "$DATA_DIR/labels/train"
    "$DATA_DIR/labels/val"
)

# Count files that will be deleted
total=0
for dir in "${DIRS[@]}"; do
    if [[ -d "$dir" ]]; then
        count=$(find "$dir" -maxdepth 1 -type f | wc -l)
        total=$((total + count))
        echo "  $dir: $count files"
    fi
done

if [[ $total -eq 0 ]]; then
    echo "Nothing to delete -- data directories are already empty."
    exit 0
fi

echo ""
echo "Total files to delete: $total"

if [[ "$FORCE" != true ]]; then
    read -rp "Are you sure you want to delete all training data? [y/N] " confirm
    if [[ ! "$confirm" =~ ^[Yy]$ ]]; then
        echo "Aborted."
        exit 0
    fi
fi

# Delete files but keep directories
for dir in "${DIRS[@]}"; do
    if [[ -d "$dir" ]]; then
        find "$dir" -maxdepth 1 -type f -delete
    fi
done

echo "Done. All training data has been removed."
echo "Directory structure preserved:"
for dir in "${DIRS[@]}"; do
    echo "  $dir/"
done
