"""
Import images and auto-generated YOLO labels into Label Studio.

Sets up a Label Studio project with a bounding-box labeling configuration for
golf_ball and putter, imports images, and pre-annotates them with the YOLO
labels produced by auto_label.py.

Prerequisites:
    1. Label Studio running locally:  label-studio start
    2. An API key (from Account & Settings in the Label Studio UI)

Usage:
    # Import training images + labels
    python import_to_label_studio.py \
        --images ../data/images/train \
        --labels ../data/labels/train \
        --api-key YOUR_API_KEY

    # Export corrected labels back to YOLO format
    python import_to_label_studio.py --export --project-id 1 --api-key YOUR_API_KEY
"""

import argparse
import json
import os
import sys
import time
from pathlib import Path

import requests as _requests

from label_studio_sdk import Client

# #region agent log
LOG_PATH = "/home/connorwoodford/Desktop/projects/golf-sim/.cursor/debug.log"
def _dbg(hypothesisId, location, message, data=None):
    import json as _j
    entry = {"hypothesisId": hypothesisId, "location": location, "message": message, "data": data or {}, "timestamp": int(time.time()*1000)}
    with open(LOG_PATH, "a") as f:
        f.write(_j.dumps(entry) + "\n")
# #endregion

# ── Constants ────────────────────────────────────────────────────────────────
DEFAULT_LS_URL = "http://localhost:8080"

CLASS_ID_TO_NAME = {0: "golf_ball", 1: "putter"}
CLASS_NAME_TO_ID = {"golf_ball": 0, "putter": 1}

IMAGE_EXTENSIONS = {".jpg", ".jpeg", ".png", ".bmp", ".tif", ".tiff", ".webp"}

# Label Studio labeling config for bounding-box annotation
LABELING_CONFIG = """
<View>
  <Image name="image" value="$image"/>
  <RectangleLabels name="label" toName="image">
    <Label value="golf_ball" background="#00FF00"/>
    <Label value="putter" background="#FF00FF"/>
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
    # #region agent log
    _dbg("FIX", "create_session", "Session login succeeded", {"status": check.status_code})
    # #endregion
    return sess


def import_to_label_studio(
    images_dir: Path,
    labels_dir: Path,
    ls_url: str,
    email: str,
    password: str,
    project_name: str = "Golf Ball Detection",
    project_id: int | None = None,
):
    """Create or reuse a project, import images, and attach pre-annotations."""

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
                # #region agent log
                _dbg("FIX", "import:delete_tasks", "Delete old tasks", {"status": del_resp.status_code, "count": len(task_ids)})
                # #endregion
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
        # #region agent log
        _dbg("FIX", "import:create_project", "Create project response", {"status": resp.status_code, "body": resp.text[:300]})
        # #endregion
        resp.raise_for_status()
        project_data = resp.json()
        project_id = project_data["id"]
        print(f"[INFO] Created project: '{project_name}' (id={project_id})")

    # Collect images
    image_files = sorted(
        p for p in images_dir.iterdir()
        if p.suffix.lower() in IMAGE_EXTENSIONS
    )
    if not image_files:
        print(f"[ERROR] No images in {images_dir}")
        sys.exit(1)

    print(f"[INFO] Uploading {len(image_files)} images via import endpoint...")

    # Upload images directly through the import endpoint (multipart form data).
    # This creates tasks automatically.
    for idx, img_path in enumerate(image_files, start=1):
        csrf = sess.cookies.get("csrftoken", "")
        with open(img_path, "rb") as f:
            resp = sess.post(
                f"{ls_url}/api/projects/{project_id}/import",
                files={"file": (img_path.name, f, "image/png")},
                headers={"X-CSRFToken": csrf},
            )
        # #region agent log
        _dbg("H1", f"import:upload_{idx}", "File import result", {"file": img_path.name, "status": resp.status_code, "body": resp.text[:300]})
        # #endregion
        if resp.status_code in (200, 201):
            print(f"  [{idx}/{len(image_files)}] Imported: {img_path.name}")
        else:
            print(f"  [{idx}/{len(image_files)}] FAILED ({resp.status_code}): {img_path.name}")

    # Fetch all tasks from the project to get task IDs and map to filenames
    print("[INFO] Fetching tasks to attach predictions...")
    filename_to_task_id = {}
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
            # LS prepends a UUID prefix to uploaded filenames, e.g.
            # "/data/upload/20/85d18acf-frame_000001.png"
            # Match by checking which original filename the URL ends with.
            for img in image_files:
                if image_url.endswith(img.name):
                    filename_to_task_id[img.name] = task_id
                    break
        # #region agent log
        _dbg("H2", "import:fetch_tasks", "Fetched tasks page", {"page": page, "count": len(tasks_list), "sample_map": dict(list(filename_to_task_id.items())[:3])})
        # #endregion
        if isinstance(data, dict) and data.get("next"):
            page += 1
        else:
            break

    # #region agent log
    _dbg("H2", "import:task_map", "Final filename->task_id map", {"total": len(filename_to_task_id), "entries": dict(list(filename_to_task_id.items())[:5])})
    # #endregion
    print(f"[INFO] Matched {len(filename_to_task_id)} tasks to filenames")

    # Add pre-annotations (predictions) to tasks that have matching YOLO labels
    pre_annotated = 0
    for img_path in image_files:
        task_id = filename_to_task_id.get(img_path.name)
        if not task_id:
            continue

        label_path = labels_dir / (img_path.stem + ".txt")
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
            # #region agent log
            _dbg("FIX", "import:pred_fail", "Prediction add failed", {"task_id": task_id, "status": pred_resp.status_code, "body": pred_resp.text[:200]})
            # #endregion
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
        help="Image directory to import"
    )
    parser.add_argument(
        "--labels", type=str, default="../data/labels/train",
        help="YOLO label directory for pre-annotations"
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
        import_to_label_studio(
            images_dir=Path(args.images),
            labels_dir=Path(args.labels),
            ls_url=args.ls_url,
            email=args.email,
            password=args.password,
            project_name=args.project_name,
            project_id=args.project_id,
        )


if __name__ == "__main__":
    main()
