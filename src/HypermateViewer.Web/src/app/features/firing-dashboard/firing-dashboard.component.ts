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
import { Subject, merge, interval, switchMap, catchError, of, combineLatest, forkJoin, fromEvent, EMPTY, startWith, map } from 'rxjs';

import { FiringService } from '../../core/services/firing.service';
import { LogsService } from '../../core/services/logs.service';
import { LogRecord } from '../../core/models/log-record.model';
import { LineInfo } from '../../core/models/line-info.model';
import { CalledOrder } from '../../core/models/called-order.model';
import { ActiveOrder } from '../../core/models/active-order.model';
import { CountersActivation } from '../../core/models/counters-activation.model';
import { Counter } from '../../core/models/counter.model';
import { StepStatus } from './components/order-step/order-step.component';
import { TimelineEvent } from './components/step-timeline/step-timeline.component';

import { LineSelectorComponent } from './components/line-selector/line-selector.component';
import { OrderStepComponent } from './components/order-step/order-step.component';
import { StepTimelineComponent } from './components/step-timeline/step-timeline.component';

const STATUS_POLL_MS  = 5_000;
const COUNTER_POLL_MS = 15_000;

interface SubPhaseState {
  status: StepStatus;
  code: string;
}

@Component({
  selector: 'app-firing-dashboard',
  standalone: true,
  imports: [LineSelectorComponent, OrderStepComponent, StepTimelineComponent, DatePipe, RouterLink],
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
  private prevStatesLoaded = false;

  // Separate baseline caches for entry and exit counters
  private readonly entryCountersBaseline = signal<Counter | null>(null);
  private readonly exitCountersBaseline  = signal<Counter | null>(null);
  private entryBaselineFetched   = false;
  private exitBaselineFetched    = false;
  private lastEntryBaselineCode  = '';
  private lastExitBaselineCode   = '';
  loading              = signal<boolean>(true);

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

  /** Order code used to filter entry counters (= countersActivation.orderNumber). */
  entryCounterCode = computed(() => this.countersActivation()?.orderNumber ?? '');

  /** Order code used to filter exit counters (= exitOrder.orderNumber). */
  exitCounterCode = computed(() => this.exitOrder()?.orderNumber ?? '');

  /** Timeline events for the current called order (recomputed whenever signals change). */
  timelineEvents = computed<TimelineEvent[]>(() => {
    const called     = this.calledOrder();
    const entry      = this.entryOrder();
    const activation = this.countersActivation();
    const exit       = this.exitOrder();

    if (!called) return [];
    const calledNum = called.orderNumber;
    const events: TimelineEvent[] = [];

    // A1 – Chiamata Ordine Hypermate (always shown for current order)
    events.push({
      stepId:    'A1',
      label:     'Chiamata Ordine',
      orderCode: called.erpCode,
      timestamp: called.eventTime
    });

    // B1 – Ordine Hypermate Pronto (only if aligned with called order)
    if (entry && entry.orderNumber === calledNum) {
      events.push({
        stepId:    'B1',
        label:     'Ordine Pronto',
        orderCode: entry.codiceOrdine,
        timestamp: entry.eventTime
      });
    }

    // B2 – Avvio Conteggi Hypermate (only if aligned with called order)
    if (activation && activation.orderNumber === calledNum) {
      events.push({
        stepId:    'B2',
        label:     'Avvio Conteggi',
        orderCode: activation.erpCode,
        timestamp: activation.eventTime
      });
    }

    // C1 – Attivazione Uscita (only if aligned with called order)
    if (exit && exit.orderNumber === calledNum) {
      events.push({
        stepId:    'C1',
        label:     'Attivazione Uscita',
        orderCode: exit.codiceOrdine,
        timestamp: exit.eventTime
      });
    }

    return events;
  });

  ngOnInit(): void {
    this.firingService.getLines().pipe(
      takeUntilDestroyed(this.destroyRef)
    ).subscribe(lines => {
      this.lines.set(lines);
      if (lines.length > 0) {
        this.selectedLine.set(lines[0]);
        this.startPolling();
        this.loadPreviousStatesAndStart(lines[0].id);
      }
    });
  }

  selectLine(line: LineInfo): void {
    this.selectedLine.set(line);
    this.loading.set(true);
    this.calledOrder.set(null);
    this.entryOrder.set(null);
    this.exitOrder.set(null);
    this.countersActivation.set(null);
    this.entryCounters.set(null);
    this.entryLive.set(null);
    this.exitCounters.set(null);
    this.exitLive.set(null);
    this.entryCountersBaseline.set(null);
    this.exitCountersBaseline.set(null);
    this.entryBaselineFetched  = false;
    this.exitBaselineFetched   = false;
    this.lastEntryBaselineCode = '';
    this.lastExitBaselineCode  = '';
    this.prevStatesLoaded = false;
    this.loadPreviousStatesAndStart(line.id);
  }

  private startPolling(): void {
    // Emits immediately on every tab focus, then at the given interval.
    // Pauses entirely (EMPTY) while the tab is hidden.
    const visibleTick = (ms: number) =>
      fromEvent(document, 'visibilitychange').pipe(
        startWith(null),
        map(() => !document.hidden),
        switchMap(visible => visible
          ? interval(ms).pipe(startWith(0))
          : EMPTY
        )
      );

    merge(this.refreshTrigger$, visibleTick(STATUS_POLL_MS)).pipe(
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
      this.loading.set(false);
      this.checkAndLogStateChanges(called, entry, exit, activation);

      // Reset entry baseline when countersActivation order changes
      const entryCode = activation?.orderNumber ?? '';
      if (entryCode !== this.lastEntryBaselineCode) {
        this.entryCountersBaseline.set(null);
        this.entryBaselineFetched = false;
        this.lastEntryBaselineCode = entryCode;
      }

      // Reset exit baseline when exitOrder order changes
      const exitCode = exit?.orderNumber ?? '';
      if (exitCode !== this.lastExitBaselineCode) {
        this.exitCountersBaseline.set(null);
        this.exitBaselineFetched = false;
        this.lastExitBaselineCode = exitCode;
      }

      // After step data arrives, immediately trigger counter refresh
      this.counterRefreshTrigger$.next();
    });

    // Counter poll: uses countersActivation.orderNumber for entry and exitOrder.orderNumber for exit.
    // Baseline (heavy query) is fetched once per order; live (light query) is fetched every cycle.
    merge(this.counterRefreshTrigger$, visibleTick(COUNTER_POLL_MS)).pipe(
      switchMap(() => {
        const line       = this.selectedLine();
        const activation = this.countersActivation();
        const exit       = this.exitOrder();
        if (!line) return of(null);

        const entryCode  = activation?.orderNumber ?? '';
        const entryStart = activation?.eventTime   ?? '';
        const entryEnd   = activation?.eventTime   ?? undefined;

        const exitCode   = exit?.orderNumber ?? '';
        const exitStart  = exit?.eventTime   ?? '';
        const exitEnd    = exit?.eventTime   ?? undefined;

        const needEntryBaseline = !this.entryBaselineFetched;
        const needExitBaseline  = !this.exitBaselineFetched;

        if (needEntryBaseline || needExitBaseline) {
          const entryBaseline$ = needEntryBaseline && entryCode
            ? this.firingService.getCounters(line.id, entryCode, entryEnd).pipe(catchError(() => of([])))
            : of(null);
          const exitBaseline$ = needExitBaseline && exitCode
            ? this.firingService.getCounters(line.id, exitCode, exitEnd).pipe(catchError(() => of([])))
            : of(null);
          const entryLive$ = entryCode
            ? this.firingService.getLiveCounters(line.id, entryCode, entryStart).pipe(catchError(() => of([])))
            : of([]);
          const exitLive$ = exitCode
            ? this.firingService.getLiveCounters(line.id, exitCode, exitStart).pipe(catchError(() => of([])))
            : of([]);

          return combineLatest([entryBaseline$, exitBaseline$, entryLive$, exitLive$]).pipe(
            map(([entryB, exitB, entryL, exitL]) => ({
              type:       'full' as const,
              entryBaseline: entryB,
              exitBaseline:  exitB,
              entryLive:  entryL,
              exitLive:   exitL,
              fetchedEntryBaseline: needEntryBaseline,
              fetchedExitBaseline:  needExitBaseline
            }))
          );
        }

        // Subsequent polls: only light live queries
        const entryLive$ = entryCode
          ? this.firingService.getLiveCounters(line.id, entryCode, entryStart).pipe(catchError(() => of([])))
          : of([]);
        const exitLive$ = exitCode
          ? this.firingService.getLiveCounters(line.id, exitCode, exitStart).pipe(catchError(() => of([])))
          : of([]);

        return combineLatest([entryLive$, exitLive$]).pipe(
          map(([entryL, exitL]) => ({ type: 'live' as const, entryLive: entryL, exitLive: exitL }))
        );
      }),
      takeUntilDestroyed(this.destroyRef)
    ).subscribe(result => {
      if (!result) return;

      if (result.type === 'full') {
        if (result.fetchedEntryBaseline && result.entryBaseline !== null) {
          this.entryCountersBaseline.set(this.findInbound(result.entryBaseline));
          this.entryBaselineFetched = true;
        }
        if (result.fetchedExitBaseline && result.exitBaseline !== null) {
          this.exitCountersBaseline.set(this.findOutbound(result.exitBaseline));
          this.exitBaselineFetched = true;
        }
        this.entryLive.set(this.findInbound(result.entryLive));
        this.exitLive.set(this.findOutbound(result.exitLive));
      } else {
        this.entryLive.set(this.findInbound(result.entryLive));
        this.exitLive.set(this.findOutbound(result.exitLive));
      }

      this.entryCounters.set(this.sumCounters(this.entryCountersBaseline(), this.entryLive()));
      this.exitCounters.set(this.sumCounters(this.exitCountersBaseline(), this.exitLive()));
    });
  }

  private checkAndLogStateChanges(
    called:     CalledOrder | null,
    entry:      ActiveOrder | null,
    exit:       ActiveOrder | null,
    activation: CountersActivation | null
  ): void {
    if (!this.prevStatesLoaded) return;

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

  private loadPreviousStatesAndStart(lineId: string): void {
    const stepDescs = [
      'Chiamata Ordine - Chiamata ordine Hypermate',
      'Attivazione Ingresso - Predisposizione ordine Hypermate',
      'Attivazione Ingresso - Pressione pulsante avvio conteggi',
      'Attivazione Uscita - Predisposizione ordine Hypermate'
    ];

    forkJoin(
      stepDescs.map(desc => this.logsService.getLatestLog(lineId, desc))
    ).subscribe({
      next: results => {
        this.prevA1 = this.logRecordToState(results[0]);
        this.prevB1 = this.logRecordToState(results[1]);
        this.prevB2 = this.logRecordToState(results[2]);
        this.prevC1 = this.logRecordToState(results[3]);
        this.prevStatesLoaded = true;
        this.refreshTrigger$.next();
      },
      error: () => {
        // Fall back to grey states so polling can still start
        this.prevStatesLoaded = true;
        this.refreshTrigger$.next();
      }
    });
  }

  private logRecordToState(record: LogRecord | null): SubPhaseState {
    if (!record) return { status: 'grey', code: '' };
    return {
      status: this.reverseStateLabel(record.newState),
      code: record.newErpCode
    };
  }

  private reverseStateLabel(state: string): StepStatus {
    switch (state) {
      case 'valido':      return 'green';
      case 'attesa':      return 'yellow';
      case 'nessun dato': return 'grey';
      default:            return 'grey';
    }
  }

  private findInbound(counters: Counter[] | null): Counter | null {
    if (!counters) return null;
    return counters.find(c => c.machine?.toLowerCase() === 'inbound') ?? null;
  }

  private findOutbound(counters: Counter[] | null): Counter | null {
    if (!counters) return null;
    return counters.find(c => c.machine?.toLowerCase() === 'outbound') ?? null;
  }

  private sumCounters(baseline: Counter | null, live: Counter | null): Counter | null {
    if (!baseline && !live) return null;
    const pieces = (baseline?.pieces ?? 0) + (live?.pieces ?? 0);
    const m2 = this.sumM2(baseline?.m2 ?? '', live?.m2 ?? '');
    return {
      lastPiece: live?.lastPiece ?? baseline?.lastPiece ?? '',
      machine:   baseline?.machine ?? live?.machine ?? '',
      pieces,
      m2
    };
  }

  private sumM2(a: string, b: string): string {
    const parse = (s: string) => parseFloat(s.replace(',', '.')) || 0;
    const sum = parse(a) + parse(b);
    if (sum === 0) return '';
    // toFixed(2) then strip trailing decimal zeros (e.g. '10.50' → '10.5', '10.00' → '10')
    return sum.toFixed(2).replace(/\.?0+$/, '');
  }
}
