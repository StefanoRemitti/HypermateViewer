import {
  Component,
  OnInit,
  inject,
  signal,
  computed,
  DestroyRef
} from '@angular/core';
import { DatePipe } from '@angular/common';
import { takeUntilDestroyed } from '@angular/core/rxjs-interop';
import { interval, switchMap, catchError, of, startWith, combineLatest } from 'rxjs';

import { FiringService } from '../../core/services/firing.service';
import { LineInfo } from '../../core/models/line-info.model';
import { CalledOrder } from '../../core/models/called-order.model';
import { ActiveOrder } from '../../core/models/active-order.model';
import { Counter } from '../../core/models/counter.model';
import { StepStatus } from './components/order-step/order-step.component';

import { LineSelectorComponent } from './components/line-selector/line-selector.component';
import { OrderStepComponent } from './components/order-step/order-step.component';

const STATUS_POLL_MS  = 5_000;
const COUNTER_POLL_MS = 10_000;

@Component({
  selector: 'app-firing-dashboard',
  standalone: true,
  imports: [LineSelectorComponent, OrderStepComponent, DatePipe],
  templateUrl: './firing-dashboard.component.html',
  styleUrl: './firing-dashboard.component.scss'
})
export class FiringDashboardComponent implements OnInit {
  private readonly firingService = inject(FiringService);
  private readonly destroyRef    = inject(DestroyRef);

  lines           = signal<LineInfo[]>([]);
  selectedLine    = signal<LineInfo | null>(null);
  lastUpdated     = signal<Date | null>(null);

  calledOrder     = signal<CalledOrder | null>(null);
  entryOrder      = signal<ActiveOrder | null>(null);
  exitOrder       = signal<ActiveOrder | null>(null);

  entryCounters   = signal<Counter | null>(null);
  entryLive       = signal<Counter | null>(null);
  exitCounters    = signal<Counter | null>(null);
  exitLive        = signal<Counter | null>(null);

  entryStatus = computed<StepStatus>(() => {
    const called = this.calledOrder();
    const entry  = this.entryOrder();
    if (!entry) return 'grey';
    if (!called) return 'yellow';
    return entry.orderNumber === called.orderNumber ? 'green' : 'yellow';
  });

  exitStatus = computed<StepStatus>(() => {
    const called = this.calledOrder();
    const exit   = this.exitOrder();
    if (!exit) return 'grey';
    if (!called) return 'yellow';
    return exit.orderNumber === called.orderNumber ? 'green' : 'yellow';
  });

  ngOnInit(): void {
    this.firingService.getLines().pipe(
      takeUntilDestroyed(this.destroyRef)
    ).subscribe(lines => {
      this.lines.set(lines);
      if (lines.length > 0) {
        this.selectedLine.set(lines[0]);
        this.startPolling();
      }
    });
  }

  selectLine(line: LineInfo): void {
    this.selectedLine.set(line);
    this.calledOrder.set(null);
    this.entryOrder.set(null);
    this.exitOrder.set(null);
    this.entryCounters.set(null);
    this.entryLive.set(null);
    this.exitCounters.set(null);
    this.exitLive.set(null);
  }

  private startPolling(): void {
    // Status polling (every 5s)
    interval(STATUS_POLL_MS).pipe(
      startWith(0),
      switchMap(() => {
        const line = this.selectedLine();
        if (!line) return of(null);
        return combineLatest([
          this.firingService.getCalledOrder(line.id).pipe(catchError(() => of(null))),
          this.firingService.getActiveOrder(line.id, 'entry').pipe(catchError(() => of(null))),
          this.firingService.getActiveOrder(line.id, 'exit').pipe(catchError(() => of(null)))
        ]);
      }),
      takeUntilDestroyed(this.destroyRef)
    ).subscribe(result => {
      if (!result) return;
      const [called, entry, exit] = result;
      this.calledOrder.set(called);
      this.entryOrder.set(entry);
      this.exitOrder.set(exit);
      this.lastUpdated.set(new Date());
    });

    // Counters polling (every 10s)
    interval(COUNTER_POLL_MS).pipe(
      startWith(0),
      switchMap(() => {
        const line   = this.selectedLine();
        const called = this.calledOrder();
        const entry  = this.entryOrder();
        const exit   = this.exitOrder();
        if (!line) return of(null);

        const entryCode   = entry?.orderNumber  ?? '';
        const exitCode    = exit?.orderNumber   ?? '';
        const calledCode  = called?.orderNumber ?? '';
        const startTime   = called?.eventTime   ?? '';

        return combineLatest([
          this.firingService.getCounters(line.id, entryCode).pipe(catchError(() => of([]))),
          this.firingService.getLiveCounters(line.id, calledCode, startTime).pipe(catchError(() => of([]))),
          this.firingService.getCounters(line.id, exitCode).pipe(catchError(() => of([]))),
          this.firingService.getLiveCounters(line.id, calledCode, startTime).pipe(catchError(() => of([])))
        ]);
      }),
      takeUntilDestroyed(this.destroyRef)
    ).subscribe(result => {
      if (!result) return;
      const [entryC, entryL, exitC, exitL] = result;
      this.entryCounters.set(this.findInbound(entryC));
      this.entryLive.set(this.findInbound(entryL));
      this.exitCounters.set(this.findOutbound(exitC));
      this.exitLive.set(this.findOutbound(exitL));
    });
  }

  private findInbound(counters: Counter[]): Counter | null {
    return counters.find(c => c.machine?.toLowerCase() === 'inbound') ?? null;
  }

  private findOutbound(counters: Counter[]): Counter | null {
    return counters.find(c => c.machine?.toLowerCase() === 'outbound') ?? null;
  }
}
