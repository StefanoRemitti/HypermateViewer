import { Component, input } from '@angular/core';
import { NgClass } from '@angular/common';
import { CalledOrder } from '../../../../core/models/called-order.model';
import { ActiveOrder } from '../../../../core/models/active-order.model';
import { Counter } from '../../../../core/models/counter.model';
import { CountersDisplayComponent } from '../counters-display/counters-display.component';

export type StepType = 'called' | 'entry' | 'exit';
export type StepStatus = 'green' | 'yellow' | 'grey';

@Component({
  selector: 'app-order-step',
  standalone: true,
  imports: [NgClass, CountersDisplayComponent],
  templateUrl: './order-step.component.html',
  styleUrl: './order-step.component.scss'
})
export class OrderStepComponent {
  stepType = input.required<StepType>();
  status = input.required<StepStatus>();
  calledOrder = input<CalledOrder | null>(null);
  activeOrder = input<ActiveOrder | null>(null);
  orderCounters = input<Counter | null>(null);
  liveCounters = input<Counter | null>(null);

  get stepLabel(): string {
    switch (this.stepType()) {
      case 'called': return 'A. Chiamata Ordine';
      case 'entry':  return 'B. Attiv. Ingresso';
      case 'exit':   return 'C. Attiv. Uscita';
    }
  }

  get statusLabel(): string {
    switch (this.status()) {
      case 'green':  return 'Allineato';
      case 'yellow': return 'In attesa';
      case 'grey':   return 'Nessun dato';
    }
  }

  get orderCode(): string {
    if (this.stepType() === 'called') return this.calledOrder()?.erpCode ?? '—';
    return this.activeOrder()?.codiceOrdine ?? '—';
  }

  get orderNumber(): string {
    if (this.stepType() === 'called') return this.calledOrder()?.orderNumber ?? '—';
    return this.activeOrder()?.orderNumber ?? '—';
  }

  get eventTime(): string {
    if (this.stepType() === 'called') return this.formatTime(this.calledOrder()?.eventTime);
    return this.formatTime(this.activeOrder()?.eventTime);
  }

  private formatTime(ts: string | undefined): string {
    if (!ts) return '—';
    // ts format: "YYYY-MM-DD HH:mm:ss"
    const datePart = ts.slice(5, 10).replace('-', '/');
    const timePart = ts.slice(11, 19);
    return `${datePart} ${timePart}`;
  }
}
