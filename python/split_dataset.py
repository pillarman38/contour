"""
Split Dataset into Train / Validation Sets

Moves a percentage of images (and their matching YOLO label files) from the
training set into the validation set.  Uses temporal-chunk shuffling to avoid
putting near-duplicate consecutive frames into different sets.

Usage:
    python split_dataset.py [OPTIONS]

Examples:
    # Default 80/20 split
    python split_dataset.py

    # 90/10 split
    python split_dataset.py --val-ratio 0.1

    # Custom paths
    python split_dataset.py \
        --train-images ../data/images/train \
        --val-images   ../data/images/val \
        --train-labels ../data/labels/train \
        --val-labels   ../data/labels/val

    # Dry run (show what would be moved without moving)
    python split_dataset.py --dry-run
"""

import argparse
import random
import shutil
from pathlib import Path

IMAGE_EXTENSIONS = {".jpg", ".jpeg", ".png", ".bmp", ".tif", ".tiff", ".webp"}


def collect_images(directory: Path) -> list[Path]:
    """Gather image files sorted by name."""
    return sorted(
        p for p in directory.iterdir()
        if p.suffix.lower() in IMAGE_EXTENSIONS
    )


def chunk_list(lst: list, chunk_size: int) -> list[list]:
    """Split a list into contiguous chunks of the given size."""
    return [lst[i:i + chunk_size] for i in range(0, len(lst), chunk_size)]


def split_dataset(
    train_images_dir: Path,
    val_images_dir: Path,
    train_labels_dir: Path,
    val_labels_dir: Path,
    val_ratio: float = 0.2,
    chunk_size: int = 5,
    seed: int = 42,
    dry_run: bool = False,
):
    """Move a portion of training images/labels to validation."""

    images = collect_images(train_images_dir)
    if not images:
        print(f"[ERROR] No images found in {train_images_dir}")
        return

    # Ensure output dirs exist
    if not dry_run:
        val_images_dir.mkdir(parents=True, exist_ok=True)
        val_labels_dir.mkdir(parents=True, exist_ok=True)

    # Group into temporal chunks so consecutive frames stay together
    chunks = chunk_list(images, chunk_size)

    # Shuffle chunks (not individual frames) to avoid near-duplicates
    # ending up in different sets
    random.seed(seed)
    random.shuffle(chunks)

    val_chunk_count = max(1, int(len(chunks) * val_ratio))
    val_chunks = chunks[:val_chunk_count]
    val_images = [img for chunk in val_chunks for img in chunk]

    print(f"[INFO] Total images:       {len(images)}")
    print(f"[INFO] Chunk size:         {chunk_size} frames")
    print(f"[INFO] Total chunks:       {len(chunks)}")
    print(f"[INFO] Val chunks:         {val_chunk_count}")
    print(f"[INFO] Val images:         {len(val_images)} ({len(val_images)/len(images)*100:.1f}%)")
    print(f"[INFO] Train images:       {len(images) - len(val_images)} ({(len(images)-len(val_images))/len(images)*100:.1f}%)")
    print(f"[INFO] Random seed:        {seed}")
    print()

    if dry_run:
        print("[DRY RUN] The following files would be moved to validation:")
        for img in val_images:
            print(f"  {img.name}")
        print(f"\n[DRY RUN] {len(val_images)} images would be moved. No files changed.")
        return

    moved_images = 0
    moved_labels = 0

    for img_path in val_images:
        # Move image
        dest_img = val_images_dir / img_path.name
        shutil.move(str(img_path), str(dest_img))
        moved_images += 1

        # Move matching label if it exists
        label_path = train_labels_dir / (img_path.stem + ".txt")
        if label_path.exists():
            dest_label = val_labels_dir / label_path.name
            shutil.move(str(label_path), str(dest_label))
            moved_labels += 1

    print("═" * 60)
    print(f"  Moved {moved_images} images to {val_images_dir}")
    print(f"  Moved {moved_labels} labels to {val_labels_dir}")
    print(f"  Remaining in train: {len(collect_images(train_images_dir))} images")
    print("═" * 60)


def build_arg_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Split dataset into train/val sets"
    )
    parser.add_argument(
        "--train-images", type=str, default="../data/images/train",
        help="Training images directory (default: ../data/images/train)"
    )
    parser.add_argument(
        "--val-images", type=str, default="../data/images/val",
        help="Validation images directory (default: ../data/images/val)"
    )
    parser.add_argument(
        "--train-labels", type=str, default="../data/labels/train",
        help="Training labels directory (default: ../data/labels/train)"
    )
    parser.add_argument(
        "--val-labels", type=str, default="../data/labels/val",
        help="Validation labels directory (default: ../data/labels/val)"
    )
    parser.add_argument(
        "--val-ratio", type=float, default=0.2,
        help="Fraction of data to use for validation (default: 0.2)"
    )
    parser.add_argument(
        "--chunk-size", type=int, default=5,
        help="Consecutive frames kept together to avoid near-duplicates "
             "crossing the split boundary (default: 5)"
    )
    parser.add_argument(
        "--seed", type=int, default=42,
        help="Random seed for reproducibility (default: 42)"
    )
    parser.add_argument(
        "--dry-run", action="store_true",
        help="Show what would be moved without actually moving files"
    )
    return parser


def main():
    parser = build_arg_parser()
    args = parser.parse_args()

    split_dataset(
        train_images_dir=Path(args.train_images),
        val_images_dir=Path(args.val_images),
        train_labels_dir=Path(args.train_labels),
        val_labels_dir=Path(args.val_labels),
        val_ratio=args.val_ratio,
        chunk_size=args.chunk_size,
        seed=args.seed,
        dry_run=args.dry_run,
    )


if __name__ == "__main__":
    main()
