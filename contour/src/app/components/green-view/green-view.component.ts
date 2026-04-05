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

  /** Ball index selected for hole aiming (putter hover or tap). */
  selectedBallIndex = signal<number>(-1);

  /** Putter must be this close (px) to ball center to count as "hovering" for selection. */
  private static readonly PUTTER_BALL_RADIUS = 40;
  /** Frames (~100ms each) putter must stay within radius before auto-select (reduces stroke false positives). */
  private static readonly HOVER_FRAMES = 8;

  private putterBallHoverFrames = 0;
  private putterBallHoverIndex = -1;
  /** Last frame: balls that had a putter within PUTTER_BALL_RADIUS (for occlusion auto-select). */
  private prevBallsNearPutter: { index: number; x: number; y: number }[] = [];

  /** Max distance from vanished putter to preview hole for selection to trigger */
  private static readonly VANISH_HOLE_RADIUS = 200;
  /**
   * If a ghost track is this close to any visible ball, hide "Place ball here" — detector
   * often emits a new visible track while the old ghost remains (same physical ball).
   */
  private static readonly PLACE_MARKER_SUPPRESS_NEAR_VISIBLE_PX = 140;

  /** Last frame's putter positions (for detecting vanish events) */
  private prevPutterPositions: {x: number; y: number}[] = [];
  private holeSelectCooldownUntil = 0;
  private currentPreviewHole = -1;

  /** Distinct colors per ball label for aim lines. First color is not #ff6b6b (putter crosshair). */
  private static readonly USER_COLORS = [
    '#6bcbff', '#ffd93d', '#a8e063', '#ff6b9d', '#ff9f43', '#54a0ff',
    '#00d2d3', '#a55eea', '#ffc107', '#ff7f50', '#00fa9a', '#ff6b6b',
  ];

  getUserColor(key: string): string {
    let h = 0;
    for (let i = 0; i < key.length; i++) {
      h = (h << 5) - h + key.charCodeAt(i);
      h |= 0;
    }
    return GreenViewComponent.USER_COLORS[Math.abs(h) % GreenViewComponent.USER_COLORS.length];
  }

  getBallColor(ball: {username?: string; index: number}): string {
    return this.getUserColor(ball.username?.trim() || `Ball ${ball.index}`);
  }

  /** Balls with a valid target hole for drawing aim lines (only when the ball is actually on-screen) */
  readonly ballsWithTarget = computed(() => {
    const balls = this.balls();
    const holes = this.holes();
    return balls.filter(
      (b) =>
        b.visible &&
        typeof b.target_hole_index === 'number' &&
        b.target_hole_index >= 0 &&
        b.target_hole_index < holes.length,
    );
  });

  /** Ghost tracks that should still show the reclaim marker (not redundant with a visible ball nearby). */
  readonly placeMarkerBallIndices = computed(() => {
    const balls = this.balls();
    const visible = balls.filter((b) => b.visible);
    const r2 = GreenViewComponent.PLACE_MARKER_SUPPRESS_NEAR_VISIBLE_PX ** 2;
    const set = new Set<number>();
    for (const b of balls) {
      if (b.visible || b.username?.trim()) continue;
      let suppressed = false;
      for (const v of visible) {
        const dx = v.x - b.x;
        const dy = v.y - b.y;
        if (dx * dx + dy * dy < r2) {
          suppressed = true;
          break;
        }
      }
      if (!suppressed) set.add(b.index);
    }
    return set;
  });

  /** Display label for the currently selected ball ("alice" or "Ball 2") */
  readonly selectedBallLabel = computed(() => {
    const idx = this.selectedBallIndex();
    if (idx < 0) return null;
    const ball = this.balls().find(b => b.index === idx);
    if (!ball) return null;
    return ball.username?.trim() || `Ball ${idx}`;
  });

  /** Live preview: which hole will be selected if putter disappears now (-1 if none) */
  readonly previewHoleIndex = signal<number>(-1);

  constructor() {
    effect(() => {
      this.onTrackingUpdate();
    });
    effect(() => {
      const idx = this.selectedBallIndex();
      void this.tracking.setHoleAimBallIndex(idx);
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

  /** Tap ball body / marker to toggle selection for hole aiming. */
  onBallSelectForHole(ballIndex: number, event?: Event): void {
    event?.stopPropagation();
    this.selectedBallIndex.update((s) => (s === ballIndex ? -1 : ballIndex));
  }

  /** Tap "Claim this ball" only — avoids accidental selection while holding the putter near the ball. */
  onOpenClaimPicker(ballIndex: number, event: Event): void {
    event.stopPropagation();
    this.pickerBallIndex.set(ballIndex);
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
    const selected = this.selectedBallIndex();

    // Detect vanished putters: only when the putter count actually decreased.
    if (selected >= 0 && pts.length < this.prevPutterPositions.length && Date.now() > this.holeSelectCooldownUntil && this.currentPreviewHole >= 0) {
      const matchDist2 = 100 * 100;
      for (const prev of this.prevPutterPositions) {
        let matched = false;
        for (const cur of pts) {
          const dx = cur.x - prev.x;
          const dy = cur.y - prev.y;
          if (dx * dx + dy * dy < matchDist2) { matched = true; break; }
        }
        if (!matched) {
          const previewHole = d.holes[this.currentPreviewHole];
          if (previewHole) {
            const dx = prev.x - previewHole.x;
            const dy = prev.y - previewHole.y;
            const d2 = dx * dx + dy * dy;
            if (d2 < GreenViewComponent.VANISH_HOLE_RADIUS ** 2) {
              const holeIdx = this.currentPreviewHole;
              this.holeSelectCooldownUntil = Date.now() + 3000;
              this.selectedBallIndex.set(-1);
              this.currentPreviewHole = -1;
              this.previewHoleIndex.set(-1);
              const selBall = d.balls.find(b => b.index === selected);
              this.tracking.selectTargetHoleForBall(holeIdx, selected, selBall?.username?.trim());
              break;
            }
          }
        }
      }
    }

    // Save current putter positions for next frame's vanish detection
    this.prevPutterPositions = pts.map(p => ({x: p.x, y: p.y}));

    // Re-read selected (may have been cleared by vanish handler above)
    const sel = this.selectedBallIndex();

    // Update preview ring: find the hole nearest to ANY visible putter (with hysteresis)
    if (sel >= 0 && anyVisible) {
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
      return;
    }

    const r2 = GreenViewComponent.PUTTER_BALL_RADIUS ** 2;

    // Occlusion: ball was next to putter last frame and is now invisible — select that ball.
    if (sel < 0) {
      for (const prev of this.prevBallsNearPutter) {
        const ball = d.balls.find((b) => b.index === prev.index);
        if (!ball || ball.visible) continue;
        let putterNearby = false;
        for (const p of pts) {
          const dx = p.x - prev.x;
          const dy = p.y - prev.y;
          if (dx * dx + dy * dy < r2) {
            putterNearby = true;
            break;
          }
        }
        if (putterNearby) {
          this.selectedBallIndex.set(prev.index);
          this.putterBallHoverFrames = 0;
          this.putterBallHoverIndex = -1;
          break;
        }
      }
    }

    const selAfterVanish = this.selectedBallIndex();
    let nearestBallIdx = -1;
    let nearestBallD2 = r2;
    const nextBallsNearPutter: { index: number; x: number; y: number }[] = [];

    for (const b of d.balls) {
      if (!b.visible) continue;
      let nearPutter = false;
      for (const p of pts) {
        const dx = p.x - b.x;
        const dy = p.y - b.y;
        const d2 = dx * dx + dy * dy;
        if (d2 < r2) {
          nearPutter = true;
          if (selAfterVanish < 0 || b.index !== selAfterVanish) {
            if (d2 < nearestBallD2) {
              nearestBallD2 = d2;
              nearestBallIdx = b.index;
            }
          }
        }
      }
      if (nearPutter) {
        nextBallsNearPutter.push({ index: b.index, x: b.x, y: b.y });
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
        this.selectedBallIndex.set(nearestBallIdx);
        this.putterBallHoverFrames = 0;
      }
    } else {
      this.putterBallHoverFrames = 0;
      this.putterBallHoverIndex = -1;
    }
  }
}
