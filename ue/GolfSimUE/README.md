# GolfSimUE — Unreal Engine 5.7 Golf Putting Visualiser

Receives real-time ball and putter tracking data from the **golf-sim** C++ tracker
over UDP and drives actors in the Unreal scene.

## Quick Start

1. **Open the project** — double-click `GolfSimUE.uproject` and compile when prompted.
2. **Place actors** — drag a **UDP Golf Receiver** into the level, plus a static mesh for the ball (and optionally a putter).
3. **Configure the receiver** (Details panel):

  | Property                   | Default                   | Description                        |
  | -------------------------- | ------------------------- | ---------------------------------- |
  | **Listen Port**            | `7001`                    | Must match `--port` on the tracker |
  | **Listen IP**              | `0.0.0.0`                 | Bind address                       |
  | **Ball Actor**             | —                         | Pick the ball mesh in the level    |
  | **Putter Actor**           | —                         | Pick the putter mesh               |
  | **Pixel Bounds Max**       | `1920 × 1080`             | Camera resolution in the tracker   |
  | **World Bounds Min / Max** | `(0,0,0)` / `(300,200,0)` | World rectangle the green occupies |
  | **Interpolate**            | `true`                    | Smooth movement via `VInterpTo`    |
  | **Interpolation Speed**    | `15`                      | Higher = snappier tracking         |

4. **Run the tracker**, then press **Play** in UE:
  ```
   golf_sim.exe --engine model.engine --port 7001
  ```

## Coordinate Mapping

The tracker sends 2D pixel positions. The receiver maps them linearly:

```
WorldX = lerp(WorldBoundsMin.X, WorldBoundsMax.X, PixelX / PixelBoundsMax.X)
WorldY = lerp(WorldBoundsMin.Y, WorldBoundsMax.Y, PixelY / PixelBoundsMax.Y)
```

Adjust `WorldBoundsMin`, `WorldBoundsMax`, and `PixelBoundsMax` until the ball
tracks correctly over your green mesh.

## Blueprint Events

- **On Putt Made** — fires once on the rising edge of `putt_made`.
- **On UDP Data Received** — fires every time a valid datagram arrives; passes the full `FGolfUDPPayload` struct.

---

## Ball Placement Markers (Return Ball)

When a claimed ball is not visible the tracker sends a target position for that player so they know where to place their ball.

- **Made putt** (ball picked up after sinking): markers sit on a horizontal line through the green center. Multiple players waiting at once get spaced slots along that line (sorted by username).
- **Not a made putt** but ball lost (occlusion / pickup): marker uses the last-known ball position.

### What you need


| Item                            | Count      | Purpose                                                                  |
| ------------------------------- | ---------- | ------------------------------------------------------------------------ |
| `**BP_PlacementMarkerManager`** | 1 in level | Actor Blueprint you create; holds all the logic on its **Event Graph**   |
| `**PlaceBallMarker`** actors    | N in level | Visual-only actors (no Tick, no UDP logic); the manager moves/hides them |
| **UDP Golf Receiver**           | 1 in level | Already placed for ball tracking; the manager reads from it              |


### Variables on BP_PlacementMarkerManager

Add these two variables in the **My Blueprint** panel:


| Variable      | Type                                        | How to set                                                                 |
| ------------- | ------------------------------------------- | -------------------------------------------------------------------------- |
| `UDPReceiver` | Object ref → **UDP Golf Receiver**          | Assign in Details panel, or let Begin Play find it automatically           |
| `MarkerPool`  | Array of **Actor** (or **PlaceBallMarker**) | Assign your N marker instances in Details, in order (index 0 = first slot) |


### Step 1 — Event Begin Play (resolve receiver + bind)

All of this lives on the **Event Graph**. No Functions tab, no separate Blueprints.

```
Event BeginPlay
  → Is Valid (UDPReceiver)
      ├─ Is Valid     → (merge point)
      └─ Is Not Valid → Get UDP Golf Receiver In World (World Context = Self)
                        → SET UDPReceiver
                        → (merge point)
  → Assign On UDP Data Received (Target = UDPReceiver)
      Event = OnRefreshPlacementMarkers  ← custom event you create on this same graph
```

**Key details:**

- **Get UDP Golf Receiver In World** needs **World Context Object = Self** (this actor), *not* the empty `UDPReceiver` pin. If the node demands a wrong Target, use **Get Actor of Class** → UDP Golf Receiver instead, or just assign `UDPReceiver` in the level Details panel.
- **Assign On UDP Data Received** creates a custom event node automatically. Name it `OnRefreshPlacementMarkers` (or any name you like). Its body is Step 2 below.

### Step 2 — OnRefreshPlacementMarkers (move + show/hide markers)

Wire the custom event's exec output straight into the loop. Everything stays on the **Event Graph**.

```
CustoOut Markerm Event · OnRefreshPlacementMarkers
  → [data] GET UDPReceiver → Get Waiting Placement Markers World → Out Markers
  → For Each Loop (Array = Out Markers)
      ├─ Loop Body:
      │     Break Array Element → World Location
      │     GET MarkerPool[Array Index] → Set Actor Location (New Location = World Location)
      │                                 → Set Actor Hidden In Game (New Hidden = false)
      └─ Completed:
            For Loop (First = Out Markers Length, Last = MarkerPool.Num − 1)
              → GET MarkerPool[Loop Index] → Set Actor Hidden In Game (New Hidden = true)
```

**How to read this:**

1. **Get Waiting Placement Markers World** (Target = `UDPReceiver`) fills **Out Markers** — an array of `FGolfWaitingPlacementWorld` structs with `WorldLocation`, `Username`, `StableId`, and `bAfterPuttReturn`.
2. **For Each Loop** iterates the waiting markers. For each one, grab the corresponding **MarkerPool** actor by index, move it to the world location, and unhide it.
3. **Completed** fires after the loop. A second **For Loop** hides any leftover pool actors whose index is beyond the marker count.

**Important wiring rules:**

- Wire **Out Markers** into the For Each Loop's **Array** pin (the *input*). Do **not** wire it into **Array Element** — that pin is an *output* the loop gives you each iteration.
- To get a pool actor, drag from **MarkerPool** and use **Get (a ref)** with the **Array Index** (or **Loop Index**) pin. Wire the output actor into **Set Actor Location → Target**.

### Step 3 — Level setup

1. Create an Actor Blueprint named `BP_PlacementMarkerManager`.
2. Add the two variables (`UDPReceiver`, `MarkerPool`) and build the Event Graph above.
3. Place it once in the level.
4. Place N `PlaceBallMarker` actors in the level (start with 4–6).
5. Select the manager → Details → set **MarkerPool** array elements to your marker actors (in order).
6. Optionally set **UDPReceiver** to the receiver in the level (or leave it empty — Begin Play will find it).

### Optional: Event Tick fallback

If you also want markers to update every frame (not just on UDP packets):

```
Event Tick → Call OnRefreshPlacementMarkers
```

Set **Tick Group** to **Post Update Work**. Prefer UDP-only unless you have a reason to poll.

### Alternative: single For Loop (show + hide in one pass)

Instead of For Each + a second hide loop, you can use one **For Loop** over the entire pool:

```
For Loop (First = 0, Last = MarkerPool.Num − 1)
  → Branch (Loop Index < Out Markers Length)
      ├─ True:  GET Markers[Loop Index] → World Location
      │         GET MarkerPool[Loop Index] → Set Actor Location + Set Hidden = false
      └─ False: GET MarkerPool[Loop Index] → Set Hidden = true
```

### Common mistakes


| Issue                                                                 | Fix                                                                                                                                                 |
| --------------------------------------------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------- |
| "Self is not a UDPGolfReceiver" on **Get UDP Golf Receiver In World** | Wire **World Context Object = Self** (this manager actor), not `UDPReceiver`. Or use **Get Actor of Class**.                                        |
| Marker never hides                                                    | Make sure the **Completed** path (or **False** branch) sets **Hidden = true** on leftover pool actors.                                              |
| For Each running on every marker instance                             | Only the **manager** runs the loop. `PlaceBallMarker` actors are passive — no Tick, no UDP logic on them.                                           |
| **Out Markers** wired to **Array Element**                            | Wrong pin. **Array Element** is an output. Wire **Out Markers** → **Array** (input).                                                                |
| Marker appears at origin instead of world position                    | Make sure you **Break** the struct from **Array Element** to get **World Location**, and wire that into **New Location** on **Set Actor Location**. |
| Pool shorter than marker count                                        | Add a **Branch** (`Array Index < MarkerPool.Num`) before Set Actor Location, or just add more actors to the pool.                                   |


### Data reference


| Node / Property                            | Returns                                                                                               | Use for                                                         |
| ------------------------------------------ | ----------------------------------------------------------------------------------------------------- | --------------------------------------------------------------- |
| **Get Waiting Placement Markers World**    | `TArray<FGolfWaitingPlacementWorld>` with `WorldLocation`, `Username`, `StableId`, `bAfterPuttReturn` | Driving the marker pool (recommended)                           |
| **Get Placement World For Stable Id**      | single world pos + bool                                                                               | Per-player marker (if you prefer one marker per known StableId) |
| **Ball Placements Data**                   | `TArray<FGolfBallPlacement>` with pixel coords, `bWaitingPlacement`, `bAfterPuttReturn`               | Raw pixel data before world mapping                             |
| **Get Last Payload** → **Ball Placements** | Full `FGolfUDPPayload`                                                                                | Access to everything in one struct                              |
| **Get Seconds Since Last UDP**             | float (seconds)                                                                                       | Fade markers when tracker is disconnected                       |


### Visual reference

Open `[BP_PlacementMarkerManager_Graph.html](BP_PlacementMarkerManager_Graph.html)` in a browser for a UE-style node layout of the entire Event Graph described above.

---

## StandLine (Where to Stand Indicator)

The StandLine mesh in `Ball_Blueprint` rotates to point from the hole toward the ball,
showing players where to stand. Implement this in Blueprint:

1. Open **Ball_Blueprint** and go to the Event Graph.
2. Add **Event Tick**.
3. Add a reference to **UDPGolfReceiver** (promote to variable if needed).

### Logic (each tick)

- **Hide when ball not visible**: if `GolfReceiver.BallData.bVisible` is false → `StandLine.SetVisibility(false)` and return.
- **Optional — only when idle**: if `GolfReceiver.PuttStats.State` is not "idle" or "stopped" → return.
- **Get ball world location**:
  - If `BallsData.Num() > 0` and `BallsData[0].Tracked.bVisible`: use `PixelToWorld(BallsData[0].Tracked.X, BallsData[0].Tracked.Y)`.
  - Else if `BallData.bVisible`: use `PixelToWorld(BallData.X, BallData.Y)`.
  - Else: hide StandLine and return.
- **Get hole location**: `Get Actor Location` (this actor = hole).
- **Compute direction**: `(BallWorld - HoleLocation).Normalize()`.
- **Set rotation**: `StandLine.Set World Rotation` using `Make Rot from X` (direction) with Pitch/Roll = 0, or use `Find Look at Rotation`.
- **Show**: `StandLine.SetVisibility(true)`.

