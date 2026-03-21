import { Component, inject, OnInit, OnDestroy, signal, computed } from '@angular/core';
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

  readonly usernames = this.tracking.usernames;

  /** When >= 0, show username picker for hole selection */
  pickerHoleIndex = signal<number>(-1);

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
        b.visible &&
        b.username?.trim() &&
        typeof b.target_hole_index === 'number' &&
        b.target_hole_index >= 0 &&
        b.target_hole_index < holes.length,
    );
  });

  ngOnInit(): void {
    this.tracking.startPolling(100);
  }

  ngOnDestroy(): void {
    this.tracking.stopPolling();
  }

  holeRadius(r: number): number {
    return Math.max(r, 22);
  }

  onHoleClick(index: number): void {
    this.pickerHoleIndex.set(index);
  }

  async onBallClick(ballIndex: number): Promise<void> {
    const username = window.prompt('Enter your username to claim this ball:');
    if (username == null || !username.trim()) return;
    const ok = await this.tracking.claimBall(ballIndex, username.trim());
    if (!ok) window.alert('Failed to claim ball. Please try again.');
  }

  cancelPicker(): void {
    this.pickerHoleIndex.set(-1);
  }

  async onUsernameSelected(name: string): Promise<void> {
    const holeIdx = this.pickerHoleIndex();
    this.pickerHoleIndex.set(-1);
    if (holeIdx >= 0 && name?.trim()) {
      const ok = await this.tracking.selectTargetHole(holeIdx, name.trim());
      if (!ok) window.alert('Failed to select hole. Please try again.');
    }
  }

  async onEnterOtherUsername(): Promise<void> {
    const name = window.prompt('Enter your username:');
    if (name != null && name.trim()) {
      await this.onUsernameSelected(name.trim());
      this.pickerHoleIndex.set(-1);
    }
  }
}
