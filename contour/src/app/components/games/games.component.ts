import { Component } from '@angular/core';

export interface PuttingGame {
  id: string;
  name: string;
  description: string;
  minPlayers: number;
  maxPlayers: number;
}

@Component({
  selector: 'app-games',
  standalone: true,
  templateUrl: './games.component.html',
  styleUrl: './games.component.css',
})
export class GamesComponent {
  readonly games: PuttingGame[] = [
    {
      id: 'closest-to-pin',
      name: 'Closest to the Pin',
      description: 'Each player putts once. Whoever ends up closest to the hole wins the hole. Best of 9 or 18.',
      minPlayers: 2,
      maxPlayers: 8,
    },
    {
      id: 'snake',
      name: 'Snake',
      description: 'Players take turns putting. Three-putt (or more) and you get a letter. First to spell S-N-A-K-E is out.',
      minPlayers: 2,
      maxPlayers: 8,
    },
    {
      id: 'horse',
      name: 'Horse',
      description: 'Miss a putt and you get a letter. First to spell H-O-R-S-E is eliminated. Last player standing wins.',
      minPlayers: 2,
      maxPlayers: 6,
    },
    {
      id: 'around-the-world',
      name: 'Around the World',
      description: 'Sink putts from increasing distances. First to complete the full circuit wins.',
      minPlayers: 2,
      maxPlayers: 6,
    },
    {
      id: '21',
      name: '21',
      description: 'Each putt made adds points (distance = value). First to reach 21 without going over wins.',
      minPlayers: 2,
      maxPlayers: 6,
    },
    {
      id: 'eliminator',
      name: 'Eliminator',
      description: 'Each round, the worst putter is eliminated. Last player standing wins.',
      minPlayers: 3,
      maxPlayers: 8,
    },
  ];

  playGame(game: PuttingGame): void {
    // TODO: Navigate to game setup / professional view with game mode
    console.log('Play game:', game.name);
  }
}
