# Golf Sim -- Ball & Putter Detection Pipeline

Real-time golf ball and putter detection on a putting green, designed to feed
tracking data into Unreal Engine for a golf simulator.

## Architecture

```
┌──────────────┐    ┌──────────────┐    ┌──────────────────────────┐
│  Python       │    │  Bash Script │    │  C++ Inference Pipeline  │
│  (YOLOv10)    │───>│  ONNX→TRT    │───>│  TensorRT + OpenCV       │
│  Train/Export │    │  Conversion  │    │  Track → UDP → Unreal    │
└──────────────┘    └──────────────┘    └──────────────────────────┘
```

---

## Prerequisites

- **NVIDIA GPU** with CUDA support
- **CUDA Toolkit** >= 11.8
- **TensorRT** >= 8.6
- **OpenCV** >= 4.8 (with C++ and Python)
- **CMake** >= 3.18
- **Python** >= 3.10
- **ffmpeg** (for frame capture)

---

## Quick Start

```bash
# 1. Create conda environment
conda create -n golf-sim python=3.10 -y
conda activate golf-sim

# 2. Install Python dependencies
pip install -r requirements.txt
```

---

## Workflow Overview

The full pipeline from data collection to real-time inference:

| Step | Script | Description |
|------|--------|-------------|
| 1 | `scripts/capture_frames.sh` | Capture frames from camera |
| 2 | `python/auto_label.py` | Auto-label images with GroundingDINO |
| 3 | `python/import_to_label_studio.py` | Review and correct labels in Label Studio |
| 4 | `python/split_dataset.py` | Split data into train/val sets |
| 5 | `python/detect_golf_ball.py train` | Train YOLOv10 model |
| 6 | `python/detect_golf_ball.py export` | Export trained model to ONNX |
| 7 | `scripts/convert_onnx_to_trt.sh` | Convert ONNX to TensorRT engine |
| 8 | C++ `golf_sim` executable | Real-time inference and tracking |

---

## Commands Reference

### 1. Capture Frames from Camera

Captures every Nth frame from a camera or video using ffmpeg. Includes a
5-second countdown before capture begins. New captures never overwrite
existing frames.

```bash
# Capture from webcam at 1080p@120fps, every 3rd frame, for 15 seconds
./scripts/capture_frames.sh \
    --source /dev/video0 \
    --fps 120 \
    --resolution 1920x1080 \
    --every 3 \
    --duration 15

# Capture from a video file, every 5th frame
./scripts/capture_frames.sh \
    --source recording.mp4 \
    --every 5 \
    --output data/images/val
```

| Flag | Default | Description |
|------|---------|-------------|
| `--source PATH` | `/dev/video0` | Camera device or video file |
| `--fps N` | `120` | Camera capture framerate |
| `--resolution WxH` | `1920x1080` | Capture resolution |
| `--every N` | `3` | Keep every Nth frame (3-5 recommended) |
| `--output DIR` | `data/images/train` | Output directory |
| `--prefix NAME` | `frame` | Filename prefix |
| `--format EXT` | `png` | Image format (`png` or `jpg`) |
| `--duration SECS` | `15` | Stop after N seconds |

---

### 2. Auto-Label Images

Uses GroundingDINO (zero-shot detection) to automatically generate YOLO-format
bounding box labels for golf balls and putters.

```bash
cd python

# Auto-label training images
python auto_label.py \
    --images ../data/images/train \
    --labels ../data/labels/train

# With higher confidence threshold and overwrite existing labels
python auto_label.py \
    --images ../data/images/train \
    --labels ../data/labels/train \
    --conf 0.4 \
    --overwrite
```

| Flag | Default | Description |
|------|---------|-------------|
| `--images DIR` | `../data/images/train` | Directory of images to label |
| `--labels DIR` | `../data/labels/train` | Output directory for YOLO `.txt` labels |
| `--conf FLOAT` | `0.3` | Detection confidence threshold |
| `--overwrite` | off | Overwrite existing label files |

---

### 3. Review Labels in Label Studio

Imports images and auto-generated labels into Label Studio for manual review
and correction. Can also export corrected labels back to YOLO format.

```bash
# Start Label Studio (run in a separate terminal)
label-studio start

cd python

# Import images and pre-annotations (creates a new project)
python import_to_label_studio.py \
    --images ../data/images/train \
    --labels ../data/labels/train \
    --email your@email.com \
    --password yourpassword

# Re-import into an EXISTING project (clears old tasks, re-uploads everything)
# Use this when training data has been updated -- no need to delete the project
python import_to_label_studio.py \
    --project-id 8 \
    --images ../data/images/train \
    --labels ../data/labels/train \
    --email your@email.com \
    --password yourpassword

# Export corrected labels back to YOLO format
python import_to_label_studio.py \
    --export \
    --project-id 8 \
    --email your@email.com \
    --password yourpassword \
    --output ../data/labels/train
```

| Flag | Default | Description |
|------|---------|-------------|
| `--images DIR` | `../data/images/train` | Image directory to import |
| `--labels DIR` | `../data/labels/train` | YOLO label directory for pre-annotations |
| `--ls-url URL` | `http://localhost:8080` | Label Studio URL |
| `--email EMAIL` | *required* | Label Studio account email |
| `--password PASS` | *required* | Label Studio account password |
| `--project-name NAME` | `Golf Ball Detection` | Project name (only when creating new) |
| `--project-id ID` | -- | Reuse existing project (import) or export from (export) |
| `--export` | off | Switch to export mode |
| `--output DIR` | `../data/labels/train` | Output dir for exported labels |

---

### 4. Split Dataset

Splits the dataset into training and validation sets using temporal-chunk
shuffling to avoid near-duplicate frames crossing the split boundary.

```bash
cd python

# Default 80/20 split
python split_dataset.py

# 90/10 split
python split_dataset.py --val-ratio 0.1

# Preview what would be moved without actually moving
python split_dataset.py --dry-run
```

| Flag | Default | Description |
|------|---------|-------------|
| `--train-images DIR` | `../data/images/train` | Training images directory |
| `--val-images DIR` | `../data/images/val` | Validation images directory |
| `--train-labels DIR` | `../data/labels/train` | Training labels directory |
| `--val-labels DIR` | `../data/labels/val` | Validation labels directory |
| `--val-ratio FLOAT` | `0.2` | Fraction reserved for validation |
| `--chunk-size N` | `5` | Consecutive frames kept together |
| `--seed N` | `42` | Random seed for reproducibility |
| `--dry-run` | off | Preview changes without moving files |

---

### 5. Train YOLOv10 Model

Trains a YOLOv10 model on the labeled dataset.

```bash
cd python

python detect_golf_ball.py train \
    --data ../configs/golf_ball_dataset.yaml \
    --weights yolov10n.pt \
    --epochs 100 \
    --img-size 640 \
    --batch 16
```

| Flag | Default | Description |
|------|---------|-------------|
| `--data PATH` | *required* | Path to dataset YAML config |
| `--weights PATH` | `yolov10n.pt` | Base model weights |
| `--epochs N` | `100` | Number of training epochs |
| `--img-size N` | `640` | Input image size |
| `--batch N` | `16` | Batch size |

---

### 6. Run Inference (Test)

Run the trained model on images, video, or a live camera feed.

```bash
cd python

python detect_golf_ball.py detect \
    --source path/to/video.mp4 \
    --weights runs/train/golf_ball_detector/weights/best.pt \
    --conf 0.5 \
    --show
```

| Flag | Default | Description |
|------|---------|-------------|
| `--source PATH` | *required* | Image, video, or camera index |
| `--weights PATH` | *required* | Trained model weights |
| `--img-size N` | `640` | Input image size |
| `--conf FLOAT` | `0.5` | Confidence threshold |
| `--show` | off | Display results in a window |

---

### 7. Export to ONNX

Export the trained PyTorch model to ONNX format for TensorRT conversion.

```bash
cd python

python detect_golf_ball.py export \
    --weights runs/train/golf_ball_detector/weights/best.pt \
    --img-size 640
```

| Flag | Default | Description |
|------|---------|-------------|
| `--weights PATH` | *required* | Trained `.pt` weights file |
| `--img-size N` | `640` | Input image size |

---

### 8. Convert ONNX to TensorRT

Converts an ONNX model to an optimized TensorRT engine using `trtexec`.

```bash
# FP16 conversion (recommended -- good balance of speed and accuracy)
./scripts/convert_onnx_to_trt.sh models/best.onnx models/golf.engine --fp16

# FP32 (higher precision, slower)
./scripts/convert_onnx_to_trt.sh models/best.onnx models/golf.engine --fp32

# INT8 (fastest, requires calibration cache)
./scripts/convert_onnx_to_trt.sh models/best.onnx models/golf.engine \
    --int8 --calib-cache models/calib.cache
```

| Flag | Default | Description |
|------|---------|-------------|
| `<input.onnx>` | *required* | Input ONNX model file |
| `[output.engine]` | auto | Output engine path (defaults to `.engine` extension) |
| `--fp16` | default | Use FP16 precision |
| `--fp32` | -- | Use FP32 precision |
| `--int8` | -- | Use INT8 precision |
| `--calib-cache FILE` | -- | INT8 calibration cache |
| `--workspace MB` | `4096` | GPU workspace size (MiB) |
| `--batch-size N` | `1` | Max batch size |
| `--input-name NAME` | `images` | ONNX input tensor name |
| `--input-shape SHAPE` | `1x3x640x640` | Input shape |
| `--verbose` | off | Verbose logging |

---

### 9. C++ Real-Time Inference

Build and run the C++ inference pipeline that loads the TensorRT engine,
processes camera frames, tracks objects, and sends results to Unreal Engine
over UDP.

#### Build

```bash
cd cpp
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)

# If TensorRT is in a non-standard location:
cmake .. -DCMAKE_BUILD_TYPE=Release -DTENSORRT_DIR=/path/to/TensorRT
```

#### Run

```bash
# Webcam with GUI preview
./golf_sim --engine ../../models/golf.engine --source 0

# Video file with custom Unreal Engine endpoint
./golf_sim --engine ../../models/golf.engine \
           --source ../../data/test_video.mp4 \
           --host 192.168.1.100 --port 7001

# Headless mode (no preview window)
./golf_sim --engine ../../models/golf.engine --source 0 --no-gui
```

| Flag | Default | Description |
|------|---------|-------------|
| `--engine PATH` | *required* | Path to TensorRT `.engine` file |
| `--source SRC` | `0` | Camera index or video file path |
| `--host HOST` | `127.0.0.1` | Unreal Engine UDP host |
| `--port PORT` | `7001` | Unreal Engine UDP port |
| `--conf THRESH` | `0.5` | Detection confidence threshold |
| `--no-gui` | off | Disable OpenCV preview window |

---

### 10. Clear Training Data

Removes all captured images and labels while keeping the directory structure.

```bash
# Interactive (asks for confirmation)
bash scripts/clear_training_data.sh

# Skip confirmation
bash scripts/clear_training_data.sh --force
```

| Flag | Default | Description |
|------|---------|-------------|
| `--force` | off | Skip confirmation prompt |

---

## Unreal Engine Integration

The C++ pipeline sends JSON datagrams over UDP on every frame:

```json
{
  "timestamp_ms": 1708099200000,
  "ball": {
    "x": 320.5, "y": 240.1,
    "vx": 15.2, "vy": -8.7,
    "conf": 0.95,
    "visible": true
  },
  "putter": {
    "x": 310.0, "y": 260.3,
    "vx": 0.0, "vy": 0.0,
    "conf": 0.88,
    "visible": true
  }
}
```

In your Unreal project, create a UDP listener on port **7001** and parse the
incoming JSON to drive your game logic (ball physics, putter position, etc.).

---

## Dataset Format

```
data/
├── images/
│   ├── train/    # Training images (.png)
│   └── val/      # Validation images (.png)
└── labels/
    ├── train/    # YOLO-format .txt labels
    └── val/
```

Each label file has one line per object:

```
<class_id> <x_center> <y_center> <width> <height>
```

| Class ID | Name |
|----------|------|
| 0 | golf_ball |
| 1 | putter |

---

## Project Structure

```
golf-sim/
├── README.md                          # This file
├── requirements.txt                   # Python dependencies
├── configs/
│   └── golf_ball_dataset.yaml        # YOLO dataset config
├── scripts/
│   ├── capture_frames.sh            # Frame capture from camera
│   ├── clear_training_data.sh       # Remove all training data
│   └── convert_onnx_to_trt.sh       # ONNX to TensorRT conversion
├── python/
│   ├── detect_golf_ball.py           # Train, detect, and export YOLOv10
│   ├── auto_label.py                 # Auto-label with GroundingDINO
│   ├── import_to_label_studio.py     # Label Studio import/export
│   └── split_dataset.py             # Train/val dataset splitter
├── cpp/
│   ├── CMakeLists.txt               # C++ build system
│   ├── include/
│   │   ├── trt_engine.h             # TensorRT engine wrapper
│   │   ├── frame_pipeline.h         # OpenCV frame processing
│   │   ├── tracker.h                # EMA object tracker
│   │   └── unreal_sender.h          # UDP sender for Unreal Engine
│   └── src/
│       ├── main.cpp                 # C++ entry point
│       ├── trt_engine.cpp
│       ├── frame_pipeline.cpp
│       ├── tracker.cpp
│       └── unreal_sender.cpp
├── data/
│   ├── images/{train,val}/          # Image data
│   └── labels/{train,val}/          # YOLO label files
└── models/                           # ONNX and TensorRT engines
```
