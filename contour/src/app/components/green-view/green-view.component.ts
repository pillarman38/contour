import { Component, inject, OnInit, OnDestroy, signal, computed, effect } from '@angular/core';
import { TrackingService } from '../../services/tracking.service';

@Component({
  selector: 'app-green-view',
  standalone: true,
  templateUrl: './green-view.component.html',
  styleUrl: './green-view.component.css',
})
export class GreenViewComponent implements OnInit, OnDestroy {
  private readonly tracking = inject(TrackingService);

  readonly data = this.tracking.tracking;
  readonly targetIndex = this.tracking.targetIndex;
  readonly username = this.tracking.username;

  /** Index of hole to highlight; prefers targetIndex when user is identified (synced from server) */
  readonly selectedHoleIndex = computed(() => {
    const d = this.data();
    const holes = d?.holes ?? [];
    const targetIdx = this.targetIndex();
    const hasUser = !!this.username()?.trim();
    if (holes.length === 0) return 0;
    if (hasUser && targetIdx >= 0 && targetIdx < holes.length) {
      return targetIdx;
    }
    const tx = d?.targetHoleX;
    const ty = d?.targetHoleY;
    if (typeof tx === 'number' && typeof ty === 'number') {
      let bestIdx = 0;
      let bestD2 = Infinity;
      for (let i = 0; i < holes.length; i++) {
        const dx = holes[i].x - tx;
        const dy = holes[i].y - ty;
        const d2 = dx * dx + dy * dy;
        if (d2 < bestD2) {
          bestD2 = d2;
          bestIdx = i;
        }
      }
      return bestIdx;
    }
    return Math.max(0, Math.min(targetIdx, holes.length - 1));
  });

  readonly ballPos = computed(() => {
    const d = this.data();
    if (!d?.ball?.visible) return null;
    return { x: d.ball.x, y: d.ball.y };
  });

  readonly balls = computed(() => this.data()?.balls ?? []);

  readonly holes = computed(() => this.data()?.holes ?? []);

  readonly putter = computed(() => this.data()?.putter ?? null);

  readonly putters = computed(() => this.data()?.putters ?? []);

  /** Users who should return their ball to the marked spot (camera pixels). */
  readonly usersAwaitingBallPlacement = computed(() => {
    const users = this.data()?.users ?? [];
    return users.filter(
      (u) =>
        u.placement_waiting &&
        u.placement_hint_valid &&
        typeof u.placement_pixel_x === 'number' &&
        typeof u.placement_pixel_y === 'number' &&
        u.username?.trim(),
    );
  });

  readonly usernames = this.tracking.usernames;

  /** When >= 0, show username picker for ball claim */
  pickerBallIndex = signal<number>(-1);

  /** Username of ball selected via putter hover */
  selectedBallUsername = signal<string | null>(null);

  /** Pixel radius for putter-to-ball proximity (camera pixels) */
  private static readonly PUTTER_BALL_RADIUS = 60;
  /** Max distance from vanished putter to preview hole for selection to trigger */
  private static readonly VANISH_HOLE_RADIUS = 200;
  /** Consecutive frames a putter must hover over a ball before selecting it */
  private static readonly HOVER_FRAMES = 4;

  private putterBallHoverFrames = 0;
  private putterBallHoverIndex = -1;
  /** Last frame's putter positions (for detecting vanish events) */
  private prevPutterPositions: {x: number; y: number}[] = [];
  /** Claimed balls that were near a putter last frame (for occlusion-based selection) */
  private prevBallsNearPutter: {index: number; username: string; x: number; y: number}[] = [];
  private holeSelectCooldownUntil = 0;
  private currentPreviewHole = -1;

  /** Distinct colors for user aim lines (per-username) */
  private static readonly USER_COLORS = [
    '#ff6b6b', '#ffd93d', '#6bcbff', '#a8e063', '#ff6b9d', '#ff9f43',
    '#54a0ff', '#00d2d3', '#a55eea', '#ffc107', '#ff7f50', '#00fa9a',
  ];

  getUserColor(username: string): string {
    let h = 0;
    for (let i = 0; i < username.length; i++) {
      h = (h << 5) - h + username.charCodeAt(i);
      h |= 0;
    }
    return GreenViewComponent.USER_COLORS[Math.abs(h) % GreenViewComponent.USER_COLORS.length];
  }

  /** Balls with username and valid target hole for drawing aim lines */
  readonly ballsWithTarget = computed(() => {
    const balls = this.balls();
    const holes = this.holes();
    return balls.filter(
      (b) =>
        (b.visible || !!b.username?.trim()) &&
        b.username?.trim() &&
        typeof b.target_hole_index === 'number' &&
        b.target_hole_index >= 0 &&
        b.target_hole_index < holes.length,
    );
  });

  /** Live preview: which hole will be selected if putter disappears now (-1 if none) */
  readonly previewHoleIndex = signal<number>(-1);

  constructor() {
    effect(() => {
      this.onTrackingUpdate();
    });
  }

  ngOnInit(): void {
    this.tracking.startPolling(100);
  }

  ngOnDestroy(): void {
    this.tracking.stopPolling();
  }

  holeRadius(r: number): number {
    return Math.max(r, 22);
  }

  onBallClick(ballIndex: number): void {
    const ball = this.balls().find((b) => b.index === ballIndex);
    if (!ball?.username?.trim()) {
      this.pickerBallIndex.set(ballIndex);
    }
  }

  cancelPicker(): void {
    this.pickerBallIndex.set(-1);
  }

  async onUsernameSelected(name: string): Promise<void> {
    const ballIdx = this.pickerBallIndex();
    this.pickerBallIndex.set(-1);
    if (ballIdx >= 0 && name?.trim()) {
      const ok = await this.tracking.claimBall(ballIdx, name.trim());
      if (!ok) window.alert('Failed to claim ball. Please try again.');
    }
  }

  async onEnterOtherUsername(): Promise<void> {
    const name = window.prompt('Enter your username:');
    if (name != null && name.trim()) {
      await this.onUsernameSelected(name.trim());
    }
  }

  private findNearestHole(px: number, py: number, holes: {x:number;y:number}[]): {idx:number; d2:number} {
    let bestIdx = -1;
    let bestD2 = Infinity;
    for (let i = 0; i < holes.length; i++) {
      const h = holes[i];
      const dx = px - h.x;
      const dy = py - h.y;
      const d2 = dx * dx + dy * dy;
      if (d2 < bestD2) {
        bestD2 = d2;
        bestIdx = i;
      }
    }
    return {idx: bestIdx, d2: bestD2};
  }

  private onTrackingUpdate(): void {
    const d = this.data();
    if (!d) return;
    const pts = d.putters;
    const anyVisible = pts.length > 0;
    const selected = this.selectedBallUsername();

    // Detect vanished putters: only when the putter count actually decreased.
    // Without this guard, a putter moving >100px between frames triggers a false vanish.
    if (selected && pts.length < this.prevPutterPositions.length && Date.now() > this.holeSelectCooldownUntil && this.currentPreviewHole >= 0) {
      const matchDist2 = 100 * 100;
      for (const prev of this.prevPutterPositions) {
        let matched = false;
        for (const cur of pts) {
          const dx = cur.x - prev.x;
          const dy = cur.y - prev.y;
          if (dx * dx + dy * dy < matchDist2) { matched = true; break; }
        }
        if (!matched) {
          // This putter vanished — check if it was near the preview hole
          const previewHole = d.holes[this.currentPreviewHole];
          if (previewHole) {
            const dx = prev.x - previewHole.x;
            const dy = prev.y - previewHole.y;
            const d2 = dx * dx + dy * dy;
            if (d2 < GreenViewComponent.VANISH_HOLE_RADIUS ** 2) {
              const holeIdx = this.currentPreviewHole;
              this.holeSelectCooldownUntil = Date.now() + 3000;
              this.selectedBallUsername.set(null);
              this.currentPreviewHole = -1;
              this.previewHoleIndex.set(-1);
              this.putterBallHoverFrames = 0;
              this.putterBallHoverIndex = -1;
              this.tracking.selectTargetHole(holeIdx, selected);
              break;
            }
          }
        }
      }
    }

    // Save current putter positions for next frame's vanish detection
    this.prevPutterPositions = pts.map(p => ({x: p.x, y: p.y}));

    // Re-read selected (may have been cleared by vanish handler above)
    const sel = this.selectedBallUsername();

    // Update preview ring: find the hole nearest to ANY visible putter (with hysteresis)
    if (sel && anyVisible) {
      let bestHoleIdx = -1;
      let bestHoleD2 = Infinity;
      for (const p of pts) {
        const n = this.findNearestHole(p.x, p.y, d.holes);
        if (n.d2 < bestHoleD2) {
          bestHoleD2 = n.d2;
          bestHoleIdx = n.idx;
        }
      }
      if (this.currentPreviewHole < 0 || bestHoleIdx === this.currentPreviewHole) {
        this.currentPreviewHole = bestHoleIdx;
      } else {
        const curHole = d.holes[this.currentPreviewHole];
        let curBestD2 = Infinity;
        for (const p of pts) {
          const dx = p.x - curHole.x;
          const dy = p.y - curHole.y;
          const d2 = dx * dx + dy * dy;
          if (d2 < curBestD2) curBestD2 = d2;
        }
        if (bestHoleD2 < curBestD2 * 0.7) {
          this.currentPreviewHole = bestHoleIdx;
        }
      }
      this.previewHoleIndex.set(this.currentPreviewHole);
    } else if (!anyVisible) {
      this.currentPreviewHole = -1;
      this.previewHoleIndex.set(-1);
    }

    if (!anyVisible || Date.now() < this.holeSelectCooldownUntil) {
      this.putterBallHoverFrames = 0;
      this.putterBallHoverIndex = -1;
      this.prevBallsNearPutter = [];
      return;
    }

    // Ball-vanish selection: if a claimed ball was near a putter last frame and is
    // now not visible (occluded by putter), select it immediately. A putter must
    // still be near the ball's last position (filters out putts where the putter
    // swings through quickly).
    if (!sel) {
      const vanishR2 = GreenViewComponent.PUTTER_BALL_RADIUS ** 2;
      for (const prev of this.prevBallsNearPutter) {
        const ball = d.balls.find(b => b.index === prev.index);
        if (!ball || ball.visible) continue;
        let putterNearby = false;
        for (const p of pts) {
          const dx = p.x - prev.x;
          const dy = p.y - prev.y;
          if (dx * dx + dy * dy < vanishR2) { putterNearby = true; break; }
        }
        if (putterNearby) {
          this.selectedBallUsername.set(prev.username);
          this.putterBallHoverFrames = 0;
          this.putterBallHoverIndex = -1;
          break;
        }
      }
    }

    // Ball selection: check if ANY putter is near a claimed ball (closest pair wins)
    const selAfterVanish = this.selectedBallUsername();
    let nearestBallIdx = -1;
    let nearestBallD2 = GreenViewComponent.PUTTER_BALL_RADIUS ** 2;
    const nextBallsNearPutter: {index: number; username: string; x: number; y: number}[] = [];
    for (const b of d.balls) {
      if (!b.visible) continue;
      const name = b.username?.trim();
      if (!name) continue;
      let nearPutter = false;
      for (const p of pts) {
        const dx = p.x - b.x;
        const dy = p.y - b.y;
        const d2 = dx * dx + dy * dy;
        if (d2 < GreenViewComponent.PUTTER_BALL_RADIUS ** 2) {
          nearPutter = true;
          if (!selAfterVanish || name !== selAfterVanish) {
            if (d2 < nearestBallD2) {
              nearestBallD2 = d2;
              nearestBallIdx = b.index;
            }
          }
        }
      }
      if (nearPutter) {
        nextBallsNearPutter.push({index: b.index, username: name, x: b.x, y: b.y});
      }
    }
    this.prevBallsNearPutter = nextBallsNearPutter;

    if (nearestBallIdx >= 0) {
      if (this.putterBallHoverIndex === nearestBallIdx) {
        this.putterBallHoverFrames++;
      } else {
        this.putterBallHoverIndex = nearestBallIdx;
        this.putterBallHoverFrames = 1;
      }
      if (this.putterBallHoverFrames >= GreenViewComponent.HOVER_FRAMES) {
        const ball = d.balls.find((b) => b.index === nearestBallIdx);
        if (ball?.username?.trim()) {
          this.selectedBallUsername.set(ball.username.trim());
        }
        this.putterBallHoverFrames = 0;
      }
    } else {
      this.putterBallHoverFrames = 0;
      this.putterBallHoverIndex = -1;
    }
  }
}
