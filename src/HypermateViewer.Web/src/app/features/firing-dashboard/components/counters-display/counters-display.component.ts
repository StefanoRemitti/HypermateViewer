import { Component, input } from '@angular/core';
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
  counter = input<Counter | null>(null);
}
