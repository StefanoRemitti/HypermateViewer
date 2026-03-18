import {
  Component,
  OnInit,
  inject,
  signal,
  computed,
  DestroyRef
} from '@angular/core';
import { DatePipe } from '@angular/common';
import { RouterLink } from '@angular/router';
import { takeUntilDestroyed } from '@angular/core/rxjs-interop';
import { Subject, merge, interval, switchMap, catchError, of, combineLatest } from 'rxjs';

import { FiringService } from '../../core/services/firing.service';
import { LogsService } from '../../core/services/logs.service';
import { LineInfo } from '../../core/models/line-info.model';
import { CalledOrder } from '../../core/models/called-order.model';
import { ActiveOrder } from '../../core/models/active-order.model';
import { CountersActivation } from '../../core/models/counters-activation.model';
import { Counter } from '../../core/models/counter.model';
import { StepStatus } from './components/order-step/order-step.component';

import { LineSelectorComponent } from './components/line-selector/line-selector.component';
import { OrderStepComponent } from './components/order-step/order-step.component';

const STATUS_POLL_MS  = 5_000;
const COUNTER_POLL_MS = 10_000;

interface SubPhaseState {
  status: StepStatus;
  code: string;
}

@Component({
  selector: 'app-firing-dashboard',
  standalone: true,
  imports: [LineSelectorComponent, OrderStepComponent, DatePipe, RouterLink],
  templateUrl: './firing-dashboard.component.html',
  styleUrl: './firing-dashboard.component.scss'
})
export class FiringDashboardComponent implements OnInit {
  private readonly firingService = inject(FiringService);
  private readonly logsService   = inject(LogsService);
  private readonly destroyRef    = inject(DestroyRef);

  private readonly refreshTrigger$        = new Subject<void>();
  private readonly counterRefreshTrigger$ = new Subject<void>();

  // Previous sub-phase states for change-detection logging
  private prevA1: SubPhaseState = { status: 'grey', code: '' };
  private prevB1: SubPhaseState = { status: 'grey', code: '' };
  private prevB2: SubPhaseState = { status: 'grey', code: '' };
  private prevC1: SubPhaseState = { status: 'grey', code: '' };

  lines                = signal<LineInfo[]>([]);
  selectedLine         = signal<LineInfo | null>(null);
  lastUpdated          = signal<Date | null>(null);

  calledOrder          = signal<CalledOrder | null>(null);
  entryOrder           = signal<ActiveOrder | null>(null);
  exitOrder            = signal<ActiveOrder | null>(null);
  countersActivation   = signal<CountersActivation | null>(null);

  entryCounters        = signal<Counter | null>(null);
  entryLive            = signal<Counter | null>(null);
  exitCounters         = signal<Counter | null>(null);
  exitLive             = signal<Counter | null>(null);

  ngOnInit(): void {
    this.firingService.getLines().pipe(
      takeUntilDestroyed(this.destroyRef)
    ).subscribe(lines => {
      this.lines.set(lines);
      if (lines.length > 0) {
        this.selectedLine.set(lines[0]);
        this.startPolling();
        this.refreshTrigger$.next();
      }
    });
  }

  selectLine(line: LineInfo): void {
    this.selectedLine.set(line);
    this.calledOrder.set(null);
    this.entryOrder.set(null);
    this.exitOrder.set(null);
    this.countersActivation.set(null);
    this.entryCounters.set(null);
    this.entryLive.set(null);
    this.exitCounters.set(null);
    this.exitLive.set(null);
    // Reset previous states so line switch doesn't trigger spurious logs
    this.prevA1 = { status: 'grey', code: '' };
    this.prevB1 = { status: 'grey', code: '' };
    this.prevB2 = { status: 'grey', code: '' };
    this.prevC1 = { status: 'grey', code: '' };
    this.refreshTrigger$.next();
  }

  private startPolling(): void {
    // Status polling: immediate trigger + every 5s
    merge(this.refreshTrigger$, interval(STATUS_POLL_MS)).pipe(
      switchMap(() => {
        const line = this.selectedLine();
        if (!line) return of(null);
        return combineLatest([
          this.firingService.getCalledOrder(line.id).pipe(catchError(() => of(null))),
          this.firingService.getActiveOrder(line.id, 'entry').pipe(catchError(() => of(null))),
          this.firingService.getActiveOrder(line.id, 'exit').pipe(catchError(() => of(null))),
          this.firingService.getCountersActivation(line.id).pipe(catchError(() => of(null)))
        ]);
      }),
      takeUntilDestroyed(this.destroyRef)
    ).subscribe(result => {
      if (!result) return;
      const [called, entry, exit, activation] = result;
      this.calledOrder.set(called);
      this.entryOrder.set(entry);
      this.exitOrder.set(exit);
      this.countersActivation.set(activation);
      this.lastUpdated.set(new Date());
      this.checkAndLogStateChanges(called, entry, exit, activation);
      // After step data arrives, immediately trigger counter refresh
      this.counterRefreshTrigger$.next();
    });

    // Counters polling: triggered after step fetch + every 10s
    merge(this.counterRefreshTrigger$, interval(COUNTER_POLL_MS)).pipe(
      switchMap(() => {
        const line   = this.selectedLine();
        const called = this.calledOrder();
        const entry  = this.entryOrder();
        const exit   = this.exitOrder();
        if (!line) return of(null);

        const entryCode  = entry?.orderNumber  ?? '';
        const exitCode   = exit?.orderNumber   ?? '';
        const calledCode = called?.orderNumber ?? '';
        const startTime  = called?.eventTime   ?? '';

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

  private checkAndLogStateChanges(
    called:     CalledOrder | null,
    entry:      ActiveOrder | null,
    exit:       ActiveOrder | null,
    activation: CountersActivation | null
  ): void {
    const line = this.selectedLine();
    if (!line) return;

    const calledNum = called?.orderNumber ?? '';

    // A1 - Chiamata ordine Hypermate
    const a1Status = called ? 'green' : 'grey' as StepStatus;
    const a1Code   = called?.orderNumber ?? '';
    this.maybeLog(line.id, 'Linea', 'Chiamata Ordine - Chiamata ordine Hypermate',
      this.prevA1, { status: a1Status, code: a1Code });
    this.prevA1 = { status: a1Status, code: a1Code };

    // B1 - Predisposizione ordine Hypermate (entry)
    const b1Status = this.computeMatchStatus(entry?.orderNumber, calledNum);
    const b1Code   = entry?.orderNumber ?? '';
    this.maybeLog(line.id, 'Ingresso', 'Attivazione Ingresso - Predisposizione ordine Hypermate',
      this.prevB1, { status: b1Status, code: b1Code });
    this.prevB1 = { status: b1Status, code: b1Code };

    // B2 - Pressione pulsante avvio conteggi
    const b2Status = this.computeMatchStatus(activation?.orderNumber, calledNum);
    const b2Code   = activation?.orderNumber ?? '';
    this.maybeLog(line.id, 'Ingresso', 'Attivazione Ingresso - Pressione pulsante avvio conteggi',
      this.prevB2, { status: b2Status, code: b2Code });
    this.prevB2 = { status: b2Status, code: b2Code };

    // C1 - Predisposizione ordine Hypermate (exit)
    const c1Status = this.computeMatchStatus(exit?.orderNumber, calledNum);
    const c1Code   = exit?.orderNumber ?? '';
    this.maybeLog(line.id, 'Uscita', 'Attivazione Uscita - Predisposizione ordine Hypermate',
      this.prevC1, { status: c1Status, code: c1Code });
    this.prevC1 = { status: c1Status, code: c1Code };
  }

  private maybeLog(
    line:        string,
    machine:     string,
    stepDesc:    string,
    prev:        SubPhaseState,
    curr:        SubPhaseState
  ): void {
    if (prev.status === curr.status && prev.code === curr.code) return;

    this.logsService.insertLog({
      line,
      machine,
      stepDescription: stepDesc,
      oldState:   this.stateLabel(prev.status),
      newState:   this.stateLabel(curr.status),
      oldErpCode: prev.code,
      newErpCode: curr.code
    }).pipe(catchError(err => { console.error('Log insertion failed:', err); return of(null); })).subscribe();
  }

  private computeMatchStatus(
    dataNum: string | undefined | null,
    refNum:  string | undefined | null
  ): StepStatus {
    if (!dataNum) return 'grey';
    if (!refNum)  return 'yellow';
    return dataNum === refNum ? 'green' : 'yellow';
  }

  private stateLabel(status: StepStatus): string {
    switch (status) {
      case 'green':  return 'valido';
      case 'yellow': return 'attesa';
      case 'grey':   return 'nessun dato';
    }
  }

  private findInbound(counters: Counter[]): Counter | null {
    return counters.find(c => c.machine?.toLowerCase() === 'inbound') ?? null;
  }

  private findOutbound(counters: Counter[]): Counter | null {
    return counters.find(c => c.machine?.toLowerCase() === 'outbound') ?? null;
  }
}
