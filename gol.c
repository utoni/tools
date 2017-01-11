/**
 * @author Steffen Vogel <info@steffenvogel.de>, Toni Uhlig <matzeton@googlemail.com>
 * @copyright Copyright (c) 2010-2014, Steffen Vogel, Toni Uhlig
 * @license http://opensource.org/licenses/gpl-license.php GNU Public License
 */

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <curses.h>
#include <time.h>

/* configuration */
#include "config.h"

#if defined(RANDOM_SPAWNS)
static uint8_t rnd_spawns = 0xFF;
#endif

struct config {
	uint8_t paused:1;
	uint8_t quit:1;
	uint8_t menu:1;
	uint8_t reprint:1;
};

struct pattern {
	uint8_t width;
	uint8_t height;
	uint8_t * data;
};

struct cursor {
	uint8_t x;
	uint8_t y;
};

uint8_t start[3][3] = {
	{0, 1, 1},
	{1, 1, 0},
	{0, 1, 0}
};

uint8_t glider[3][3] = {
	{0, 1, 0},
	{0, 0, 1},
	{1, 1, 1}
};

uint8_t segler[4][5] = {
	{0, 1, 1, 1, 1},
	{1, 0, 0, 0, 1},
	{0, 0, 0, 0, 1},
	{1, 0, 0, 1, 0}
};

uint8_t buffer[7][3] = {
	{1, 1, 1},
	{1, 0, 1},
	{1, 0, 1},
	{0, 0, 0},
	{1, 0, 1},
	{1, 0, 1},
	{1, 1, 1}
};

uint8_t kreuz[3][3] = {
	{0, 1, 0},
	{1, 1, 1},
	{0, 1, 0}
};

uint8_t ship[3][3] = {
	{1, 1, 0},
	{1, 0, 1},
	{0, 1, 1}
};

/* initialize world with zero (dead cells) */
void clean_world(uint8_t ** world, uint8_t width, uint8_t height) {
	int a;
	for (a = 0; a < width; a++) {
		memset(world[a], 0, height * sizeof(uint8_t));
	}
}

/* allocate memory for world */
uint8_t ** create_world(uint8_t width, uint8_t height) {
	uint8_t ** world = calloc(width, sizeof(uint8_t *));
	int a;
	for (a = 0; a < width; a++) {
		world[a] = calloc(height, sizeof(uint8_t));
	}

	clean_world(world, width, height);
	return world;
}

void free_world(uint8_t ** world, uint8_t width, uint8_t height) {
	int a;
	for (a = 0; a < width; a++) {
		free(world[a]);
	}
	free(world);
}

/* insert pattern at (x|y) into world */
void inhabit_world(struct pattern pattern, uint8_t x, uint8_t y, uint8_t ** world, uint8_t width, uint8_t height) {
	uint8_t a, b;

	for (a = 0; a < pattern.height; a++) {
		int c = a;
		if ((y + c) >= height) c -= height;

		for (b = 0; b < pattern.width; b++) {
			int d = b;
			if ((x + d) >= width) d -= width;
			world[x+d][y+c] = pattern.data[(a*pattern.width)+b];
		}
	}
}

/* calc alive cells */
uint8_t calc_cell_count(uint8_t ** world, uint8_t width, uint8_t height) {
	int cell_count = 0;
	uint8_t a, b;

	for (a = 0; a < width; a++) {
		for (b = 0; b < height; b++) {
			cell_count += (world[a][b]) ? 1 : 0;
		}
	}

	return cell_count;
}

uint8_t calc_cell_neighbours(uint8_t x, uint8_t y, uint8_t ** world, uint8_t width, uint8_t height) {
	uint8_t neighbours = 0;
	int a, b;

	for (a = x-1; a <= x+1; a++) {
		int c = a;
		if (a < 0) c += width;
		if (a >= width) c -= width;

		for (b = y-1; b <= y+1; b++) {
			int d = b;
			if (a == x && b == y) continue;
			if (b < 0) d += height;
			if (b >= height) d -= height;

			neighbours += (world[c][d] > 0) ? 1 : 0;
		}
	}

	return neighbours; /* 0 <= neighbours <= 8 */
}

uint8_t calc_next_cell_gen(uint8_t x, uint8_t y, uint8_t ** world, uint8_t width, uint8_t height) {
	fflush(stdout);
	uint8_t neighbours = calc_cell_neighbours(x, y, world, width, height);
	uint8_t alive = world[x][y];

	if (alive) {
		if (neighbours > 3 || neighbours < 2) {
			return 0; /* died by over-/underpopulation */
		}
		else {
			return 1; /* kept alive */
		}
	}
	else if (neighbours == 3) {
		return 1; /* born */
	}
	else {
		return 0; /* still dead */
	}
}

void calc_next_gen(uint8_t ** world, uint8_t ** next_gen, uint8_t width, uint8_t height) {
	uint8_t x, y;

	for (x = 0; x < width; x++) {
		for (y = 0; y < height; y++) {
			next_gen[x][y] = calc_next_cell_gen(x, y, world, width, height);
		}
	}

	/* copy world */
	for (x = 0; x < width; x++) {
		for (y = 0; y < height; y++) {
			world[x][y] = next_gen[x][y];
		}
	}
}

/* print world with colors and count of neighbours */
void print_world(uint8_t ** world, uint8_t width, uint8_t height) {
	uint8_t x, y;
	move(0, 0); /* reset cursor */

	/* cells */
	for (y = 0; y < height; y++) {
		for (x = 0; x < width; x++) {
			uint8_t neighbours = calc_cell_neighbours(x, y, world, width, height);

			if (neighbours > 1) attron(COLOR_PAIR(neighbours));
			addch((world[x][y]) ? '0' + neighbours : ' ');
			if (neighbours > 1) attroff(COLOR_PAIR(neighbours));
		}
	}
}

#ifdef ENABLE_CURSOR
void print_cursor(uint8_t ** world, struct cursor cur) {
	uint8_t color = (world[cur.x][cur.y]) ? 7 : 6;

	move(cur.y, cur.x);
	addch(CURSOR_CHAR | A_BLINK | A_BOLD | A_STANDOUT | COLOR_PAIR(color));
}
#endif

#ifdef ENABLE_HOTKEYS
#define MENU_WIDTH 25
void print_menu(uint8_t width, uint8_t height) {
	uint8_t startx, starty, mwdth_rel, posy = 0;

	mwdth_rel = (uint8_t) (MENU_WIDTH)/2+10;
	startx = (uint8_t) (width/2) - mwdth_rel;
	starty = (uint8_t) (height/2) - (uint8_t) (mwdth_rel/2);
	attron(COLOR_PAIR(1));
	for (int i = 0; i < MENU_WIDTH; i++) {
		mvprintw(starty, startx+i, "-");
		mvprintw(starty+(mwdth_rel*2/3), startx+i, "-");
		if (i % 2 == 0) {
			mvprintw(starty+posy+1, startx-1, "|");
			mvprintw(starty+posy+1, startx+MENU_WIDTH, "|");
			posy++;
		}
	}
	mvprintw(starty, startx-1, "+");
	mvprintw(starty, startx+MENU_WIDTH, "+");
	mvprintw(starty+posy+1, startx-1, "+");
	mvprintw(starty+posy+1, startx+MENU_WIDTH, "+");
	for (int i = startx; i < startx+MENU_WIDTH; i++) {
		for (int j = starty+1; j <= starty+posy; j++) {
			mvprintw(j, i, " ");
		}
	}
	mvprintw(starty+1, startx+1, "q   ~ Exit");
	mvprintw(starty+3, startx+1, "c   ~ Clear Screen");
#if defined(RANDOM_SPAWNS)
	mvprintw(starty+5, startx+1, "d   ~ Rnd Spawns [%d]", (rnd_spawns == 0 ? 0 : 1));
#endif
	mvprintw(starty+7, startx+1, "p   ~ Pause");
	mvprintw(starty+9, startx+1, "+/- ~ Change Framerate");
	mvprintw(starty+11, startx+1, "0-5 ~ Create Pattern");
	attroff(COLOR_PAIR(1));
}
#endif

/* set up ncurses screen */
WINDOW * init_screen() {
	WINDOW * win = initscr();
	noecho();
	timeout(0);
	keypad(win, 1);
	mousemask(BUTTON1_PRESSED, NULL);
	mouseinterval(200);
	curs_set(0);

	start_color();
	init_color(COLOR_CYAN, 500, 1000, 0);  /* redefine as orange */

	init_pair(1, COLOR_BLACK, COLOR_WHITE);
	init_pair(2, COLOR_WHITE, COLOR_BLACK);
	init_pair(3, COLOR_GREEN, COLOR_BLACK);
	init_pair(4, COLOR_YELLOW, COLOR_BLACK);
	init_pair(5, COLOR_CYAN, COLOR_BLACK);
	init_pair(6, COLOR_BLUE, COLOR_BLACK);
	init_pair(7, COLOR_MAGENTA, COLOR_BLACK);
	init_pair(8, COLOR_RED, COLOR_BLACK);

	return win;
}

uint8_t **init_world(WINDOW *win, uint8_t **worlds[2], uint8_t *width, uint8_t *height) {
	getmaxyx(win, *height, *width);
	for (int i = 0; i < 2; i++)
		worlds[i] = create_world(*width, *height);
	return (worlds[0]);
}

void free_all(uint8_t **worlds[2], uint8_t width, uint8_t height) {
	free_world(worlds[0], width, height);
	free_world(worlds[1], width, height);
}

// returns realloc'd && resized world
uint8_t **resized(WINDOW *win, uint8_t **worlds[2], uint8_t *width, uint8_t *height) {
	free_world(worlds[0], *width, *height);
	free_world(worlds[1], *width, *height);
	return (init_world(win, worlds, width, height));
}

int main(int argc, char * argv[]) {
	WINDOW * win = init_screen();
#ifdef ENABLE_CURSOR
	MEVENT event;
#endif

	/* predefined patterns */
	struct pattern patterns[] = {
		{3, 3, (uint8_t *) start},
		{3, 3, (uint8_t *) glider},
		{5, 4, (uint8_t *) segler},
		{3, 7, (uint8_t *) buffer},
		{8, 4, (uint8_t *) kreuz},
		{6, 6, (uint8_t *) ship}
	};
	struct cursor cur = {0, 0};
	struct config cfg;

	int generation = 0, input, framerate = 17;
	uint8_t width, height;
	uint8_t ** worlds[2], ** world;
#if defined(RANDOM_SPAWNS)
	int idle_gens = 0;
	srand(time(NULL));
#endif

	memset(&cfg, '\0', sizeof(struct config));
	/* initialize world */
	world = init_world(win, worlds, &width, &height);
	/* make the world real */
	inhabit_world(patterns[3], width/2, height/2, worlds[0], width, height);

	/* simulation loop */
	while(!cfg.quit) {
		if (!cfg.paused) {
			/* calc next generation */
			usleep(1 / (float) framerate * 1000000); /* sleep */
			calc_next_gen(world, worlds[++generation % 2], width, height);
			world = worlds[generation % 2]; /* new world */
		}

		/* handle events */
		switch (input = getch()) {
#ifdef ENABLE_HOTKEYS
			case '+': /* increase framerate */
				framerate++;
				break;

			case '-': /* decrease framerate */
				if (framerate > 1) framerate--;
				break;

			case 'q': /* quit */
				cfg.quit = 1;
				break;
#if defined(RANDOM_SPAWNS)
			case 'd': /* disable random spawn */
				rnd_spawns = ~rnd_spawns;
				break;
#endif
			case 'p': /* pause */
				cfg.paused ^= 1;
				break;

			case 'c': /* clean world */
				clean_world(world, width, height);
				generation = 0;
				break;
#endif
#if defined(ENABLE_HOTKEYS) || defined(ENABLE_CURSOR)
			case '0': /* insert pattern */
			case '1':
			case '2':
			case '3':
			case '4':
			case '5':
				inhabit_world(patterns[input - '0'], cur.x, cur.y, world, width, height);
				break;
#endif
#ifdef ENABLE_CURSOR
			case ' ': /* toggle cell at cursor position */
				world[cur.x][cur.y] = (world[cur.x][cur.y]) ? 0 : 1;
				break;

			case KEY_MOUSE: /* move cursor to mouse posititon */
				if (getmouse(&event) == OK && event.bstate & BUTTON1_PRESSED) {
					cur.x = event.x;
					cur.y = event.y;
					if (cur.x >= width) cur.x = width - 1;
					if (cur.y >= height) cur.y = height - 1;
					world[cur.x][cur.y] = (world[cur.x][cur.y]) ? 0 : 1;
				}
				break;

			case KEY_UP:
				if (cur.y > 0) {
					cur.y--;
				}
				break;

			case KEY_DOWN:
				if (cur.y < height-1) {
					cur.y++;
				}
				break;

			case KEY_LEFT:
				if (cur.x > 0) {
					cur.x--;
				}
				break;

			case KEY_RIGHT:
				if (cur.x < width-1) {
					cur.x++;
				}
				break;
			case KEY_RESIZE:
				world = resized(win, worlds, &width, &height);
				break;
#endif
		}

#if defined(RANDOM_SPAWNS)
		if (rnd_spawns) {
			/* spawn a new pattern at a random position, if nothing happens */
			idle_gens++;
			if (idle_gens >= RANDOM_SPAWNS && !cfg.paused)
			{
				idle_gens = 0;
				inhabit_world(patterns[rand() % (sizeof(patterns)/sizeof(patterns[0]))], rand() % (width - 1), rand() % (height - 1), world, width, height);
			}
		}
#endif

		/* update screen */
		print_world(world, width, height);
#ifdef ENABLE_CURSOR
		print_cursor(world, cur);
#endif
#ifdef ENABLE_STATUS
		attron(COLOR_PAIR(1));
		for (int i = 0; i < width; i++) mvprintw(0, width - i, " ");
		mvprintw(0, 0, "[generation:%4d] [cells:%3d] [fps:%2d] [width:%d] [height:%d] [cursor:%2d|%2d]", generation, calc_cell_count(world, width, height), framerate, width, height, cur.x, cur.y);
		if (cfg   .paused) mvprintw(0, width-6, "PAUSED");
		attroff(COLOR_PAIR(1));
#endif

#ifdef ENABLE_HOTKEYS
		if (cfg.paused) {
			print_menu(width, height);
			usleep(1);
		}
#endif
		refresh();
	}

	free_all(worlds, width, height);
	delwin(win);
	endwin(); /* exit ncurses mode */
	return (EXIT_SUCCESS);
}
