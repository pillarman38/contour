# Golf Sim — Real-Time Putting Green Tracker & Simulator

Real-time golf ball and putter detection on a putting green, feeding
tracking data into an Angular dashboard and an Unreal Engine 5.7
visualiser over REST and UDP.

## Architecture

```
Camera
  │
  ▼
┌─────────────────────────────────────────┐
│  C++ Pipeline  (TensorRT + OpenCV)      │
│  Detect → Track → PuttStats → StatsApi  │
└───────────┬──────────────┬──────────────┘
            │ UDP (JSON)   │ REST API (:8080)
            ▼              ▼
  ┌──────────────┐   ┌─────────────────────────────┐
  │  Unreal 5.7  │   │  Angular 21 (contour/)      │
  │  3D putting  │   │  Top-down overlay, ball      │
  │  visualiser  │   │  claiming, hole selection,   │
  │              │   │  putt stats dashboard        │
  └──────────────┘   └─────────────────────────────┘
```

### Data flow

1. **C++ tracker** runs TensorRT inference on each camera frame, detecting
   balls (class 0), putters (class 1), and holes (class 2).
2. An EMA tracker smooths positions, computes velocities, detects made putts,
   and protects ball tracks from putter occlusion.
3. **REST API** (`GET /api/tracking`, `POST /api/claim-ball`,
   `POST /api/target-hole`, etc.) exposes tracking state + putt stats to the
   Angular dashboard.
4. **UDP JSON datagrams** stream to Unreal Engine every frame with positions,
   per-ball target holes, putt stats, and ball-placement hints.

---

## Prerequisites

- **NVIDIA GPU** with CUDA support
- **CUDA Toolkit** >= 11.8
- **TensorRT** >= 8.6
- **OpenCV** >= 4.8 (C++ and Python)
- **CMake** >= 3.18
- **Python** >= 3.10
- **Node.js** >= 18 (for the Angular dashboard)
- **Unreal Engine** 5.7 (for the 3D visualiser)
- **ffmpeg** (for frame capture)

---

## Quick Start

```bash
# 1. Create conda environment
conda create -n golf-sim python=3.10 -y
conda activate golf-sim

# 2. Install Python dependencies
pip install -r requirements.txt

# 3. Install Angular dashboard dependencies
cd contour && npm install && cd ..
```

---

## Project Structure

```
golf-sim/
├── README.md
├── requirements.txt
├── configs/
│   └── golf_ball_dataset.yaml         # YOLO dataset config
├── scripts/
│   ├── capture_frames.sh              # Frame capture from camera
│   ├── clear_training_data.sh         # Remove all training data
│   └── convert_onnx_to_trt.sh        # ONNX → TensorRT conversion
├── python/
│   ├── detect_golf_ball.py            # Train, detect, and export YOLOv10
│   ├── auto_label.py                  # Auto-label with GroundingDINO
│   ├── import_to_label_studio.py      # Label Studio import/export
│   └── split_dataset.py              # Train/val dataset splitter
├── cpp/                               # C++ real-time pipeline
│   ├── CMakeLists.txt
│   ├── include/
│   │   ├── trt_engine.h              # TensorRT engine wrapper
│   │   ├── frame_pipeline.h          # OpenCV frame processing
│   │   ├── tracker.h                 # EMA object tracker (multi-ball, multi-putter)
│   │   ├── putt_stats.h              # Per-putt state machine & stats
│   │   ├── stats_api.h               # REST API server (httplib)
│   │   └── unreal_sender.h           # UDP sender for Unreal Engine
│   └── src/
│       ├── main.cpp                  # Entry point & main loop
│       ├── trt_engine.cpp
│       ├── frame_pipeline.cpp
│       ├── tracker.cpp
│       ├── putt_stats.cpp
│       ├── stats_api.cpp
│       └── unreal_sender.cpp
├── contour/                           # Angular 21 dashboard
│   ├── package.json
│   ├── server.js                     # Express proxy (serves Angular + proxies API)
│   └── src/app/
│       ├── components/
│       │   ├── green-view/           # Top-down green overlay (SVG)
│       │   ├── games/                # Games / mini-game view
│       │   └── layout/               # App layout shell
│       └── services/
│           └── tracking.service.ts   # REST polling, ball claiming, hole selection
├── ue/GolfSimUE/                      # Unreal Engine 5.7 project
│   ├── GolfSimUE.uproject
│   └── Source/GolfSimUE/
│       ├── UDPGolfReceiver.h/cpp     # UDP listener, JSON parsing, pixel→world mapping
│       ├── BallToHoleLineActor.h/cpp # Spline-mesh aim line (ball → hole)
│       ├── BallTrailSplineActor.h/cpp# Ball trail visualisation
│       └── PuttMarkerActor.h/cpp     # Ball placement marker
├── data/
│   ├── images/{train,val}/           # Image data
│   └── labels/{train,val}/           # YOLO label files
└── models/                            # ONNX and TensorRT engines
```

---

## Components

### C++ Pipeline (`cpp/`)

The core of the system. Runs TensorRT inference on camera frames, tracks
objects, computes putt statistics, and fans out data via REST and UDP.

#### Key classes

| Class | File | Purpose |
|-------|------|---------|
| `TrtEngine` | `trt_engine.h/cpp` | Loads a TensorRT `.engine` file and runs inference |
| `FramePipeline` | `frame_pipeline.h/cpp` | Camera capture, pre-processing, NMS |
| `Tracker` | `tracker.h/cpp` | Multi-ball / multi-putter EMA tracker with stable IDs, putter-occlusion protection, and made-putt detection |
| `PuttStats` | `putt_stats.h/cpp` | Per-putt state machine (idle → in_motion → stopped) with speed, distance, break |
| `StatsApi` | `stats_api.h/cpp` | REST API on port 8080 — serves tracking data and accepts ball claims / hole selections |
| `UnrealSender` | `unreal_sender.h/cpp` | Sends JSON over UDP to Unreal Engine every frame |

#### Build & run

```bash
cd cpp
mkdir build && cd build

# Linux
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)

# Windows (Visual Studio)
cmake .. -G "Visual Studio 17 2022" -A x64 -DCMAKE_BUILD_TYPE=Release \
  -DCUDA_TOOLKIT_ROOT_DIR="C:/Program Files/NVIDIA GPU Computing Toolkit/CUDA/v13.1" \
  -DOpenCV_DIR="C:/path/to/opencv/build/x64/vc16/lib"
```

```bash
# Run with webcam
./golf_sim --engine path/to/model.engine --source 0

# Run with video, custom UE endpoint, headless
./golf_sim --engine path/to/model.engine \
           --source video.mp4 \
           --host 192.168.1.100 --port 7001 --no-gui
```

| Flag | Default | Description |
|------|---------|-------------|
| `--engine PATH` | *required* | TensorRT `.engine` file |
| `--source SRC` | `0` | Camera index or video file |
| `--host HOST` | `127.0.0.1` | Unreal Engine UDP host |
| `--port PORT` | `7001` | Unreal Engine UDP port |
| `--conf THRESH` | `0.5` | Detection confidence threshold |
| `--no-gui` | off | Disable OpenCV preview window |

#### REST API endpoints

| Method | Path | Description |
|--------|------|-------------|
| `GET` | `/api/tracking` | Full tracking snapshot (balls, holes, putters, users, frame dimensions) |
| `GET` | `/api/stats/current` | Current putt data (speed, distance, state) |
| `GET` | `/api/stats/history` | All completed putts |
| `GET` | `/api/stats/session` | Session averages |
| `POST` | `/api/claim-ball` | Claim a ball by stable index (`{ ball_index, username }`) |
| `POST` | `/api/target-hole` | Set target hole for a user (`{ index, username }`) |

---

### Angular Dashboard (`contour/`)

An Angular 21 single-page app that renders a real-time SVG overlay of the
putting green. Served by an Express server that proxies REST requests to the
C++ backend.

#### Features

- **Top-down green view** — balls, holes, and putters drawn over the camera frame
- **Ball claiming** — tap an unclaimed ball → pick a username
- **Hole selection** — putter hover over ball to select it, then hover over a hole
- **Ball-vanish selection** — if a putter occludes a ball, it auto-selects
- **Aim lines** — per-user coloured lines from ball to target hole
- **Putt stats** — live speed, distance, and break for each ball
- **Placement markers** — shows where to place a ball after a made putt
- **Multi-putter support** — any number of simultaneous putters

#### Run

```bash
cd contour
npm run build     # one-time Angular build
npm start         # Express server on port 4200 (proxies API to localhost:8080)
```

Open `http://localhost:4200` in a browser.

---

### Unreal Engine Visualiser (`ue/GolfSimUE/`)

Receives UDP JSON datagrams from the C++ pipeline and drives actors in a 3D
putting green scene. See [`ue/GolfSimUE/README.md`](ue/GolfSimUE/README.md)
for full setup instructions.

#### Key actors

| Actor | Purpose |
|-------|---------|
| `AUDPGolfReceiver` | Listens on UDP port 7001, parses JSON, maps pixel → world coordinates, drives ball/putter/hole actors |
| `ABallToHoleLineActor` | Spline mesh from ball to target hole that follows the green surface |
| `ABallTrailSplineActor` | Visualises the ball's path during a putt |
| `APuttMarkerActor` | Ball placement indicator |
| `BP_PlacementMarkerManager` | Blueprint actor that manages a pool of placement markers |

#### UDP payload (sent every frame)

```json
{
  "timestamp_ms": 1708099200000,
  "ball": { "x": 320.5, "y": 240.1, "vx": 15.2, "vy": -8.7, "conf": 0.95, "visible": true },
  "putter": { "x": 310.0, "y": 260.3, "conf": 0.88, "visible": true },
  "balls": [
    { "x": 320.5, "y": 240.1, "visible": true, "index": 0, "username": "alice",
      "target_hole_index": 1, "putt_number": 3, "speed": 12.5, "distance": 45.2, "state": "idle" }
  ],
  "holes": [ { "x": 500.0, "y": 400.0, "radius": 18.5, "visible": true } ],
  "putters": [ { "x": 310.0, "y": 260.3, "visible": true } ],
  "putt_made": false,
  "frame_width": 1920,
  "frame_height": 1080
}
```

---

## Training Pipeline

The model training workflow — from raw camera footage to a TensorRT engine:

| Step | Command | Description |
|------|---------|-------------|
| 1 | `scripts/capture_frames.sh` | Capture frames from camera via ffmpeg |
| 2 | `python/auto_label.py` | Auto-label images with GroundingDINO |
| 3 | `python/import_to_label_studio.py` | Review / correct labels in Label Studio |
| 4 | `python/split_dataset.py` | Split into train/val sets |
| 5 | `python/detect_golf_ball.py train` | Train YOLOv10 model |
| 6 | `python/detect_golf_ball.py export` | Export to ONNX |
| 7 | `scripts/convert_onnx_to_trt.sh` | Convert ONNX → TensorRT engine |

### Capture frames

```bash
./scripts/capture_frames.sh \
    --source /dev/video0 \
    --fps 120 --resolution 1920x1080 \
    --every 3 --duration 15
```

### Auto-label

```bash
python python/auto_label.py \
    --images data/images/train \
    --labels data/labels/train
```

### Train

```bash
python python/detect_golf_ball.py train \
    --data configs/golf_ball_dataset.yaml \
    --weights yolov10n.pt \
    --epochs 100 --batch 8
```

### Export & convert

```bash
python python/detect_golf_ball.py export \
    --weights runs/train/golf_ball_detector/weights/best.pt

./scripts/convert_onnx_to_trt.sh models/best.onnx models/golf.engine --fp16
```

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
| 2 | hole |

---

## Object Detection Classes

| ID | Class | Notes |
|----|-------|-------|
| 0 | Golf ball | Up to 4 tracked simultaneously with stable IDs |
| 1 | Putter | Multiple putters supported; best-confidence used for legacy/UE single-putter |
| 2 | Hole | Up to 8 holes; persisted for 150 frames when not visible |

---

## Interaction Model

The putter-hover interaction flow for the Angular dashboard:

1. **Claim a ball** — tap an unclaimed ball in the UI, pick a username
2. **Select a ball** — hover any putter over a claimed ball for 4+ frames (yellow ring appears)
3. **Select a target hole** — with a ball selected, hover the putter near a hole (blue preview ring), then lift the putter (vanish triggers selection)
4. **Aim line appears** — a coloured line draws from the ball to the selected hole
5. **Re-select** — hover over another hole and lift to change the target
6. **Ball-vanish shortcut** — if the putter covers a ball and it disappears from camera view, the ball is auto-selected (putter occlusion protection keeps the ball valid in the tracker)

---

## Tracker Behaviour

- **EMA smoothing** with configurable alpha (default 0.6) for position and velocity
- **Putter occlusion protection** — when a ball is not detected but a putter is within 60px of its last position, the ball stays valid (prevents aim line flicker / UE actor freeze)
- **Stable IDs** — balls get persistent IDs that survive brief detection loss; IDs are reserved for claimed balls so they don't get recycled
- **Made-putt detection** — when the primary ball disappears within 6" of the hole at reasonable speed, a made putt is declared
- **Hole persistence** — holes remain valid for up to 150 frames after losing detection
