import { Injectable, inject } from '@angular/core';
import { HttpClient, HttpParams } from '@angular/common/http';
import { Observable } from 'rxjs';
import { environment } from '../../../environments/environment';
import { LogEntry } from '../models/log-entry.model';
import { LogRecord } from '../models/log-record.model';
import { LogFilter } from '../models/log-filter.model';

@Injectable({ providedIn: 'root' })
export class LogsService {
  private readonly http = inject(HttpClient);
  private readonly baseUrl = `${environment.apiBaseUrl}/api/logs`;

  insertLog(entry: LogEntry): Observable<void> {
    return this.http.post<void>(this.baseUrl, entry);
  }

  getLogs(filter: LogFilter): Observable<LogRecord[]> {
    let params = new HttpParams();
    if (filter.line)     params = params.set('line', filter.line);
    if (filter.erpCode)  params = params.set('erpCode', filter.erpCode);
    if (filter.dateFrom) params = params.set('dateFrom', filter.dateFrom);
    if (filter.dateTo)   params = params.set('dateTo', filter.dateTo);
    if (filter.state)    params = params.set('state', filter.state);
    return this.http.get<LogRecord[]>(this.baseUrl, { params });
  }
}
