# GolfSimUE — Unreal Engine 5.7 Golf Putting Visualiser

Receives real-time ball and putter tracking data from the **golf-sim** C++ tracker
over UDP and drives actors in the Unreal scene.

## Quick Start

1. **Open the project**
   Double-click `GolfSimUE.uproject`. UE will prompt to compile modules — click **Yes**.
   (Or right-click the `.uproject` → *Generate Visual Studio project files*, then build from VS.)

2. **Set up the scene**
   - Place a **Static Mesh Actor** for the ball (name it however you like).
   - Optionally place another for the putter.
   - Drag an **AUDPGolfReceiver** into the level (search "UDP Golf Receiver" in the actor list).

3. **Configure the receiver** (Details panel)

   | Property | Default | Description |
   |---|---|---|
   | **Listen Port** | `7001` | Must match `--port` in the golf-sim tracker |
   | **Listen IP** | `0.0.0.0` | Bind address |
   | **Ball Actor** | — | Pick the ball static mesh actor |
   | **Putter Actor** | — | Pick the putter static mesh actor |
   | **Pixel Bounds Max** | `1920 × 1080` | Camera resolution in the tracker |
   | **World Bounds Min / Max** | `(0,0,0)` / `(300,200,0)` | World rectangle the green occupies |
   | **Interpolate** | `true` | Smooth movement via `VInterpTo` |
   | **Interpolation Speed** | `15` | Higher = snappier tracking |

4. **Run the tracker**
   ```
   golf_sim.exe --engine model.engine --port 7001
   ```
   Then press **Play** in UE. The ball actor will follow the tracked ball position.

## Blueprint Events

- **On Putt Made** — fires once on the rising edge of `putt_made`.
- **On UDP Data Received** — fires every time a valid datagram arrives,
  passes the full `FGolfUDPPayload` struct so you can drive UI, particles, etc.

## Coordinate Mapping

The tracker sends 2D pixel positions. The receiver maps them linearly:

```
WorldX = lerp(WorldBoundsMin.X, WorldBoundsMax.X, PixelX / PixelBoundsMax.X)
WorldY = lerp(WorldBoundsMin.Y, WorldBoundsMax.Y, PixelY / PixelBoundsMax.Y)
```

Adjust `WorldBoundsMin`, `WorldBoundsMax`, and `PixelBoundsMax` until the ball
tracks correctly over your green mesh.
