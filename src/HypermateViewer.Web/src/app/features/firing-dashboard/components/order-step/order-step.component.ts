import { Component, input, computed } from '@angular/core';
import { NgClass } from '@angular/common';
import { CalledOrder } from '../../../../core/models/called-order.model';
import { ActiveOrder } from '../../../../core/models/active-order.model';
import { CountersActivation } from '../../../../core/models/counters-activation.model';
import { Counter } from '../../../../core/models/counter.model';
import { CountersDisplayComponent } from '../counters-display/counters-display.component';

export type StepType = 'called' | 'entry' | 'exit';
export type StepStatus = 'green' | 'yellow' | 'grey';

export interface SubPhase {
  label: string;
  status: StepStatus;
  orderCode: string;
  eventTime: string;
  placeholder?: string;
  /** True when the order code shown refers to the previous (not yet replaced) order. */
  isPreviousOrder?: boolean;
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
  /** Order code used to filter the displayed counters (may differ from calledOrder when not aligned). */
  countersOrderCode  = input<string>('');

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
      // Only flag as "previous order" when there IS a reference called order to compare against
      const b1IsPrev = b1Status === 'yellow' && !!active?.orderNumber && !!called?.orderNumber;
      const b1: SubPhase = {
        label:          b1IsPrev ? 'Attesa scaricamento da Box' : 'Ordine Hypermate Pronto',
        status:         b1Status,
        orderCode:      active?.codiceOrdine ?? '',
        eventTime:      this.formatTime(active?.eventTime),
        isPreviousOrder: b1IsPrev
      };

      const b2Status = this.matchStatus(activation?.orderNumber, called?.orderNumber);
      const b2IsPrev = b2Status === 'yellow' && !!activation?.orderNumber && !!called?.orderNumber;
      const b2: SubPhase = {
        label:          b2IsPrev ? 'Attesa pressione pulsante in ingresso' : 'Avvio Conteggi Hypermate',
        status:         b2Status,
        orderCode:      activation?.erpCode ?? '',
        eventTime:      this.formatTime(activation?.eventTime),
        isPreviousOrder: b2IsPrev
      };
      return [b1, b2];
    }

    // exit — C1 always shown regardless of entry step status
    const c1Status = this.matchStatus(active?.orderNumber, called?.orderNumber);
    const c1IsPrev = c1Status === 'yellow' && !!active?.orderNumber && !!called?.orderNumber;
    return [
      {
        label:          c1IsPrev ? 'Attesa cambio prodotto in uscita' : 'Avvio Conteggi Hypermate',
        status:         c1Status,
        orderCode:      active?.codiceOrdine ?? '',
        eventTime:      this.formatTime(active?.eventTime),
        isPreviousOrder: c1IsPrev
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

  /** True when the step is not fully validated — drives the pulse animation. */
  isPending = computed(() => this.overallStatus() !== 'green');

  /** True when counters shown belong to the previous order (not the currently called order). */
  countersIsPreviousOrder = computed(() => {
    const calledNum = this.calledOrder()?.orderNumber ?? '';
    const counterCode = this.countersOrderCode();
    return !!counterCode && !!calledNum && counterCode !== calledNum;
  });

  liveCountersSubtitle = computed(() => {
    const activation = this.countersActivation();
    const active     = this.activeOrder();
    // For exit, use activeOrder's eventTime; for entry, use activation's eventTime
    const ts = activation?.eventTime ?? active?.eventTime;
    if (!ts) return '';
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
