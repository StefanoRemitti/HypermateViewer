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

  /** Events with a valid, parseable timestamp — invalid entries are excluded from rendering. */
  private validEvents = computed(() =>
    this.events().filter(e => {
      if (!e.timestamp) return false;
      const ms = new Date(e.timestamp).getTime();
      return !isNaN(ms);
    })
  );

  /** Sorted events by timestamp (ascending) for display */
  sortedEvents = computed(() =>
    [...this.validEvents()].sort(
      (a, b) => new Date(a.timestamp).getTime() - new Date(b.timestamp).getTime()
    )
  );

  /** Minimum and maximum timestamps for proportional positioning */
  private timeRange = computed(() => {
    const evts = this.sortedEvents();
    if (evts.length === 0) return { minMs: 0, rangeMs: 1 };
    const times = evts.map(e => new Date(e.timestamp).getTime());
    const minMs = Math.min(...times);
    const maxMs = Math.max(...times);
    return { minMs, rangeMs: Math.max(maxMs - minMs, 1) };
  });

  /** Compute horizontal position (6–94%) for an event, keeping labels within bounds. */
  positionOf(event: TimelineEvent): number {
    const { minMs, rangeMs } = this.timeRange();
    const ms = new Date(event.timestamp).getTime();
    if (this.sortedEvents().length === 1) return 50;
    return ((ms - minMs) / rangeMs) * 88 + 6;
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
