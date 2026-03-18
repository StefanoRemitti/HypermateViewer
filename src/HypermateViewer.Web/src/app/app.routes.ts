import { Routes } from '@angular/router';
import { FiringDashboardComponent } from './features/firing-dashboard/firing-dashboard.component';
import { LogsPageComponent } from './features/logs/logs-page.component';

export const routes: Routes = [
  { path: '', component: FiringDashboardComponent },
  { path: 'logs', component: LogsPageComponent },
  { path: '**', redirectTo: '' }
];
