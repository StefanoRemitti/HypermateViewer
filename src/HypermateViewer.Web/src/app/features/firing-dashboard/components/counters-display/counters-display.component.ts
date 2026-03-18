import { Component, input, computed } from '@angular/core';
import { DecimalPipe, SlicePipe } from '@angular/common';
import { Counter } from '../../../../core/models/counter.model';

@Component({
  selector: 'app-counters-display',
  standalone: true,
  imports: [DecimalPipe, SlicePipe],
  templateUrl: './counters-display.component.html',
  styleUrl: './counters-display.component.scss'
})
export class CountersDisplayComponent {
  label = input.required<string>();
  subtitle = input<string>('');
  counter = input<Counter | null>(null);

  displayM2 = computed(() => {
    const m2 = this.counter()?.m2;
    return m2 && m2.trim() !== '' ? m2 : '0';
  });
}
