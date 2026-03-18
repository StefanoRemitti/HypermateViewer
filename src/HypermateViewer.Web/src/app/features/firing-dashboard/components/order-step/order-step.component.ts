import { Component, input, computed } from '@angular/core';
import { NgClass } from '@angular/common';
import { CalledOrder } from '../../../../core/models/called-order.model';
import { ActiveOrder } from '../../../../core/models/active-order.model';
import { CountersActivation } from '../../../../core/models/counters-activation.model';
import { Counter } from '../../../../core/models/counter.model';
import { CountersDisplayComponent } from '../counters-display/counters-display.component';

export type StepType = 'called' | 'entry' | 'exit';
export type StepStatus = 'green' | 'yellow' | 'grey';

/** Placeholder sub-phase shown when a dependency is not yet satisfied. */
const WAITING_PLACEHOLDER: Pick<SubPhase, 'status' | 'orderCode' | 'eventTime' | 'placeholder'> = {
  status:      'grey',
  orderCode:   '',
  eventTime:   '',
  placeholder: 'In attesa dello step precedente'
};

export interface SubPhase {
  label: string;
  status: StepStatus;
  orderCode: string;
  eventTime: string;
  placeholder?: string;
}

@Component({
  selector: 'app-order-step',
  standalone: true,
  imports: [NgClass, CountersDisplayComponent],
  templateUrl: './order-step.component.html',
  styleUrl: './order-step.component.scss'
})
export class OrderStepComponent {
  stepType           = input.required<StepType>();
  calledOrder        = input<CalledOrder | null>(null);
  activeOrder        = input<ActiveOrder | null>(null);
  countersActivation = input<CountersActivation | null>(null);
  orderCounters      = input<Counter | null>(null);
  liveCounters       = input<Counter | null>(null);
  /** Overall status of the preceding step; defaults to 'green' (no dependency) for step A. */
  prevStepStatus     = input<StepStatus>('green');

  get stepLabel(): string {
    switch (this.stepType()) {
      case 'called': return 'Chiamata Ordine';
      case 'entry':  return 'Attivazione Ordine Ingresso';
      case 'exit':   return 'Attivazione Ordine Uscita';
    }
  }

  subPhases = computed<SubPhase[]>(() => {
    const stepType   = this.stepType();
    const called     = this.calledOrder();
    const active     = this.activeOrder();
    const activation = this.countersActivation();

    if (stepType === 'called') {
      return [
        {
          label:       'Chiamata Ordine AGV',
          status:      'grey' as StepStatus,
          orderCode:   '',
          eventTime:   '',
          placeholder: 'In attesa connessione AGV'
        },
        {
          label:     'Chiamata Ordine Hypermate',
          status:    called ? 'green' : 'grey',
          orderCode: called?.erpCode ?? '',
          eventTime: this.formatTime(called?.eventTime)
        }
      ];
    }

    if (stepType === 'entry') {
      const b1Status = this.matchStatus(active?.orderNumber, called?.orderNumber);
      const b1: SubPhase = {
        label:     'Ordine Hypermate Pronto',
        status:    b1Status,
        orderCode: active?.codiceOrdine ?? '',
        eventTime: this.formatTime(active?.eventTime)
      };
      // B2 depends on B1: only show real data when B1 is green
      const b2: SubPhase = b1Status === 'green'
        ? {
            label:     'Avvio Conteggi Hypermate',
            status:    this.matchStatus(activation?.orderNumber, called?.orderNumber),
            orderCode: activation?.erpCode ?? '',
            eventTime: this.formatTime(activation?.eventTime)
          }
        : { label: 'Avvio Conteggi Hypermate', ...WAITING_PLACEHOLDER };
      return [b1, b2];
    }

    // exit — C1 depends on entry step (B) overall status
    const prevStatus = this.prevStepStatus();
    if (prevStatus !== 'green') {
      return [{ label: 'Avvio Conteggi Hypermate', ...WAITING_PLACEHOLDER }];
    }
    return [
      {
        label:     'Avvio Conteggi Hypermate',
        status:    this.matchStatus(active?.orderNumber, called?.orderNumber),
        orderCode: active?.codiceOrdine ?? '',
        eventTime: this.formatTime(active?.eventTime)
      }
    ];
  });

  overallStatus = computed<StepStatus>(() => {
    const stepType = this.stepType();
    const phases   = this.subPhases();

    // For step A, A1 (AGV placeholder) is always grey and excluded from overall status
    const active = stepType === 'called' ? phases.slice(1) : phases;

    if (active.every(p => p.status === 'green'))  return 'green';
    if (active.every(p => p.status === 'grey'))   return 'grey';
    return 'yellow';
  });

  liveCountersSubtitle = computed(() => {
    const activation = this.countersActivation();
    if (!activation?.eventTime) return '';
    const ts = activation.eventTime;
    const datePart = ts.slice(5, 10).replace('-', '/');
    const timePart = ts.slice(11, 19);
    return `A partire da ${datePart} ${timePart}`;
  });

  get statusLabel(): string {
    switch (this.overallStatus()) {
      case 'green':  return 'Allineato';
      case 'yellow': return 'In attesa';
      case 'grey':   return 'Nessun dato';
    }
  }

  private matchStatus(
    dataOrderNumber: string | undefined | null,
    refOrderNumber:  string | undefined | null
  ): StepStatus {
    if (!dataOrderNumber) return 'grey';
    if (!refOrderNumber)  return 'yellow';
    return dataOrderNumber === refOrderNumber ? 'green' : 'yellow';
  }

  private formatTime(ts: string | undefined): string {
    if (!ts) return '';
    const datePart = ts.slice(5, 10).replace('-', '/');
    const timePart = ts.slice(11, 19);
    return `${datePart} ${timePart}`;
  }
}
