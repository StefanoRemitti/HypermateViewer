import { Component, inject, signal, OnInit } from '@angular/core';
import { RouterLink } from '@angular/router';
import { FormsModule } from '@angular/forms';
import { DatePipe } from '@angular/common';
import { LogsService } from '../../core/services/logs.service';
import { LogRecord } from '../../core/models/log-record.model';
import { LogFilter } from '../../core/models/log-filter.model';

const LINE_OPTIONS = [
  { id: '',        label: 'Tutte le linee' },
  { id: 'firing4', label: 'Forno 4' },
  { id: 'firing5', label: 'Forno 5' },
  { id: 'firing6', label: 'Forno 6' }
];

const STATE_OPTIONS = [
  { id: '',           label: 'Tutti gli stati' },
  { id: 'valido',     label: 'Valido' },
  { id: 'attesa',     label: 'In attesa' },
  { id: 'nessun dato', label: 'Nessun dato' }
];

@Component({
  selector: 'app-logs-page',
  standalone: true,
  imports: [RouterLink, FormsModule, DatePipe],
  templateUrl: './logs-page.component.html',
  styleUrl: './logs-page.component.scss'
})
export class LogsPageComponent implements OnInit {
  private readonly logsService = inject(LogsService);

  readonly lineOptions  = LINE_OPTIONS;
  readonly stateOptions = STATE_OPTIONS;

  // Filter form fields
  filterLine     = '';
  filterErpCode  = '';
  filterDateFrom = '';
  filterDateTo   = '';
  filterState    = '';

  logs    = signal<LogRecord[]>([]);
  loading = signal(false);
  error   = signal<string | null>(null);

  ngOnInit(): void {
    this.search();
  }

  search(): void {
    this.loading.set(true);
    this.error.set(null);

    const filter: LogFilter = {
      line:     this.filterLine     || undefined,
      erpCode:  this.filterErpCode  || undefined,
      dateFrom: this.filterDateFrom || undefined,
      dateTo:   this.filterDateTo   || undefined,
      state:    this.filterState    || undefined
    };

    this.logsService.getLogs(filter).subscribe({
      next:  logs  => { this.logs.set(logs); this.loading.set(false); },
      error: _err  => { this.error.set('Errore nel caricamento dei log'); this.loading.set(false); }
    });
  }

  stateClass(state: string): string {
    switch (state) {
      case 'valido':      return 'state-green';
      case 'attesa':      return 'state-yellow';
      case 'nessun dato': return 'state-grey';
      default:            return '';
    }
  }
}
