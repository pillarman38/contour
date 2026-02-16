"""
Auto-Label Golf Images using GroundingDINO (via autodistill)

Runs a zero-shot open-vocabulary object detector over every image in a source
directory and writes YOLO-format .txt label files.  No training required --
GroundingDINO understands natural-language prompts like "golf ball" and "putter".

Usage:
    python auto_label.py [OPTIONS]

Examples:
    # Label training images with default settings
    python auto_label.py --images ../data/images/train --labels ../data/labels/train

    # Use a tighter confidence threshold
    python auto_label.py --images ../data/images/train --labels ../data/labels/train --conf 0.4

    # Label validation images
    python auto_label.py --images ../data/images/val --labels ../data/labels/val
"""

import argparse
import sys
from pathlib import Path

import cv2
import numpy as np

from autodistill_grounding_dino import GroundingDINO
from autodistill.detection import CaptionOntology


# ── Class mapping (must match configs/golf_ball_dataset.yaml) ────────────────
# Maps natural-language prompts → YOLO class names
ONTOLOGY = CaptionOntology({
    "small white round ball": "golf_ball",
    "metal putter head":      "putter",
})

# YOLO class IDs
CLASS_NAME_TO_ID = {
    "golf_ball": 0,
    "putter":    1,
}

IMAGE_EXTENSIONS = {".jpg", ".jpeg", ".png", ".bmp", ".tif", ".tiff", ".webp"}


def collect_images(image_dir: Path) -> list[Path]:
    """Gather all image files from a directory (non-recursive)."""
    images = sorted(
        p for p in image_dir.iterdir()
        if p.suffix.lower() in IMAGE_EXTENSIONS
    )
    return images


def xyxy_to_yolo(x1: float, y1: float, x2: float, y2: float,
                 img_w: int, img_h: int) -> tuple[float, float, float, float]:
    """Convert (x1, y1, x2, y2) pixel coords to YOLO normalised format."""
    cx = ((x1 + x2) / 2.0) / img_w
    cy = ((y1 + y2) / 2.0) / img_h
    w = (x2 - x1) / img_w
    h = (y2 - y1) / img_h
    return (
        max(0.0, min(1.0, cx)),
        max(0.0, min(1.0, cy)),
        max(0.0, min(1.0, w)),
        max(0.0, min(1.0, h)),
    )


def auto_label(
    image_dir: Path,
    label_dir: Path,
    conf_threshold: float = 0.3,
    overwrite: bool = False,
):
    """Run GroundingDINO on every image and write YOLO .txt labels."""

    label_dir.mkdir(parents=True, exist_ok=True)
    images = collect_images(image_dir)

    if not images:
        print(f"[ERROR] No images found in {image_dir}")
        sys.exit(1)

    print(f"[INFO] Found {len(images)} images in {image_dir}")
    print(f"[INFO] Labels will be written to {label_dir}")
    print(f"[INFO] Confidence threshold: {conf_threshold}")
    print()

    # Initialise model
    model = GroundingDINO(ontology=ONTOLOGY)

    total_detections = 0
    labeled_count = 0

    for idx, img_path in enumerate(images, start=1):
        label_path = label_dir / (img_path.stem + ".txt")

        if label_path.exists() and not overwrite:
            print(f"  [{idx}/{len(images)}] SKIP (label exists): {img_path.name}")
            continue

        # Load image to get dimensions
        img = cv2.imread(str(img_path))
        if img is None:
            print(f"  [{idx}/{len(images)}] SKIP (unreadable): {img_path.name}")
            continue
        img_h, img_w = img.shape[:2]

        # Run detection
        detections = model.predict(str(img_path))

        # Filter by confidence and write labels
        lines = []
        for i in range(len(detections.xyxy)):
            conf = detections.confidence[i]
            if conf < conf_threshold:
                continue

            class_idx = int(detections.class_id[i])
            class_name = list(CLASS_NAME_TO_ID.keys())[class_idx] if class_idx < len(CLASS_NAME_TO_ID) else None
            if class_name is None:
                continue
            yolo_id = CLASS_NAME_TO_ID[class_name]

            x1, y1, x2, y2 = detections.xyxy[i]
            cx, cy, w, h = xyxy_to_yolo(float(x1), float(y1), float(x2), float(y2), img_w, img_h)

            lines.append(f"{yolo_id} {cx:.6f} {cy:.6f} {w:.6f} {h:.6f}")

        # Write label file (even if empty -- signals the image was processed)
        label_path.write_text("\n".join(lines) + ("\n" if lines else ""))

        total_detections += len(lines)
        labeled_count += 1

        det_summary = ", ".join(
            f"{line.split()[0]}={CLASS_NAME_TO_ID}" for line in lines
        ) if lines else "no detections"
        print(f"  [{idx}/{len(images)}] {img_path.name} → {len(lines)} detections")

    print()
    print("═" * 60)
    print(f"  Done: {labeled_count} images labeled, {total_detections} total detections")
    print(f"  Labels in: {label_dir}")
    print("═" * 60)


def build_arg_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Auto-label golf images using GroundingDINO"
    )
    parser.add_argument(
        "--images", type=str, default="../data/images/train",
        help="Directory containing images to label (default: ../data/images/train)"
    )
    parser.add_argument(
        "--labels", type=str, default="../data/labels/train",
        help="Directory to write YOLO .txt labels (default: ../data/labels/train)"
    )
    parser.add_argument(
        "--conf", type=float, default=0.3,
        help="Confidence threshold for detections (default: 0.3)"
    )
    parser.add_argument(
        "--overwrite", action="store_true",
        help="Overwrite existing label files"
    )
    return parser


def main():
    parser = build_arg_parser()
    args = parser.parse_args()

    auto_label(
        image_dir=Path(args.images),
        label_dir=Path(args.labels),
        conf_threshold=args.conf,
        overwrite=args.overwrite,
    )


if __name__ == "__main__":
    main()
