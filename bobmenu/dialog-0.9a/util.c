/*
 *  $Id: util.c,v 1.1.1.1 2001-05-17 18:46:10 mcopenha Exp $
 *
 *  util.c
 *
 *  AUTHOR: Savio Lam (lam836@cs.cuhk.hk)
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version 2
 *  of the License, or (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include "dialog.h"

#include <stdarg.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <time.h>

#ifdef NCURSES_VERSION
#ifdef HAVE_NCURSES_NCURSES_H
#include <ncurses/term.h>
#else
#include <term.h>
#endif
#endif

#ifndef O_BINARY
#define O_BINARY 0
#endif

#ifndef S_IRUSR
#define S_IRUSR 0400
#endif

#ifndef S_IWUSR
#define S_IWUSR 0200
#endif

#ifndef DIALOG_TMPDIR
#define DIALOG_TMPDIR NULL
#endif

#define LOCK_PERMITS (S_IRUSR | S_IWUSR)
#define LOCK_TIMEOUT 10		/* timeout for locking, in seconds */

/* globals */
FILE *pipe_fp;			/* copy of stdin, for reading a pipe */
DIALOG_VARS dialog_vars;
DIALOCK lock_refresh;
DIALOCK lock_tailbg_exit;
DIALOCK lock_tailbg_refreshed;
int defaultno = FALSE;
int is_tailbg = FALSE;
int screen_initialized = 0;
pid_t tailbg_lastpid = 0;
pid_t tailbg_nokill_lastpid = 0;
pid_t tailbg_nokill_pids[MAX_TAILBG];
pid_t tailbg_pids[MAX_TAILBG];
static FILE *my_output;		/* prefer to stdout, to support --stdout */

#ifdef HAVE_COLOR
/* use colors by default? */
bool use_colors = USE_COLORS;
/* shadow dialog boxes by default?
   Note that 'use_shadow' implies 'use_colors' */
bool use_shadow = USE_SHADOW;
#endif

const char *dialog_result;

/*
 * Attribute values, default is for mono display
 */
chtype attributes[] =
{
    A_NORMAL,			/* screen_attr */
    A_NORMAL,			/* shadow_attr */
    A_REVERSE,			/* dialog_attr */
    A_REVERSE,			/* title_attr */
    A_REVERSE,			/* border_attr */
    A_BOLD,			/* button_active_attr */
    A_DIM,			/* button_inactive_attr */
    A_UNDERLINE,		/* button_key_active_attr */
    A_UNDERLINE,		/* button_key_inactive_attr */
    A_NORMAL,			/* button_label_active_attr */
    A_NORMAL,			/* button_label_inactive_attr */
    A_REVERSE,			/* inputbox_attr */
    A_REVERSE,			/* inputbox_border_attr */
    A_REVERSE,			/* searchbox_attr */
    A_REVERSE,			/* searchbox_title_attr */
    A_REVERSE,			/* searchbox_border_attr */
    A_REVERSE,			/* position_indicator_attr */
    A_REVERSE,			/* menubox_attr */
    A_REVERSE,			/* menubox_border_attr */
    A_REVERSE,			/* item_attr */
    A_NORMAL,			/* item_selected_attr */
    A_REVERSE,			/* tag_attr */
    A_REVERSE,			/* tag_selected_attr */
    A_NORMAL,			/* tag_key_attr */
    A_BOLD,			/* tag_key_selected_attr */
    A_REVERSE,			/* check_attr */
    A_REVERSE,			/* check_selected_attr */
    A_REVERSE,			/* uarrow_attr */
    A_REVERSE,			/* darrow_attr */
    A_NORMAL			/* itemhelp_attr */
};

#ifdef HAVE_COLOR
#include "colors.h"

/*
 * Table of color values
 */
#define concat(a,b) a##b
#define DATA(name) { concat(name,_FG), concat(name,_BG), concat(name,_HL) }
int color_table[][3] =
{
    DATA(SCREEN),
    DATA(SHADOW),
    DATA(DIALOG),
    DATA(TITLE),
    DATA(BORDER),
    DATA(BUTTON_ACTIVE),
    DATA(BUTTON_INACTIVE),
    DATA(BUTTON_KEY_ACTIVE),
    DATA(BUTTON_KEY_INACTIVE),
    DATA(BUTTON_LABEL_ACTIVE),
    DATA(BUTTON_LABEL_INACTIVE),
    DATA(INPUTBOX),
    DATA(INPUTBOX_BORDER),
    DATA(SEARCHBOX),
    DATA(SEARCHBOX_TITLE),
    DATA(SEARCHBOX_BORDER),
    DATA(POSITION_INDICATOR),
    DATA(MENUBOX),
    DATA(MENUBOX_BORDER),
    DATA(ITEM),
    DATA(ITEM_SELECTED),
    DATA(TAG),
    DATA(TAG_SELECTED),
    DATA(TAG_KEY),
    DATA(TAG_KEY_SELECTED),
    DATA(CHECK),
    DATA(CHECK_SELECTED),
    DATA(UARROW),
    DATA(DARROW),
    DATA(ITEMHELP),
};				/* color_table */
#endif

/*
 * Display background title if it exists ...
 */
void
put_backtitle(void)
{
    int i;

    if (dialog_vars.backtitle != NULL) {
	wattrset(stdscr, screen_attr);
	(void) wmove(stdscr, 0, 1);
	(void) waddstr(stdscr, dialog_vars.backtitle);
	for (i = 0; i < COLS - (int) strlen(dialog_vars.backtitle); i++)
	    (void) waddch(stdscr, ' ');
	(void) wmove(stdscr, 1, 1);
	for (i = 0; i < COLS - 2; i++)
	    (void) waddch(stdscr, ACS_HLINE);
    }

    (void) wnoutrefresh(stdscr);
}

/*
 * Set window to attribute 'attr'.  There are more efficient ways to do this,
 * but will not work on older/buggy ncurses versions.
 */
void
attr_clear(WINDOW *win, int height, int width, chtype attr)
{
    int i, j;

    wattrset(win, attr);
    for (i = 0; i < height; i++) {
	(void) wmove(win, i, 0);
	for (j = 0; j < width; j++)
	    (void) waddch(win, ' ');
    }
    (void) touchwin(win);
}

void
dialog_clear(void)
{
    attr_clear(stdscr, LINES, COLS, screen_attr);
}

#define isprivate(s) ((s) != 0 && strstr(s, "\033[?") != 0)

#ifdef NCURSES_VERSION
static int
my_putc(int ch)
{
    return fputc(ch, my_output);
}
#endif

static int
open_terminal(char **result, int mode)
{
    const char *device = "/dev/tty";
    if (!isatty(fileno(stderr))
	|| (device = ttyname(fileno(stderr))) == 0) {
	if (!isatty(fileno(stdout))
	    || (device = ttyname(fileno(stdout))) == 0) {
	    if (!isatty(fileno(stdin))
		|| (device = ttyname(fileno(stdin))) == 0) {
		device = "/dev/tty";
	    }
	}
    }
    *result = strclone(device);
    return open(device, mode);
}

/*
 * Do some initialization for dialog
 */
void
init_dialog(void)
{
    int fd1, fd2;
    char *device = 0;

#ifdef HAVE_RC_FILE
    if (parse_rc() == -1)	/* Read the configuration file */
	exiterr("init_dialog: parse_rc");
#endif

    /*
     * Some widgets (such as gauge) may read from the standard input.
     * That would get in the way of curses' normal reading stdin for getch.
     * If we're not reading from a tty, see if we can open /dev/tty.
     */
    pipe_fp = stdin;
    if (!isatty(fileno(stdin))) {
	if ((fd1 = open_terminal(&device, O_RDONLY)) >= 0
	    && (fd2 = dup(fileno(stdin))) >= 0) {
	    pipe_fp = fdopen(fd2, "r");
	    *stdin = *freopen(device, "r", stdin);
	    if (fileno(stdin) != 0)	/* some functions may read fd #0 */
		(void) dup2(fileno(stdin), 0);
	}
    }

    /*
     * Try to open the output directly to /dev/tty so we can support the
     * command-line option "--stdout".  Otherwise it will get lost in the
     * normal output to the screen.
     */
    if ((fd1 = open_terminal(&device, O_WRONLY)) >= 0
	&& (my_output = fdopen(fd1, "w")) != 0) {
	if (newterm(NULL, my_output, stdin) == 0) {
	    exiterr("cannot initialize curses");
	}
    } else {
	(void) initscr();
    }
#ifdef NCURSES_VERSION
    /*
     * Cancel xterm's alternate-screen mode.
     */
    if (key_mouse != 0		/* xterm and kindred */
	&& isprivate(enter_ca_mode)
	&& isprivate(exit_ca_mode)) {
	(void) tputs(exit_ca_mode, 0, my_putc);
	(void) tputs(clear_screen, 0, my_putc);
    }
#endif
    (void) keypad(stdscr, TRUE);
    (void) cbreak();
    (void) noecho();
    mouse_open();
    screen_initialized = 1;

#ifdef HAVE_COLOR
    if (use_colors || use_shadow)	/* Set up colors */
	color_setup();
#endif

    /* Set screen to screen attribute */
    dialog_clear();
}

#ifdef HAVE_COLOR
/*
 * Setup for color display
 */
void
color_setup(void)
{
    int i;

    if (has_colors()) {		/* Terminal supports color? */
	(void) start_color();

	/* Initialize color pairs */
	for (i = 0; i < ATTRIBUTE_COUNT; i++)
	    (void) init_pair(i + 1, color_table[i][0], color_table[i][1]);

	/* Setup color attributes */
	for (i = 0; i < ATTRIBUTE_COUNT; i++)
	    attributes[i] = C_ATTR(color_table[i][2], i + 1);
    }
}
#endif

/*
 * End using dialog functions.
 */
void
end_dialog(void)
{
    if (screen_initialized) {
	screen_initialized = 0;
	mouse_close();
	(void) endwin();
	(void) fflush(stdout);
    }
}

static int
centered(int width, const char *string)
{
    int left = (width - strlen(string)) / 2 - 1;
    if (left < 0)
	left = 0;
    return left;
}

/*
 * Print one line of the prompt in the window within the limits of the
 * specified right margin.  The line will end on a word boundary and a pointer
 * to the start of the next line is returned, or a NULL pointer if the end of
 * *prompt is reached.
 */
static const char *
print_line(WINDOW *win, const char *prompt, int rm, int *x)
{
    int cur_x = 2;
    const char *wrap_ptr = prompt;
    const char *p = prompt + 1;
    int i = 0;

    *x = 1;

    /*
     * Set *p to the end of the line or the right margin (rm), whichever is
     * less, and set *wrap_ptr to the end of the last word in the line.
     */
    while (*p != '\n' && *p != '\0' && cur_x < rm) {
	if (*p == ' ' && *(p - 1) != ' ') {
	    wrap_ptr = p;
	    *x = cur_x;
	}
	p++;
	cur_x++;
    }

    /*
     * If the line doesn't reach the right margin in the middle of a word, then
     * we don't have to wrap it at a the end of the previous word.
     */
    if (*p == '\n' || *p == ' ' || *p == '\0') {
	while (&wrap_ptr[++i] < p) ;
	while (wrap_ptr[i - 1] == ' ')
	    i--;
	wrap_ptr += i;
	*x += i;
    }

    /*
     * If the line has no spaces, then wrap it anyway at the right margin
     */
    else if (*x == 1 && cur_x == rm) {
	*x = rm;
	wrap_ptr = p;
    }

    /*
     * Print the line if we have a window pointer.  Otherwise this routine
     * is just being called for sizing the window.
     */
    if (win) {
	p = prompt;
	while (p < wrap_ptr) {
	    (void) waddch(win, CharOf(*p++));
	}
    }

    /* *x tells the calling function how long the line was */
    if (*x == 1)
	*x = rm;

    /* Find the start of the next line and return a pointer to it */
    p = wrap_ptr;
    while (*p == ' ')
	p++;
    if (*p == '\n')
	p++;
    return (p);
}

static void
justify_text(WINDOW *win,
	     const char *prompt,
	     int limit_y,
	     int limit_x,
	     int *high, int *wide)
{
    int x = 2;
    int y = 1;
    int max_x = 2;
    int lm = 2;			/* left margin */
    int rm = limit_x;		/* right margin */
    int bm = limit_y;		/* bottom margin */

    if (win) {
	rm -= 4;
	bm -= 2;
    }

    while (y <= bm && *prompt) {
	x = lm;

	if (*prompt == '\n') {
	    while (*prompt == '\n' && y < bm) {
		if (*(prompt + 1) != '\0') {
		    ++y;
		    if (win != 0)
			(void) wmove(win, y, lm);
		}
		prompt++;
	    }
	} else if (win != 0)
	    (void) wmove(win, y, lm);

	if (*prompt)
	    prompt = print_line(win, prompt, rm, &x);
	if (*prompt) {
	    ++y;
	    if (win != 0)
		(void) wmove(win, y, lm);
	}
	max_x = MAX(max_x, x);
    }

    /* Set the final height and width for the calling function */
    if (high != 0)
	*high = y;
    if (wide != 0)
	*wide = max_x;
}

/*
 * Print a string of text in a window, automatically wrap around to the
 * next line if the string is too long to fit on one line. Note that the
 * string may contain embedded newlines.
 */
void
print_autowrap(WINDOW *win, const char *prompt, int height, int width)
{
    justify_text(win, prompt,
		 height,
		 width,
		 (int *) 0, (int *) 0);
}

/*
 * Calculate the window size for preformatted text.  This will calculate box
 * dimensions that are at or close to the specified aspect ratio for the prompt
 * string with all spaces and newlines preserved and additional newlines added
 * as necessary.
 */
static void
auto_size_preformated(char *prompt, int *height, int *width)
{
    int high, wide;
    float car;			/* Calculated Aspect Ratio */
    float diff;
    int max_y = SLINES - 1;
    int max_x = SCOLS - 2;
    int max_width = max_x;
    int ar = dialog_vars.aspect_ratio;

    /* Get the initial dimensions */
    justify_text((WINDOW *) 0, prompt, max_y, max_x, &high, &wide);
    car = (float) (wide / high);

    /*
     * If the aspect ratio is greater than it should be, then decrease the
     * width proportionately.
     */
    if (car > ar) {
	diff = car / (float) ar;
	max_x = wide / diff + 4;
	justify_text((WINDOW *) 0, prompt, max_y, max_x, &high, &wide);
	car = (float) wide / high;
    }

    /*
     * If the aspect ratio is to small after decreasing the width, then
     * incrementally increase the width until the aspect ratio is equal to or
     * greater than the specified aspect ratio.
     */
    while (car < ar && max_x < max_width) {
	max_x += 4;
	justify_text((WINDOW *) 0, prompt, max_y, max_x, &high, &wide);
	car = (float) (wide / high);
    }

    *height = high;
    *width = wide;
}

/*
 * if (height or width == -1) Maximize()
 * if (height or width == 0), justify and return actual limits.
 */
static void
real_auto_size(const char *title,
	       char *prompt,
	       int *height, int *width,
	       int boxlines, int mincols)
{
    int x = (dialog_vars.begin_set ? dialog_vars.begin_x : 2);
    int y = (dialog_vars.begin_set ? dialog_vars.begin_y : 1);
    int title_length = title ? strlen(title) : 0;
    int nc = 4;
    int high;
    int wide;
    int save_high = *height;
    int save_wide = *width;

    if (prompt == 0) {
	if (*height == 0)
	    *height = -1;
	if (*width == 0)
	    *width = -1;
    }

    if (*height > 0) {
	high = *height;
    } else {
	high = SLINES - y;
    }

    if (*width > 0) {
	wide = *width;
    } else if (prompt != 0) {
	wide = MAX(title_length, mincols);
	if (strchr(prompt, '\n') == 0) {
	    double val = dialog_vars.aspect_ratio * strlen(prompt);
	    int tmp = sqrt(val);
	    wide = MAX(wide, tmp);
	    justify_text((WINDOW *) 0, prompt, high, wide, height, width);
	} else {
	    auto_size_preformated(prompt, height, width);
	}
    } else {
	wide = SCOLS - x;
	justify_text((WINDOW *) 0, prompt, high, wide, height, width);
    }

    if (*width < title_length) {
	justify_text((WINDOW *) 0, prompt, high, title_length, height, width);
	*width = title_length;
    }

    if (*width < mincols && save_wide == 0)
	*width = mincols;
    if (prompt != 0) {
	*width += nc;
	*height += boxlines + 2;
    }
    if (save_high > 0)
	*height = save_high;
    if (save_wide > 0)
	*width = save_wide;
}

/* End of real_auto_size() */

void
auto_size(const char *title, char *prompt, int *height, int *width, int
	  boxlines, int mincols)
{
    real_auto_size(title, prompt, height, width, boxlines, mincols);

    if (*width > SCOLS) {
	(*height)++;
	*width = SCOLS;
    }

    if (*height > SLINES)
	*height = SLINES;
}

/*
 * if (height or width == -1) Maximize()
 * if (height or width == 0)
 *    height=MIN(SLINES, num.lines in fd+n);
 *    width=MIN(SCOLS, MAX(longer line+n, mincols));
 */
void
auto_sizefile(const char *title, const char *file, int *height, int *width, int
	      boxlines, int mincols)
{
    int count = 0, len = title ? strlen(title) : 0, nc = 4, numlines = 2;
    long offset;
    char ch;
    FILE *fd;

    /* Open input file for reading */
    if ((fd = fopen(file, "rb")) == NULL)
	exiterr("auto_sizefile: Cannot open input file %s", file);

    if ((*height == -1) || (*width == -1)) {
	*height = SLINES - (dialog_vars.begin_set ? dialog_vars.begin_y : 0);
	*width = SCOLS - (dialog_vars.begin_set ? dialog_vars.begin_x : 0);
    }
    if ((*height != 0) && (*width != 0)) {
	(void) fclose(fd);
	return;
    }

    while (!feof(fd)) {
	offset = 0;
	while (((ch = getc(fd)) != '\n') && !feof(fd))
	    if ((ch == TAB) && (dialog_vars.tab_correct))
		offset += dialog_vars.tab_len - (offset % dialog_vars.tab_len);
	    else
		offset++;

	if (offset > len)
	    len = offset;

	count++;
    }

    /* now 'count' has the number of lines of fd and 'len' the max lenght */

    *height = MIN(SLINES, count + numlines + boxlines);
    *width = MIN(SCOLS, MAX((len + nc), mincols));
    /* here width and height can be maximized if > SCOLS|SLINES because
       textbox-like widgets don't put all <file> on the screen.
       Msgbox-like widget instead have to put all <text> correctly. */

    (void) fclose(fd);
}

/* End of auto_sizefile() */

/*
 * Draw a rectangular box with line drawing characters
 */
void
draw_box(WINDOW *win, int y, int x, int height, int width,
	 chtype boxchar, chtype borderchar)
{
    int i, j;
    chtype save = getattrs(win);

    wattrset(win, 0);
    for (i = 0; i < height; i++) {
	(void) wmove(win, y + i, x);
	for (j = 0; j < width; j++)
	    if (!i && !j)
		(void) waddch(win, borderchar | ACS_ULCORNER);
	    else if (i == height - 1 && !j)
		(void) waddch(win, borderchar | ACS_LLCORNER);
	    else if (!i && j == width - 1)
		(void) waddch(win, boxchar | ACS_URCORNER);
	    else if (i == height - 1 && j == width - 1)
		(void) waddch(win, boxchar | ACS_LRCORNER);
	    else if (!i)
		(void) waddch(win, borderchar | ACS_HLINE);
	    else if (i == height - 1)
		(void) waddch(win, boxchar | ACS_HLINE);
	    else if (!j)
		(void) waddch(win, borderchar | ACS_VLINE);
	    else if (j == width - 1)
		(void) waddch(win, boxchar | ACS_VLINE);
	    else
		(void) waddch(win, boxchar | ' ');
    }
    wattrset(win, save);
}

#ifdef HAVE_COLOR
/*
 * Draw shadows along the right and bottom edge to give a more 3D look
 * to the boxes
 */
void
draw_shadow(WINDOW *win, int y, int x, int height, int width)
{
    int i;

    if (has_colors()) {		/* Whether terminal supports color? */
	wattrset(win, shadow_attr);
	(void) wmove(win, y + height, x + 2);
	for (i = 0; i < width; i++)
	    (void) waddch(win, CharOf(winch(win)));
	for (i = y + 1; i < y + height + 1; i++) {
	    (void) wmove(win, i, x + width);
	    (void) waddch(win, CharOf(winch(win)));
	    (void) waddch(win, CharOf(winch(win)));
	}
	(void) wnoutrefresh(win);
    }
}
#endif

/*
 * make_tmpfile()
 */
int
make_lock_filename(DIALOCK * dialock, const char *prefix)
{
    static int first = 1;
    char *file = tempnam(DIALOG_TMPDIR, prefix);

    if (first) {
	first = 0;
	srand(time((time_t *) 0));
    }
    if (file == NULL) {
	end_dialog();
	fprintf(stderr, "\nCan't make tempfile (%s)...\n", prefix);
	return -1;
    }
    dialock->filename = file;
    dialock->identifier = rand();
    return 0;
}
/* End of make_tmpfile() */

/* private function, used by wrefresh_lock() and wrefresh_lock_tailbg() */
static void
wrefresh_lock_sub(WINDOW *win)
{
    create_lock(&lock_refresh);
    beeping();
    (void) wrefresh(win);
    remove_lock(&lock_refresh);
}

/*
 * Only one process at a time should call wrefresh()
 */

/* called by all normal widget */
void
wrefresh_lock(WINDOW *win)
{
    wrefresh_lock_sub(win);

    if (exist_lock(&lock_tailbg_refreshed))
	remove_lock(&lock_tailbg_refreshed);
}

/* If a tailboxbg was wrefreshed, wrefresh again the principal (normal) widget.
 *
 * The important thing here was to put cursor in the principal widget,
 * but with touchwin(win) the principal widget is also rewritten. This
 * is needed if a tailboxbg has overwritten this widget.
 */
void
ctl_idlemsg(WINDOW *win)
{
    if (exist_lock(&lock_tailbg_refreshed))
	wrefresh_lock(win);

    /* if a tailboxbg exited, quit */
    if (exist_lock(&lock_tailbg_exit)) {
	remove_lock(&lock_tailbg_exit);
	exiterr("ctl_idlemsg: quit");
    }
}

/* call by tailboxbg, the resident widget */
void
wrefresh_lock_tailbg(WINDOW *win)
{
    if (!exist_lock(&lock_tailbg_refreshed))
	create_lock(&lock_tailbg_refreshed);
    wrefresh_lock_sub(win);
}

/* End of wrefresh functions */

/*
 * To work with lock files, here are some functions
 */
int
exist_lock(DIALOCK * dialock)
{
    int fd;
    int code = 0;
    int check;
    struct stat sb;

    if (dialock->filename != 0
	&& (fd = open(dialock->filename, O_RDONLY | O_BINARY)) >= 0) {
	if (fstat(fd, &sb) >= 0
	    && sb.st_uid == geteuid()
	    && sb.st_gid == getegid()
	    && (sb.st_mode & S_IFMT) == S_IFREG
	    && (sb.st_mode & ~S_IFMT) == LOCK_PERMITS
	    && read(fd, &check, sizeof(check)) == sizeof(check)
	    && dialock->identifier == check) {
	    code = 1;
	}
	(void) close(fd);
    }
    return code;
}

void
create_lock(DIALOCK * dialock)
{
    int fd, i;

    i = 0;
    while ((fd = open(dialock->filename,
		      O_WRONLY | O_CREAT | O_EXCL | O_BINARY,
		      LOCK_PERMITS)) == -1) {
	napms(1000);
	if (++i == LOCK_TIMEOUT) {
	    /* can't use exiterr here, since it attempts to use a lock
	     * which could cause an endless loop of lock attempts :(
	     */
	    end_dialog();

	    fprintf(stderr, "Unable to create lock %s", dialock->filename);

	    if (!is_tailbg)
		killall_bg();

	    exit(-1);
	}
    }

    ftruncate(fd, 0);
    write(fd, &(dialock->identifier), sizeof(dialock->identifier));
    close(fd);
}

void
remove_lock(DIALOCK * dialock)
{
    (void) remove(dialock->filename);
}

/* End of functions to work with lock files */

/* killall_bg: kill all tailboxbg, if any */
void
killall_bg(void)
{
    int i;
    for (i = 0; i < tailbg_lastpid; i++)
	(void) kill(tailbg_pids[i], SIGTERM);
}

static int
is_nokill(int pid)
{				/* private func for quitall_bg() */
    int i;
    for (i = 0; i < tailbg_nokill_lastpid; i++)
	if (tailbg_nokill_pids[i] == pid)
	    return 1;
    return 0;
}

/* quitall_bg: kill all tailboxbg without --no-kill flag */
int
quitall_bg(void)
{
    int i;
    int count = 0;

    for (i = 0; i < tailbg_lastpid; i++)
	if (is_nokill(tailbg_pids[i]))
	    count++;
	else
	    (void) kill(tailbg_pids[i], SIGTERM);
    return count;
}

/* exiterr quit program killing all tailbg */
void
exiterr(const char *fmt,...)
{
    va_list ap;

    end_dialog();

    (void) fputc('\n', stderr);
    va_start(ap, fmt);
    (void) vfprintf(stderr, fmt, ap);
    va_end(ap);
    (void) fputc('\n', stderr);

    if (is_tailbg)		/* this is a child process */
	create_lock(&lock_tailbg_exit);
    else
	killall_bg();

    (void) fflush(stderr);
    (void) fflush(stdout);
    exit(-1);
}

void
beeping(void)
{
    if (dialog_vars.beep_signal) {
	(void) beep();
	dialog_vars.beep_signal = 0;
    }
}

void
print_size(int height, int width)
{
    if (dialog_vars.print_siz)
	fprintf(dialog_vars.output, "Size: %d, %d\n", height, width);
}

void
ctl_size(int height, int width)
{
    if (dialog_vars.size_err) {
	if ((width > COLS) || (height > LINES)) {
	    exiterr("Window too big. (Height, width) = (%d, %d). Max allowed (%d, %d).",
		    height, width, LINES, COLS);
	}
#ifdef HAVE_COLOR
	else if ((use_shadow) && ((width > SCOLS || height > SLINES))) {
	    exiterr("Window+Shadow too big. (Height, width) = (%d, %d). Max allowed (%d, %d).",
		    height, width, SLINES, SCOLS);
	}
#endif
    }
}

void
tab_correct_str(char *prompt)
{
    char *ptr;

    if (!dialog_vars.tab_correct)
	return;

    while ((ptr = strchr(prompt, TAB)) != NULL) {
	*ptr = ' ';
	prompt = ptr;
    }
}

void
calc_listh(int *height, int *list_height, int item_no)
{
    /* calculate new height and list_height */
    int rows = SLINES - (dialog_vars.begin_set ? dialog_vars.begin_y : 0);
    if (rows - (*height) > 0) {
	if (rows - (*height) > item_no)
	    *list_height = item_no;
	else
	    *list_height = rows - (*height);
    }
    (*height) += (*list_height);
}

int
calc_listw(int item_no, char **items, int group)
{
    int n, i, len1 = 0, len2 = 0;
    for (i = 0; i < (item_no * group); i += group) {
	if ((n = strlen(items[i])) > len1)
	    len1 = n;
	if ((n = strlen(items[i + 1])) > len2)
	    len2 = n;
    }
    return len1 + len2;
}

char *
strclone(const char *cprompt)
{
    char *prompt = (char *) malloc(strlen(cprompt) + 1);
    strcpy(prompt, cprompt);
    return prompt;
}

int
box_x_ordinate(int width)
{
    int x;

    if (dialog_vars.begin_set == 1) {
	x = dialog_vars.begin_x;
    } else {
	/* center dialog box on screen unless --begin-set */
	x = (SCOLS - width) / 2;
    }
    return x;
}

int
box_y_ordinate(int height)
{
    int y;

    if (dialog_vars.begin_set == 1) {
	y = dialog_vars.begin_y;
    } else {
	/* center dialog box on screen unless --begin-set */
	y = (SLINES - height) / 2;
    }
    return y;
}

void
draw_title(WINDOW *win, const char *title)
{
    if (title != NULL) {
	wattrset(win, title_attr);
	(void) mvwprintw(win, 0, centered(getmaxx(win), title), " %s ", title);
    }
}

void
draw_bottom_box(WINDOW *win)
{
    int width = getmaxx(win);
    int height = getmaxy(win);
    int i;

    wattrset(win, border_attr);
    (void) wmove(win, height - 3, 0);
    (void) waddch(win, ACS_LTEE);
    for (i = 0; i < width - 2; i++)
	(void) waddch(win, ACS_HLINE);
    wattrset(win, dialog_attr);
    (void) waddch(win, ACS_RTEE);
    (void) wmove(win, height - 2, 1);
    for (i = 0; i < width - 2; i++)
	(void) waddch(win, ' ');
}

WINDOW *
new_window(int height, int width, int y, int x)
{
    WINDOW *win;

#ifdef HAVE_COLOR
    if (use_shadow)
	draw_shadow(stdscr, y, x, height, width);
#endif
    if ((win = newwin(height, width, y, x)) == 0) {
	exiterr("Can't make new window at (%d,%d), size (%d,%d).\n",
		y, x, height, width);
    }

    (void) keypad(win, TRUE);
    return win;
}

WINDOW *
sub_window(WINDOW *parent, int height, int width, int y, int x)
{
    WINDOW *win;

    if ((win = subwin(parent, height, width, y, x)) == 0) {
	exiterr("Can't make sub-window at (%d,%d), size (%d,%d).\n",
		y, x, height, width);
    }

    (void) keypad(win, TRUE);
    return win;
}

int
dlg_default_item(char **items, int llen)
{
    if (dialog_vars.default_item != 0) {
	int count = 0;
	while (*items != 0) {
	    if (!strcmp(dialog_vars.default_item, *items))
		return count;
	    items += llen;
	    count++;
	}
    }
    return 0;
}

int
dlg_getc(WINDOW *win)
{
    for (;;) {
	int ch = wgetch(win);
	switch (ch) {
	case CHR_REPAINT:
	    (void) touchwin(win);
	    (void) wrefresh(curscr);
	    break;
	default:
	    return ch;
	}
    }
}

/*
 * Draw the string for item_help
 */
void
dlg_item_help(char *txt)
{
    if (dialog_vars.item_help && txt) {
	wattrset(stdscr, itemhelp_attr);
	(void) wmove(stdscr, LINES - 1, 0);
	(void) wclrtoeol(stdscr);
	(void) wprintw(stdscr, " %.*s", COLS - 2, txt);
	(void) wnoutrefresh(stdscr);
    }
}

#ifndef HAVE_STRCASECMP
int
dlg_strcmp(char *a, char *b)
{
    int ac, bc, cmp;

    for (;;) {
	ac = *a++ & 0xff;
	bc = *b++ & 0xff;
	if (isalpha(ac) && islower(ac))
	    ac = _toupper(ac);
	if (isalpha(bc) && islower(bc))
	    bc = _toupper(bc);
	cmp = ac - bc;
	if (ac == 0 || bc == 0 || cmp != 0)
	    break;
    }
    return cmp;
}
#endif

/*
 * Change embedded "\n" substrings to '\n' characters and tabs to single
 * spaces.  If there are no "\n"s, it will strip all extra spaces, for
 * justification.  If it has "\n"'s, it will preserve extra spaces.  If cr_wrap
 * is set, it will preserve '\n's.
 */
void
dlg_trim_string(char *s)
{
    char *p1;
    char *p = s;
    int has_newlines = 0;

    if (strstr(s, "\\n"))
	has_newlines = 1;

    while (*p != '\0') {
	if (*p == '\t')
	    *p = ' ';

	if (*p == '\\' && *(p + 1) == 'n') {
	    *s++ = '\n';
	    p += 2;
	    p1 = p;
	    /*
	     * Handle end of lines intelligently.  If '\n' follows "\n" then
	     * ignore the '\n'.  This eliminates the need escape the '\n'
	     * character (no need to use "\n\").
	     */
	    while (*p1 == ' ')
		p1++;
	    if (*p1 == '\n')
		p = p1 + 1;
	} else {
	    if (has_newlines) {	/* If prompt contains "\n" strings */
		if (*p == '\n') {
		    if (dialog_vars.cr_wrap)
			*s++ = *p++;
		    else {
			/* Replace the '\n' with a space if cr_wrap is not set */
			if (*(s - 1) != ' ')
			    *s++ = ' ';
			p++;
		    }
		} else		/* If *p != '\n' */
		    *s++ = *p++;
	    } else {		/* If there are no "\n" strings */
		if (*p == ' ') {
		    if (*(s - 1) != ' ') {
			*s++ = ' ';
			p++;
		    } else
			p++;
		} else if (*p == '\n') {
		    if (dialog_vars.cr_wrap)
			*s++ = *p++;
		    else if (*(s - 1) != ' ') {
			/* Strip '\n's if cr_wrap is not set. */
			*s++ = ' ';
			p++;
		    } else
			p++;
		} else
		    *s++ = *p++;
	    }
	}
    }

    *s = '\0';
}

void
dlg_set_focus(WINDOW *parent, WINDOW *win)
{
    if (win != 0) {
	(void) wmove(parent,
		     getpary(win) + getcury(win),
		     getparx(win) + getcurx(win));
	(void) wnoutrefresh(win);
	(void) doupdate();
    }
}