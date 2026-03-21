import { Routes } from '@angular/router';

export const routes: Routes = [
  {
    path: '',
    loadComponent: () => import('./components/layout/layout.component').then(m => m.LayoutComponent),
    children: [
      { path: '', redirectTo: 'professional', pathMatch: 'full' },
      {
        path: 'professional',
        loadComponent: () => import('./components/green-view/green-view.component').then(m => m.GreenViewComponent),
      },
      {
        path: 'games',
        loadComponent: () => import('./components/games/games.component').then(m => m.GamesComponent),
      },
    ],
  },
  { path: 'green', redirectTo: 'professional', pathMatch: 'full' },
];
