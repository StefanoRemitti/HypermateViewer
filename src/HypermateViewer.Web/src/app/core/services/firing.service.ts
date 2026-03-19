import { Injectable, inject } from '@angular/core';
import { HttpClient, HttpParams } from '@angular/common/http';
import { Observable } from 'rxjs';
import { environment } from '../../../environments/environment';
import { LineInfo } from '../models/line-info.model';
import { CalledOrder } from '../models/called-order.model';
import { ActiveOrder } from '../models/active-order.model';
import { Counter } from '../models/counter.model';
import { CountersActivation } from '../models/counters-activation.model';

@Injectable({ providedIn: 'root' })
export class FiringService {
  private readonly http = inject(HttpClient);
  private readonly baseUrl = `${environment.apiBaseUrl}/api/firing`;

  getLines(): Observable<LineInfo[]> {
    return this.http.get<LineInfo[]>(`${this.baseUrl}/lines`);
  }

  getCalledOrder(line: string): Observable<CalledOrder> {
    return this.http.get<CalledOrder>(`${this.baseUrl}/${line}/called-order`);
  }

  getActiveOrder(line: string, machine: 'entry' | 'exit'): Observable<ActiveOrder> {
    return this.http.get<ActiveOrder>(`${this.baseUrl}/${line}/active-order/${machine}`);
  }

  getCountersActivation(line: string): Observable<CountersActivation> {
    return this.http.get<CountersActivation>(`${this.baseUrl}/${line}/counters-activation`);
  }

  getCounters(line: string, moErpCode: string, endTime?: string): Observable<Counter[]> {
    let params = new HttpParams().set('moErpCode', moErpCode);
    if (endTime) {
      params = params.set('endTime', endTime);
    }
    return this.http.get<Counter[]>(`${this.baseUrl}/${line}/counters`, { params });
  }

  getLiveCounters(line: string, moErpCode: string, startTime: string): Observable<Counter[]> {
    const params = new HttpParams()
      .set('moErpCode', moErpCode)
      .set('startTime', startTime);
    return this.http.get<Counter[]>(`${this.baseUrl}/${line}/counters/live`, { params });
  }
}
