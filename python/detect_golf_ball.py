"""
Golf Ball Detection using YOLOv10

Detects golf balls (and optionally putters) on a golf green using
an ultralytics YOLOv10 model.  Supports:
  - Training on a custom dataset
  - Running inference on images / video / webcam
  - Exporting the trained model to ONNX for TensorRT conversion
"""

import argparse
import sys
import time
from pathlib import Path

import cv2
import numpy as np
from ultralytics import YOLO


# ── Class IDs ────────────────────────────────────────────────────────────────
CLASS_NAMES = {0: "golf_ball", 1: "putter"}
CLASS_COLORS = {0: (0, 255, 0), 1: (255, 0, 255)}  # BGR: green for ball, magenta for putter


def detect_ball_by_color(frame, min_radius=5, max_radius=80):
    """Detect the golf ball using color filtering (white blob on green surface).

    Returns a list of (cx, cy, radius) tuples for detected balls.
    Works best from an overhead camera looking at a green putting surface.
    """
    hsv = cv2.cvtColor(frame, cv2.COLOR_BGR2HSV)

    # Step 1: Find the green putting surface to narrow the search area.
    # HSV range for green/yellow-green mat
    green_lo = np.array([25, 40, 40])
    green_hi = np.array([85, 255, 255])
    green_mask = cv2.inRange(hsv, green_lo, green_hi)

    # Dilate the green mask to include the ball area sitting on the mat
    kernel = cv2.getStructuringElement(cv2.MORPH_ELLIPSE, (30, 30))
    green_region = cv2.dilate(green_mask, kernel, iterations=2)

    # Step 2: Find white/bright areas (the golf ball)
    # White in HSV = low saturation, high value
    white_lo = np.array([0, 0, 180])
    white_hi = np.array([180, 60, 255])
    white_mask = cv2.inRange(hsv, white_lo, white_hi)

    # Only keep white areas near the green surface
    ball_mask = cv2.bitwise_and(white_mask, green_region)

    # Clean up noise
    ball_mask = cv2.morphologyEx(ball_mask, cv2.MORPH_OPEN,
                                  cv2.getStructuringElement(cv2.MORPH_ELLIPSE, (3, 3)))
    ball_mask = cv2.morphologyEx(ball_mask, cv2.MORPH_CLOSE,
                                  cv2.getStructuringElement(cv2.MORPH_ELLIPSE, (7, 7)))

    # Step 3: Find circular contours
    contours, _ = cv2.findContours(ball_mask, cv2.RETR_EXTERNAL, cv2.CHAIN_APPROX_SIMPLE)

    detections = []
    for cnt in contours:
        area = cv2.contourArea(cnt)
        if area < min_radius * min_radius * 3:
            continue

        (cx, cy), radius = cv2.minEnclosingCircle(cnt)
        radius = int(radius)
        if radius < min_radius or radius > max_radius:
            continue

        # Check circularity: area vs enclosing circle area
        circularity = area / (np.pi * radius * radius) if radius > 0 else 0
        if circularity < 0.4:
            continue

        detections.append((int(cx), int(cy), radius))

    # Sort by area (largest first) and return top candidates
    detections.sort(key=lambda d: d[2], reverse=True)
    return detections[:3]


def train(
    data_yaml: str,
    weights: str = "yolov10n.pt",
    epochs: int = 100,
    img_size: int = 640,
    batch_size: int = 32,
    project: str = "runs/train",
    name: str = "golf_ball_detector",
):
    """Fine-tune YOLOv10 on the golf-ball dataset."""
    model = YOLO(weights)
    model.train(
        data=data_yaml,
        epochs=epochs,
        imgsz=img_size,
        batch=batch_size,
        project=project,
        name=name,
        patience=20,
        save=True,
        plots=True,
    )
    print(f"[INFO] Training complete. Results in {project}/{name}")
    return model


def export_onnx(weights: str, img_size: int = 640, opset: int = 13):
    """Export a trained YOLOv10 model to ONNX format."""
    model = YOLO(weights)
    onnx_path = model.export(format="onnx", imgsz=img_size, opset=opset, simplify=True)
    print(f"[INFO] ONNX model exported to {onnx_path}")
    return onnx_path


def detect(
    source: str,
    weights: str,
    img_size: int = 640,
    conf_thresh: float = 0.5,
    save_results: bool = True,
    show: bool = False,
):
    """Run inference on an image, video, or camera stream."""
    model = YOLO(weights)
    results = model.predict(
        source=source,
        imgsz=img_size,
        conf=conf_thresh,
        save=save_results,
        show=show,
        stream=True,
    )

    for frame_idx, result in enumerate(results):
        boxes = result.boxes
        if boxes is None or len(boxes) == 0:
            continue

        for box in boxes:
            cls_id = int(box.cls[0])
            confidence = float(box.conf[0])
            x1, y1, x2, y2 = box.xyxy[0].tolist()
            label = CLASS_NAMES.get(cls_id, f"class_{cls_id}")
            cx = (x1 + x2) / 2.0
            cy = (y1 + y2) / 2.0
            print(
                f"[Frame {frame_idx:04d}] {label} "
                f"conf={confidence:.2f} "
                f"bbox=({x1:.0f},{y1:.0f},{x2:.0f},{y2:.0f}) "
                f"center=({cx:.1f},{cy:.1f})"
            )


def live(
    source: int | str = 0,
    weights: str = "best.pt",
    img_size: int = 640,
    conf_thresh: float = 0.3,
    use_color: bool = False,
):
    """Live camera feed with real-time detection overlay."""
    model = None
    if weights:
        model = YOLO(weights)

    # Open camera or video source
    cap = cv2.VideoCapture(int(source) if str(source).isdigit() else source)
    if not cap.isOpened():
        print(f"[ERROR] Cannot open source: {source}")
        sys.exit(1)

    cap.set(cv2.CAP_PROP_FRAME_WIDTH, 1920)
    cap.set(cv2.CAP_PROP_FRAME_HEIGHT, 1080)

    actual_w = int(cap.get(cv2.CAP_PROP_FRAME_WIDTH))
    actual_h = int(cap.get(cv2.CAP_PROP_FRAME_HEIGHT))
    print(f"[INFO] Camera opened: {actual_w}x{actual_h}")
    if model:
        print(f"[INFO] YOLO model: {weights}")
        print(f"[INFO] Confidence threshold: {conf_thresh}")
    if use_color:
        print("[INFO] Color-based ball detection: ENABLED (cyan circles)")
    print("[INFO] Press 'q' or ESC to quit")
    print()

    prev_time = time.time()
    frame_count = 0
    fps = 0.0

    while True:
        ret, frame = cap.read()
        if not ret:
            break

        # ── YOLO detections ──────────────────────────────────────────
        if model:
            results = model.predict(
                source=frame,
                imgsz=img_size,
                conf=conf_thresh,
                verbose=False,
            )

            for result in results:
                boxes = result.boxes
                if boxes is None:
                    continue

                for box in boxes:
                    cls_id = int(box.cls[0])
                    confidence = float(box.conf[0])
                    x1, y1, x2, y2 = map(int, box.xyxy[0].tolist())
                    label = CLASS_NAMES.get(cls_id, f"class_{cls_id}")
                    color = CLASS_COLORS.get(cls_id, (255, 255, 255))

                    cv2.rectangle(frame, (x1, y1), (x2, y2), color, 2)

                    text = f"{label} {confidence:.2f}"
                    (tw, th), _ = cv2.getTextSize(text, cv2.FONT_HERSHEY_SIMPLEX, 0.6, 1)
                    cv2.rectangle(frame, (x1, y1 - th - 8), (x1 + tw + 4, y1), color, -1)
                    cv2.putText(frame, text, (x1 + 2, y1 - 4),
                                cv2.FONT_HERSHEY_SIMPLEX, 0.6, (0, 0, 0), 1, cv2.LINE_AA)

                    cx, cy = (x1 + x2) // 2, (y1 + y2) // 2
                    cv2.circle(frame, (cx, cy), 4, color, -1)

        # ── Color-based ball detection ───────────────────────────────
        if use_color:
            color_dets = detect_ball_by_color(frame)
            for cx, cy, radius in color_dets:
                # Cyan circle and crosshair for color detections
                cv2.circle(frame, (cx, cy), radius, (255, 255, 0), 2)
                cv2.drawMarker(frame, (cx, cy), (255, 255, 0),
                               cv2.MARKER_CROSS, 12, 2)

                text = f"ball (color) r={radius}"
                (tw, th), _ = cv2.getTextSize(text, cv2.FONT_HERSHEY_SIMPLEX, 0.5, 1)
                cv2.rectangle(frame, (cx - tw // 2, cy - radius - th - 10),
                              (cx + tw // 2 + 4, cy - radius), (255, 255, 0), -1)
                cv2.putText(frame, text, (cx - tw // 2 + 2, cy - radius - 4),
                            cv2.FONT_HERSHEY_SIMPLEX, 0.5, (0, 0, 0), 1, cv2.LINE_AA)

        # ── HUD ──────────────────────────────────────────────────────
        frame_count += 1
        now = time.time()
        elapsed = now - prev_time
        if elapsed >= 1.0:
            fps = frame_count / elapsed
            frame_count = 0
            prev_time = now

        mode_text = "YOLO"
        if use_color and model:
            mode_text = "YOLO + Color"
        elif use_color:
            mode_text = "Color Only"
        cv2.putText(frame, f"FPS: {fps:.1f} | {mode_text}", (10, 30),
                    cv2.FONT_HERSHEY_SIMPLEX, 1.0, (0, 255, 255), 2, cv2.LINE_AA)

        cv2.imshow("Golf Sim - Live Detection", frame)

        key = cv2.waitKey(1) & 0xFF
        if key in (ord("q"), 27):  # q or ESC
            break

    cap.release()
    cv2.destroyAllWindows()
    print("[INFO] Live feed stopped.")


def build_arg_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Golf ball detection with YOLOv10"
    )
    sub = parser.add_subparsers(dest="command", required=True)

    # ── train ────────────────────────────────────────────────────────────
    p_train = sub.add_parser("train", help="Train the model")
    p_train.add_argument("--data", required=True, help="Path to data.yaml")
    p_train.add_argument("--weights", default="yolov10n.pt", help="Base weights")
    p_train.add_argument("--epochs", type=int, default=100)
    p_train.add_argument("--img-size", type=int, default=640)
    p_train.add_argument("--batch", type=int, default=16)

    # ── export ───────────────────────────────────────────────────────────
    p_export = sub.add_parser("export", help="Export to ONNX")
    p_export.add_argument("--weights", required=True, help="Trained .pt file")
    p_export.add_argument("--img-size", type=int, default=640)

    # ── detect ───────────────────────────────────────────────────────────
    p_detect = sub.add_parser("detect", help="Run inference")
    p_detect.add_argument("--source", required=True, help="Image/video/camera")
    p_detect.add_argument("--weights", required=True, help="Model weights")
    p_detect.add_argument("--img-size", type=int, default=640)
    p_detect.add_argument("--conf", type=float, default=0.5)
    p_detect.add_argument("--show", action="store_true")

    # ── live ──────────────────────────────────────────────────────────
    p_live = sub.add_parser("live", help="Live camera feed with detection overlay")
    p_live.add_argument("--source", default="0", help="Camera index or video path (default: 0)")
    p_live.add_argument("--weights", default=None, help="YOLO model weights (omit for color-only mode)")
    p_live.add_argument("--img-size", type=int, default=640)
    p_live.add_argument("--conf", type=float, default=0.3)
    p_live.add_argument("--color", action="store_true",
                        help="Enable color-based ball detection (cyan circles)")

    return parser


def main():
    parser = build_arg_parser()
    args = parser.parse_args()

    if args.command == "train":
        train(
            data_yaml=args.data,
            weights=args.weights,
            epochs=args.epochs,
            img_size=args.img_size,
            batch_size=args.batch,
        )
    elif args.command == "export":
        export_onnx(weights=args.weights, img_size=args.img_size)
    elif args.command == "detect":
        detect(
            source=args.source,
            weights=args.weights,
            img_size=args.img_size,
            conf_thresh=args.conf,
            show=args.show,
        )
    elif args.command == "live":
        if not args.weights and not args.color:
            print("[ERROR] Provide --weights for YOLO, --color for color detection, or both.")
            sys.exit(1)
        live(
            source=args.source,
            weights=args.weights,
            img_size=args.img_size,
            conf_thresh=args.conf,
            use_color=args.color,
        )
    else:
        parser.print_help()
        sys.exit(1)


if __name__ == "__main__":
    main()
