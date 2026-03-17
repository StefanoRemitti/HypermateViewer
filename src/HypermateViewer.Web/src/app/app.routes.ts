import { Routes } from '@angular/router';
import { FiringDashboardComponent } from './features/firing-dashboard/firing-dashboard.component';

export const routes: Routes = [
  { path: '', component: FiringDashboardComponent },
  { path: '**', redirectTo: '' }
];
