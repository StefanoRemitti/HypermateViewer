import { Component, input, output } from '@angular/core';
import { LineInfo } from '../../../../core/models/line-info.model';

@Component({
  selector: 'app-line-selector',
  standalone: true,
  templateUrl: './line-selector.component.html',
  styleUrl: './line-selector.component.scss'
})
export class LineSelectorComponent {
  lines = input.required<LineInfo[]>();
  selectedLine = input.required<LineInfo>();
  lineSelected = output<LineInfo>();

  selectLine(line: LineInfo): void {
    this.lineSelected.emit(line);
  }
}
