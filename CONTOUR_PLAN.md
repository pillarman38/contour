# RealSense Green Contour Detection to UE Landscape

Captures the physical green's 3D elevation contours using an Intel RealSense D435
depth camera and applies them to the Unreal Engine landscape.

## Architecture

```
RealSense D435 ──> capture_contour.py ──> heightmap.r16 + metadata.json
                                     └──> UDP (port 7002) ──> GolfSimReceiver ──> UE Landscape
```

**Two paths to apply contours:**

- **Path A (automatic):** Python sends the heightmap grid via UDP on port 7002.
  `GolfSimReceiver` receives it and programmatically modifies the landscape heights.
- **Path B (fallback):** Python saves a `.r16` raw heightmap file. Import it manually
  via UE Landscape Mode > Import.

Both paths start from the same Python capture script.

---

## 1. Python: `capture_contour.py`

New script at `python/capture_contour.py`:

- Connects to RealSense D435 via `pyrealsense2`
- Configures depth stream (848x480 or 1280x720 at 30fps)
- Applies RealSense post-processing filters: temporal, spatial, hole-filling
- Captures N frames (e.g., 30) and averages for noise reduction
- User defines green ROI interactively (click 4 corners on the color frame using OpenCV) or accepts full-frame
- Extracts depth within the ROI, perspective-corrects if needed
- Inverts depth values (closer to camera = higher elevation in UE)
- Normalizes to 0-65535 uint16 range (UE landscape heightmap format, where 32768 = "sea level")
- Resamples to a configurable grid resolution (default 127x127 -- matches common UE landscape sizes)
- Saves:
  - `contour/heightmap.r16` -- raw 16-bit heightmap for UE import
  - `contour/heightmap_preview.png` -- visual preview
  - `contour/metadata.json` -- grid dimensions, real-world scale, min/max depth
- Optionally sends the heightmap grid as a UDP datagram to UE (port 7002)

### Commands

```bash
cd python

# Interactive ROI selection -- click 4 corners of the green, then press Enter
python capture_contour.py --frames 30 --resolution 127

# Full-frame capture (skip ROI), save only (no UDP send)
python capture_contour.py --full-frame --no-udp

# Capture and send heightmap to UE via UDP
python capture_contour.py --frames 30 --resolution 127 --udp-host 127.0.0.1 --udp-port 7002
```

| Flag | Default | Description |
|------|---------|-------------|
| `--frames N` | `30` | Number of depth frames to average for noise reduction |
| `--resolution N` | `127` | Output grid size (NxN) -- must match UE landscape |
| `--full-frame` | off | Use entire depth frame instead of interactive ROI |
| `--no-udp` | off | Skip UDP send, only save files |
| `--udp-host HOST` | `127.0.0.1` | UE heightmap listener host |
| `--udp-port PORT` | `7002` | UE heightmap listener port |
| `--output DIR` | `../contour` | Output directory for heightmap files |

### Output Files

| File | Description |
|------|-------------|
| `contour/heightmap.r16` | Raw 16-bit heightmap for UE landscape import |
| `contour/heightmap_preview.png` | Grayscale preview of the heightmap |
| `contour/metadata.json` | Grid dimensions, real-world scale, min/max depth |

### UDP Heightmap Format (Port 7002)

```json
{
  "type": "heightmap",
  "width": 127,
  "height": 127,
  "min_depth_cm": 0.0,
  "max_depth_cm": 15.0,
  "data": [0.0, 0.12, 0.34, "...flattened row-major normalized heights..."]
}
```

`data` is a flattened row-major array of normalized heights (0.0 = lowest, 1.0 = highest point on green).

---

## 2. Python: Update Dependencies

Add `pyrealsense2` to `requirements.txt`.

---

## 3. UE: Extend `GolfSimReceiver` for Heightmap Reception

Changes to `GolfSimReceiver.h` and `GolfSimReceiver.cpp`:

- Add a second UDP socket on `ContourListenPort` (default 7002)
- Add `UPROPERTY` reference to `ALandscapeProxy* GreenLandscape`
- On receiving a `"type": "heightmap"` JSON payload:
  - Parse the grid dimensions and height data
  - Map normalized heights to UE landscape uint16 values (centered on 32768)
  - Scale height range via a new `ContourHeightScale` property (UU per unit of normalized height)
  - Use `FLandscapeEditDataInterface` to write height values to the landscape
  - Log the operation
- Add a `bAutoApplyContour` toggle (default false) to prevent accidental overwrites

**Key UE includes needed:** `Landscape.h`, `LandscapeEdit.h`, `LandscapeInfo.h`, `LandscapeProxy.h`

**Build.cs change:** Add `"Landscape"` to `PrivateDependencyModuleNames`.

### UE Setup Steps

1. In the `GolfSimReceiver` Details panel, assign your Landscape to **Green Landscape**
2. Set **Contour Listen Port** to `7002`
3. Set **Contour Height Scale** to control how much the depth variance maps to UE elevation (e.g., `50` = 50 UU of total relief)
4. Enable **Auto Apply Contour** to allow incoming heightmaps to modify the landscape
5. Run `capture_contour.py` -- the landscape will update automatically

### Manual Import (Path B)

If UDP application doesn't work or you prefer manual control:

1. Run `capture_contour.py --no-udp` to generate `contour/heightmap.r16`
2. In UE Editor, switch to **Landscape Mode**
3. Click **Import** and select the `.r16` file
4. Set the dimensions to match the resolution used (e.g., 127x127)

---

## 4. Files Changed/Created

| File | Action | Description |
|------|--------|-------------|
| `python/capture_contour.py` | **New** | Depth capture + heightmap generation |
| `requirements.txt` | **Edit** | Add `pyrealsense2` |
| `GolfSimReceiver.h` | **Edit** | Add contour socket, landscape ref, height scale properties |
| `GolfSimReceiver.cpp` | **Edit** | Add contour UDP listener, landscape height modification logic |
| `MyProject.Build.cs` | **Edit** | Add `"Landscape"` module dependency |
| `contour/` | **New** | Created at runtime for heightmap output |

---

## 5. Recommended Depth Cameras

| Camera | Price (approx) | Depth Accuracy | Range | Resolution | Best For | Notes |
|--------|----------------|----------------|-------|------------|----------|-------|
| **Intel RealSense D435** | ~$180 | ~2mm @ 1-2m | 0.1–10m | 1280x720 depth | Close-range overhead (6-8 ft) | Compact, great SDK (`pyrealsense2`), wide FOV (87°). Accuracy degrades past 3m. Good starting point. |
| **Intel RealSense D455** | ~$250 | ~2mm @ 4m | 0.6–20m | 1280x720 depth | Higher overhead mounts (8-12 ft) | Longer baseline = better long-range accuracy. IMU included. Best option if mounting higher than 8 ft. |
| **Intel RealSense L515** | ~$350 (discontinued, secondary market) | <1mm @ 1m | 0.25–9m | 1024x768 depth | Highest precision at close range | LiDAR-based, sub-millimeter accuracy. Discontinued but available used. Best raw precision for subtle contours. |
| **Azure Kinect DK** | ~$400 (discontinued, secondary market) | ~1mm narrow / ~3mm wide | 0.25–5.5m | 1024x1024 (narrow) | Indoor studio setups | Excellent precision, good SDK, but large and discontinued. Overkill for a single green. |
| **Orbbec Femto Bolt** | ~$300 | ~1mm @ 1m | 0.25–5.5m | 1024x1024 | Azure Kinect replacement | Same ToF sensor as Azure Kinect. Actively manufactured. Good alternative if D455 isn't precise enough. |
| **Orbbec Gemini 2** | ~$200 | ~2mm @ 1-3m | 0.15–10m | 1280x800 depth | Budget stereo option | Stereo-based like RealSense. OpenNI SDK. Decent alternative if RealSense is unavailable. |
| **Stereolabs ZED 2** | ~$450 | ~1mm @ 1m | 0.3–20m | 2208x1242 depth | Large greens, long range | Stereo camera, excellent at range. Higher res than RealSense. Needs NVIDIA GPU for depth processing. |

### Recommendations by Use Case

**Best value (current plan):** Intel RealSense D435 (~$180). Good enough for a 5x12 ft green mounted at 6-8 ft overhead. ~2mm accuracy captures most putting contours.

**Best accuracy under $300:** Intel RealSense D455 (~$250). Better long-range accuracy than D435. Recommended if your ceiling/mount is higher than 8 ft or the green is larger than 5x12 ft.

**Best precision (subtle breaks):** Orbbec Femto Bolt (~$300). Sub-millimeter accuracy. Worth it if you need to capture very subtle slope changes (1-2% grade) that the D435 might miss.

**Future-proof for larger greens:** Stereolabs ZED 2 (~$450). Best range and resolution for covering a full 20x20 ft green from a single overhead position. Requires NVIDIA GPU.

### Important Notes

- All cameras above are supported via Python SDKs. The `capture_contour.py` script is initially built for RealSense (`pyrealsense2`) but can be adapted.
- For a 5x12 ft green with typical putting contours (2-4% slope), the D435 at 6-8 ft overhead should capture ~2-5mm of elevation detail -- enough for most undulations.
- Mount the depth camera as close to directly overhead as possible to minimize occlusion and perspective distortion.

---

## 6. Implementation Order

1. Create `python/capture_contour.py`
2. Add `pyrealsense2` to `requirements.txt`
3. Add `"Landscape"` module to `MyProject.Build.cs`
4. Extend `GolfSimReceiver.h/.cpp` with contour socket + landscape modification
5. Test end-to-end with RealSense D435
