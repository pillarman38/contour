"""
Import images and auto-generated YOLO labels into Label Studio.

Sets up a Label Studio project with a bounding-box labeling configuration for
golf_ball and putter, imports images, and pre-annotates them with the YOLO
labels produced by auto_label.py.

Prerequisites:
    1. Label Studio running locally:  label-studio start
    2. An API key (from Account & Settings in the Label Studio UI)

Usage:
    # Import training images + labels only
    python import_to_label_studio.py \
        --images ../data/images/train \
        --labels ../data/labels/train \
        --email YOUR_EMAIL --password YOUR_PASSWORD

    # Import train AND val into one project
    python import_to_label_studio.py --all \
        --email YOUR_EMAIL --password YOUR_PASSWORD

    # Export corrected labels back to YOLO format
    python import_to_label_studio.py --export --project-id 1 \
        --email YOUR_EMAIL --password YOUR_PASSWORD
"""

import argparse
import json
import os
import sys
from pathlib import Path

import requests as _requests

from label_studio_sdk import Client

# ── Constants ────────────────────────────────────────────────────────────────
DEFAULT_LS_URL = "http://localhost:8080"

CLASS_ID_TO_NAME = {0: "golf_ball", 1: "putter", 2: "hole"}
CLASS_NAME_TO_ID = {"golf_ball": 0, "putter": 1, "hole": 2}

IMAGE_EXTENSIONS = {".jpg", ".jpeg", ".png", ".bmp", ".tif", ".tiff", ".webp"}

# Label Studio labeling config for bounding-box annotation
LABELING_CONFIG = """
<View>
  <Image name="image" value="$image"/>
  <RectangleLabels name="label" toName="image">
    <Label value="golf_ball" background="#00FF00"/>
    <Label value="putter" background="#FF00FF"/>
    <Label value="hole" background="#0000FF"/>
  </RectangleLabels>
</View>
"""


def yolo_to_ls_bbox(class_id: int, cx: float, cy: float, w: float, h: float):
    """Convert YOLO normalised coords to Label Studio percentage-based bbox."""
    return {
        "x": (cx - w / 2) * 100.0,
        "y": (cy - h / 2) * 100.0,
        "width": w * 100.0,
        "height": h * 100.0,
        "rectanglelabels": [CLASS_ID_TO_NAME.get(class_id, f"class_{class_id}")],
    }


def parse_yolo_label(label_path: Path) -> list[dict]:
    """Parse a YOLO .txt label file into Label Studio annotation format."""
    results = []
    text = label_path.read_text().strip()
    if not text:
        return results

    for line in text.splitlines():
        parts = line.strip().split()
        if len(parts) < 5:
            continue
        class_id = int(parts[0])
        cx, cy, w, h = float(parts[1]), float(parts[2]), float(parts[3]), float(parts[4])

        bbox = yolo_to_ls_bbox(class_id, cx, cy, w, h)
        results.append({
            "from_name": "label",
            "to_name": "image",
            "type": "rectanglelabels",
            "value": bbox,
        })
    return results


def _create_session(ls_url: str, email: str, password: str) -> _requests.Session:
    """Authenticate to Label Studio via session login (email/password + CSRF)."""
    sess = _requests.Session()
    login_page = sess.get(f"{ls_url}/user/login")
    login_page.raise_for_status()
    csrf = sess.cookies.get("csrftoken", "")
    resp = sess.post(
        f"{ls_url}/user/login",
        data={"email": email, "password": password, "csrfmiddlewaretoken": csrf},
        headers={"Referer": f"{ls_url}/user/login"},
    )
    resp.raise_for_status()
    # Verify we're actually logged in
    check = sess.get(f"{ls_url}/api/current-user/whoami")
    if check.status_code != 200:
        raise RuntimeError(f"Session login failed (whoami returned {check.status_code})")
    return sess


def import_to_label_studio(
    images_dir: Path,
    labels_dir: Path,
    ls_url: str,
    email: str,
    password: str,
    project_name: str = "Golf Ball Detection",
    project_id: int | None = None,
    sources: list[tuple[Path, Path]] | None = None,
):
    """Create or reuse a project, import images, and attach pre-annotations.

    When sources is provided (e.g. for --all), it overrides images_dir/labels_dir.
    sources = [(images_dir, labels_dir), ...] for multiple directories.
    """
    sess = _create_session(ls_url, email, password)

    if project_id:
        # Reuse existing project -- delete old tasks first so we start fresh
        print(f"[INFO] Reusing existing project (id={project_id}), clearing old tasks...")
        csrf = sess.cookies.get("csrftoken", "")
        resp = sess.get(f"{ls_url}/api/tasks", params={"project": project_id, "page_size": 10000})
        if resp.status_code == 200:
            data = resp.json()
            tasks_list = data if isinstance(data, list) else data.get("tasks", data.get("results", []))
            task_ids = [t["id"] for t in tasks_list]
            if task_ids:
                csrf = sess.cookies.get("csrftoken", "")
                del_resp = sess.post(
                    f"{ls_url}/api/dm/actions",
                    params={"project": project_id, "id": "delete_tasks"},
                    json={"selectedItems": {"all": True, "excluded": []}},
                    headers={"X-CSRFToken": csrf},
                )
                print(f"[INFO] Cleared {len(task_ids)} old tasks")
        print(f"[INFO] Using project: id={project_id}")
    else:
        # Create a new project
        csrf = sess.cookies.get("csrftoken", "")
        resp = sess.post(
            f"{ls_url}/api/projects",
            json={"title": project_name, "label_config": LABELING_CONFIG},
            headers={"X-CSRFToken": csrf},
        )
        resp.raise_for_status()
        project_data = resp.json()
        project_id = project_data["id"]
        print(f"[INFO] Created project: '{project_name}' (id={project_id})")

    # Build (img_path, label_path, upload_name) entries from sources or single dir
    # upload_name is used for LS upload and matching; may have prefix to avoid duplicates
    image_entries: list[tuple[Path, Path, str]] = []
    if sources:
        seen_names: set[str] = set()
        for imdir, laldir in sources:
            if not imdir.exists():
                print(f"[WARN] Skipping missing directory: {imdir}")
                continue
            for p in sorted(imdir.iterdir()):
                if p.suffix.lower() in IMAGE_EXTENSIONS:
                    label_path = laldir / (p.stem + ".txt")
                    upload_name = p.name
                    if upload_name in seen_names and len(sources) > 1:
                        upload_name = f"{imdir.name}_{p.name}"
                    seen_names.add(upload_name)
                    image_entries.append((p, label_path, upload_name))
    else:
        for p in sorted(images_dir.iterdir()):
            if p.suffix.lower() in IMAGE_EXTENSIONS:
                label_path = labels_dir / (p.stem + ".txt")
                image_entries.append((p, label_path, p.name))

    if not image_entries:
        print("[ERROR] No images found in the given directory(ies)")
        sys.exit(1)

    # Unique upload name per entry (for matching after fetch)
    upload_names = [ent[2] for ent in image_entries]

    print(f"[INFO] Uploading {len(image_entries)} images via import endpoint...")

    for idx, (img_path, _, upload_name) in enumerate(image_entries, start=1):
        csrf = sess.cookies.get("csrftoken", "")
        with open(img_path, "rb") as f:
            resp = sess.post(
                f"{ls_url}/api/projects/{project_id}/import",
                files={"file": (upload_name, f, "image/png")},
                headers={"X-CSRFToken": csrf},
            )
        if resp.status_code in (200, 201):
            print(f"  [{idx}/{len(image_entries)}] Imported: {upload_name}")
        else:
            print(f"  [{idx}/{len(image_entries)}] FAILED ({resp.status_code}): {upload_name}")

    # Fetch all tasks from the project to get task IDs and map to filenames
    print("[INFO] Fetching tasks to attach predictions...")
    filename_to_task_id: dict[str, int] = {}
    page = 1
    while True:
        resp = sess.get(f"{ls_url}/api/tasks", params={
            "project": project_id, "page": page, "page_size": 100
        })
        resp.raise_for_status()
        data = resp.json()
        tasks_list = data if isinstance(data, list) else data.get("tasks", data.get("results", []))
        if not tasks_list:
            break
        for task in tasks_list:
            task_id = task.get("id")
            image_url = task.get("data", {}).get("image", "")
            for upload_name in upload_names:
                if image_url.endswith(upload_name):
                    filename_to_task_id[upload_name] = task_id
                    break
        if isinstance(data, dict) and data.get("next"):
            page += 1
        else:
            break

    print(f"[INFO] Matched {len(filename_to_task_id)} tasks to filenames")

    # Add pre-annotations (predictions) to tasks that have matching YOLO labels
    pre_annotated = 0
    for img_path, label_path, upload_name in image_entries:
        task_id = filename_to_task_id.get(upload_name)
        if not task_id:
            continue

        if not label_path.exists():
            continue

        results = parse_yolo_label(label_path)
        if not results:
            continue

        csrf = sess.cookies.get("csrftoken", "")
        pred_resp = sess.post(
            f"{ls_url}/api/predictions",
            json={
                "task": task_id,
                "model_version": "grounding_dino_auto",
                "result": results,
            },
            headers={"X-CSRFToken": csrf},
        )
        if pred_resp.status_code in (200, 201):
            pre_annotated += 1
        else:
            pass

    print(f"[INFO] Imported {len(filename_to_task_id)} tasks ({pre_annotated} with pre-annotations)")
    print(f"[INFO] Open Label Studio at: {ls_url}/projects/{project_id}")
    print()
    print("Review workflow:")
    print("  1. Open the URL above in your browser")
    print("  2. Click through images -- pre-annotations are shown as boxes")
    print("  3. Correct, delete, or add boxes as needed, then submit each image")
    print(f"  4. When done, export via: python import_to_label_studio.py "
          f"--export --project-id {project_id} --email <email> --password <password>")


def export_from_label_studio(
    project_id: int,
    output_dir: Path,
    ls_url: str,
    email: str,
    password: str,
):
    """Export corrected annotations from Label Studio back to YOLO format."""

    sess = _create_session(ls_url, email, password)

    # Fetch all tasks with annotations
    all_tasks = []
    page = 1
    while True:
        resp = sess.get(f"{ls_url}/api/tasks", params={
            "project": project_id, "page": page, "page_size": 100
        })
        resp.raise_for_status()
        data = resp.json()
        tasks = data.get("tasks", data) if isinstance(data, dict) else data
        if isinstance(data, dict) and "tasks" in data:
            tasks = data["tasks"]
        elif isinstance(data, list):
            tasks = data
        else:
            tasks = data.get("results", [])
        if not tasks:
            break
        all_tasks.extend(tasks)
        if isinstance(data, dict) and not data.get("next"):
            break
        page += 1

    output_dir.mkdir(parents=True, exist_ok=True)
    exported = 0

    for task in all_tasks:
        image_path = task["data"].get("image", "")
        stem = Path(image_path).stem

        annotations = task.get("annotations", [])
        if not annotations:
            continue

        latest = annotations[-1]
        results = latest.get("result", [])

        lines = []
        for r in results:
            if r.get("type") != "rectanglelabels":
                continue
            value = r["value"]
            labels = value.get("rectanglelabels", [])
            if not labels:
                continue

            label_name = labels[0]
            class_id = CLASS_NAME_TO_ID.get(label_name)
            if class_id is None:
                continue

            x_pct = value["x"]
            y_pct = value["y"]
            w_pct = value["width"]
            h_pct = value["height"]

            cx = (x_pct + w_pct / 2) / 100.0
            cy = (y_pct + h_pct / 2) / 100.0
            w = w_pct / 100.0
            h = h_pct / 100.0

            lines.append(f"{class_id} {cx:.6f} {cy:.6f} {w:.6f} {h:.6f}")

        label_path = output_dir / f"{stem}.txt"
        label_path.write_text("\n".join(lines) + ("\n" if lines else ""))
        exported += 1

    print(f"[INFO] Exported {exported} label files to {output_dir}")


def build_arg_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Import/export golf labels with Label Studio"
    )

    parser.add_argument(
        "--ls-url", default=DEFAULT_LS_URL,
        help=f"Label Studio URL (default: {DEFAULT_LS_URL})"
    )
    parser.add_argument(
        "--email", required=True,
        help="Label Studio account email"
    )
    parser.add_argument(
        "--password", required=True,
        help="Label Studio account password"
    )

    # Import mode (default)
    parser.add_argument(
        "--images", type=str, default="../data/images/train",
        help="Image directory to import (ignored when --all)"
    )
    parser.add_argument(
        "--labels", type=str, default="../data/labels/train",
        help="YOLO label directory for pre-annotations (ignored when --all)"
    )
    parser.add_argument(
        "--all", action="store_true",
        help="Import both train and val into one project"
    )
    parser.add_argument(
        "--images-train", type=str, default="./data/images/train",
        help="Train images dir (used with --all)"
    )
    parser.add_argument(
        "--labels-train", type=str, default="./data/labels/train",
        help="Train labels dir (used with --all)"
    )
    parser.add_argument(
        "--images-val", type=str, default="./data/images/val",
        help="Val images dir (used with --all)"
    )
    parser.add_argument(
        "--labels-val", type=str, default="./data/labels/val",
        help="Val labels dir (used with --all)"
    )
    parser.add_argument(
        "--project-name", default="Golf Ball Detection",
        help="Label Studio project name"
    )

    # Export mode
    parser.add_argument(
        "--export", action="store_true",
        help="Export mode: pull corrected labels from Label Studio"
    )
    parser.add_argument(
        "--project-id", type=int,
        help="Project ID to reuse (import) or export from (export). "
             "When importing: reuses the project instead of creating a new one. "
             "When exporting: required."
    )
    parser.add_argument(
        "--output", type=str, default="../data/labels/train",
        help="Output directory for exported YOLO labels"
    )

    return parser


def main():
    parser = build_arg_parser()
    args = parser.parse_args()

    if args.export:
        if not args.project_id:
            print("[ERROR] --project-id is required when using --export")
            sys.exit(1)
        export_from_label_studio(
            project_id=args.project_id,
            output_dir=Path(args.output),
            ls_url=args.ls_url,
            email=args.email,
            password=args.password,
        )
    else:
        sources = None
        if args.all:
            sources = [
                (Path(args.images_train), Path(args.labels_train)),
                (Path(args.images_val), Path(args.labels_val)),
            ]
            print(f"[INFO] --all: importing from train + val into one project")
        import_to_label_studio(
            images_dir=Path(args.images),
            labels_dir=Path(args.labels),
            ls_url=args.ls_url,
            email=args.email,
            password=args.password,
            project_name=args.project_name,
            project_id=args.project_id,
            sources=sources,
        )


if __name__ == "__main__":
    main()
