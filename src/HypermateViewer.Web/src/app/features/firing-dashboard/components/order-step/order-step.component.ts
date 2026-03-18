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
      return [
        {
          label:     'Ordine Hypermate Pronto',
          status:    this.matchStatus(active?.orderNumber, called?.orderNumber),
          orderCode: active?.codiceOrdine ?? '',
          eventTime: this.formatTime(active?.eventTime)
        },
        {
          label:     'Avvio Conteggi Hypermate',
          status:    this.matchStatus(activation?.orderNumber, called?.orderNumber),
          orderCode: activation?.erpCode ?? '',
          eventTime: this.formatTime(activation?.eventTime)
        }
      ];
    }

    // exit
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
