import { Injectable, signal, computed } from '@angular/core';

export interface HoleInfo {
  x: number;
  y: number;
  radius: number;
  visible: boolean;
}

export interface BallInfo {
  x: number;
  y: number;
  visible: boolean;
  index: number;
  username?: string;
  target_hole_index?: number;
}

export interface UserInfo {
  username: string;
  ball_index: number;
  target_hole_index: number;
  /** Camera pixels: where to return the ball after a putt / pickup */
  placement_pixel_x?: number;
  placement_pixel_y?: number;
  placement_hint_valid?: boolean;
  /** True when ball not visible — show “place ball here” */
  placement_waiting?: boolean;
  /** Made putt: marker on green-center line; false: last-known (occlusion) */
  placement_after_putt?: boolean;
}

export interface PutterInfo {
  x: number;
  y: number;
  visible: boolean;
}

export interface TrackingData {
  ball: { x: number; y: number; visible: boolean };
  putter: PutterInfo;
  putters: PutterInfo[];
  balls: BallInfo[];
  holes: HoleInfo[];
  users: UserInfo[];
  frameWidth: number;
  frameHeight: number;
  targetHoleX?: number;
  targetHoleY?: number;
}

@Injectable({ providedIn: 'root' })
export class TrackingService {
  private readonly data = signal<TrackingData | null>(null);
  private readonly targetHoleIndex = signal<number>(0);
  private readonly sessionId = signal<string | null>(null);
  private readonly currentUsername = signal<string | null>(null);
  private pollInterval: ReturnType<typeof setInterval> | null = null;
  private lastPostTime = 0;
  private static readonly STORAGE_KEY = 'golf-sim-username';

  readonly tracking = this.data.asReadonly();
  readonly targetIndex = this.targetHoleIndex.asReadonly();
  readonly session = this.sessionId.asReadonly();
  readonly username = this.currentUsername.asReadonly();
  readonly hasData = computed(() => this.data() !== null);

  /** Available usernames to pick from: defaults + any in use on balls or in users */
  readonly usernames = computed(() => {
    const defaults = ['Player 1', 'Player 2', 'Player 3', 'Player 4'];
    const d = this.data();
    const fromBalls = new Set<string>();
    for (const b of d?.balls ?? []) {
      if (b.username?.trim()) fromBalls.add(b.username.trim());
    }
    for (const u of d?.users ?? []) {
      if (u.username?.trim()) fromBalls.add(u.username.trim());
    }
    const combined = [...defaults];
    for (const u of fromBalls) {
      if (!combined.includes(u)) combined.push(u);
    }
    return combined;
  });

  private get apiBase(): string {
    // Use relative URLs when served by Express (proxies to C++ backend)
    return '';
  }

  async ensureSession(): Promise<string> {
    let sid = this.sessionId();
    if (sid) return sid;
    try {
      const res = await fetch(`${this.apiBase}/api/session-id`);
      if (res.ok) {
        const json = await res.json();
        sid = json.session_id ?? '';
        if (sid) this.sessionId.set(sid);
      }
    } catch {
      // fallback: generate local id
      sid = 'local-' + Math.random().toString(36).slice(2, 14);
      this.sessionId.set(sid);
    }
    // Restore persisted username on new session (for refresh / new device)
    if (!this.currentUsername()?.trim()) {
      try {
        const stored = localStorage.getItem(TrackingService.STORAGE_KEY);
        if (stored?.trim()) this.currentUsername.set(stored.trim());
      } catch {
        // ignore
      }
    }
    return sid ?? 'unknown';
  }

  startPolling(intervalMs = 100): void {
    this.stopPolling();
    this.ensureSession();
    const fetchData = async () => {
      try {
        const res = await fetch(`${this.apiBase}/api/tracking`);
        if (res.ok) {
          const json = await res.json();
          const balls: BallInfo[] = (json.balls ?? []).map((b: Record<string, unknown>) => ({
            x: Number(b['x'] ?? 0),
            y: Number(b['y'] ?? 0),
            visible: Boolean(b['visible']),
            index: Number(b['index'] ?? 0),
            username: typeof b['username'] === 'string' ? b['username'] : undefined,
            target_hole_index: typeof b['target_hole_index'] === 'number' ? b['target_hole_index'] : undefined,
          }));
          const users: UserInfo[] = (json.users ?? []).map((u: Record<string, unknown>) => ({
            username: String(u['username'] ?? ''),
            ball_index: Number(u['ball_index'] ?? -1),
            target_hole_index: Number(u['target_hole_index'] ?? 0),
            placement_pixel_x: typeof u['placement_pixel_x'] === 'number' ? u['placement_pixel_x'] : undefined,
            placement_pixel_y: typeof u['placement_pixel_y'] === 'number' ? u['placement_pixel_y'] : undefined,
            placement_hint_valid: typeof u['placement_hint_valid'] === 'boolean' ? u['placement_hint_valid'] : undefined,
            placement_waiting: typeof u['placement_waiting'] === 'boolean' ? u['placement_waiting'] : undefined,
            placement_after_putt:
              typeof u['placement_after_putt'] === 'boolean' ? u['placement_after_putt'] : undefined,
          }));
          const putter: PutterInfo = json.putter
            ? { x: Number(json.putter.x ?? 0), y: Number(json.putter.y ?? 0), visible: Boolean(json.putter.visible) }
            : { x: 0, y: 0, visible: false };
          const putters: PutterInfo[] = Array.isArray(json.putters)
            ? json.putters.map((p: Record<string, unknown>) => ({
                x: Number(p['x'] ?? 0),
                y: Number(p['y'] ?? 0),
                visible: Boolean(p['visible']),
              }))
            : putter.visible ? [putter] : [];
          this.data.set({
            ball: json.ball ?? { x: 0, y: 0, visible: false },
            putter,
            putters,
            balls,
            holes: json.holes ?? [],
            users,
            frameWidth: json.frame_width ?? 1920,
            frameHeight: json.frame_height ?? 1080,
            targetHoleX: typeof json.target_hole_x === 'number' ? json.target_hole_x : undefined,
            targetHoleY: typeof json.target_hole_y === 'number' ? json.target_hole_y : undefined,
          });
          // Sync target hole from server when we have a username (cross-device)
          const uname = this.currentUsername();
          if (uname?.trim()) {
            const myUser = users.find((u) => u.username.trim() === uname.trim());
            const myBall = balls.find((b) => b.username?.trim() === uname.trim());
            const serverIdx = myUser?.target_hole_index ?? myBall?.target_hole_index;
            if (typeof serverIdx === 'number' && serverIdx >= 0) {
              this.targetHoleIndex.set(serverIdx);
            }
          } else if (typeof json.target_hole_index === 'number' && Date.now() - this.lastPostTime > 300) {
            this.targetHoleIndex.set(json.target_hole_index);
          }
        }
      } catch {
        this.data.set(null);
      }
    };
    fetchData();
    this.pollInterval = setInterval(fetchData, intervalMs);
  }

  stopPolling(): void {
    if (this.pollInterval) {
      clearInterval(this.pollInterval);
      this.pollInterval = null;
    }
  }

  async selectTargetHole(index: number, username: string): Promise<boolean> {
    const sid = await this.ensureSession();
    try {
      const res = await fetch(`${this.apiBase}/api/target-hole`, {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ session_id: sid, index, username }),
      });
      if (res.ok) {
        this.currentUsername.set(username);
        this.targetHoleIndex.set(index);
        this.lastPostTime = Date.now();
        try {
          localStorage.setItem(TrackingService.STORAGE_KEY, username);
        } catch {
          // ignore
        }
        return true;
      }
    } catch {
      // ignore
    }
    return false;
  }

  async claimBall(ballIndex: number, username: string): Promise<boolean> {
    const sid = await this.ensureSession();
    try {
      const res = await fetch(`${this.apiBase}/api/claim-ball`, {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ session_id: sid, ball_index: ballIndex, username }),
      });
      if (res.ok) {
        this.currentUsername.set(username);
        try {
          localStorage.setItem(TrackingService.STORAGE_KEY, username);
        } catch {
          // ignore
        }
        return true;
      }
    } catch {
      // ignore
    }
    return false;
  }
}
