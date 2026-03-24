import { Component, input, computed } from '@angular/core';
import { NgClass } from '@angular/common';

export type TimelineStepId = 'A1' | 'B1' | 'B2' | 'C1';

export interface TimelineEvent {
  stepId:    TimelineStepId;
  label:     string;
  orderCode: string;
  timestamp: string;
}

@Component({
  selector: 'app-step-timeline',
  standalone: true,
  imports: [NgClass],
  templateUrl: './step-timeline.component.html',
  styleUrl: './step-timeline.component.scss'
})
export class StepTimelineComponent {
  events = input<TimelineEvent[]>([]);

  /** Sorted events by timestamp for display */
  sortedEvents = computed(() =>
    [...this.events()].sort((a, b) => a.timestamp.localeCompare(b.timestamp))
  );

  /** Minimum and maximum timestamps for positioning */
  private timeRange = computed(() => {
    const evts = this.sortedEvents();
    if (evts.length === 0) return { minMs: 0, rangeMs: 1 };
    const times = evts.map(e => new Date(e.timestamp).getTime());
    const minMs = Math.min(...times);
    const maxMs = Math.max(...times);
    return { minMs, rangeMs: Math.max(maxMs - minMs, 1) };
  });

  /** Compute horizontal position (0–100%) for an event */
  positionOf(event: TimelineEvent): number {
    const { minMs, rangeMs } = this.timeRange();
    const ms = new Date(event.timestamp).getTime();
    if (this.sortedEvents().length === 1) return 50;
    return ((ms - minMs) / rangeMs) * 88 + 6; // 6–94% to keep labels visible
  }

  formatTime(ts: string): string {
    if (!ts) return '';
    const datePart = ts.slice(5, 10).replace('-', '/');
    const timePart = ts.slice(11, 19);
    return `${datePart} ${timePart}`;
  }

  stepColor(stepId: TimelineStepId): string {
    switch (stepId) {
      case 'A1': return 'orange';
      case 'B1': return 'blue';
      case 'B2': return 'green';
      case 'C1': return 'purple';
    }
  }
}
