#define _POSIX_C_SOURCE 200809L

#include <sys/ioctl.h>
#include <sys/stat.h>

#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

/* Toggle keys in habit order; q is reserved for quit. */
static const char keys[] = "abcdefghijklmnoprstuvwxyz";
#define NKEYS (sizeof(keys) - 1)

/* TAU-CETI palette, 24-bit SGR. Accents: ACID = done, WARN = lapse/unsaved. */
#define BASE	"\x1b[0;38;2;231;236;232;48;2;10;13;16m"	/* FG on BG */
#define FG	"\x1b[38;2;231;236;232m"
#define DIM	"\x1b[38;2;139;150;155m"
#define LINE	"\x1b[38;2;86;99;107m"
#define ACID	"\x1b[38;2;201;255;61m"
#define WARN	"\x1b[38;2;255;61;90m"
#define BOLD	"\x1b[1m"
#define NOBOLD	"\x1b[22m"
/* Badges: dark text on a flat accent. */
#define B_ACID	"\x1b[1;38;2;10;13;16;48;2;201;255;61m"
#define B_WARN	"\x1b[1;38;2;10;13;16;48;2;255;61;90m"
#define B_DIM	"\x1b[1;38;2;10;13;16;48;2;139;150;155m"

#define MAXW	64	/* table never wider than this */
#define FIXW	18	/* table width minus the name column */
#define MINNW	5	/* name column fits at least "HABIT" */

static struct termios orig;
static int raw;

/* The day file, line by line, newlines included: written back verbatim. */
static char **lines;
static size_t *lens;
static size_t nlines;
/* First char of every line as last saved: toggling back is not a change. */
static char *saved;

static void
raw_off(void)
{
	if (raw) {
		/* Reset colors, show cursor, leave the alternate screen. */
		fputs("\x1b[0m\x1b[?25h\x1b[?1049l", stdout);
		fflush(stdout);
		tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig);
		raw = 0;
	}
}

static void
die(const char *fmt, ...)
{
	va_list ap;

	raw_off();
	fputs("habits: ", stderr);
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	fputc('\n', stderr);
	exit(EXIT_FAILURE);
}

static void
raw_on(void)
{
	struct termios t;

	if (tcgetattr(STDIN_FILENO, &orig) == -1)
		die("tcgetattr: %s", strerror(errno));

	t = orig;
	t.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
	t.c_oflag &= ~OPOST;
	t.c_cflag |= CS8;
	t.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
	t.c_cc[VMIN] = 1;
	t.c_cc[VTIME] = 0;
	if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &t) == -1)
		die("tcsetattr: %s", strerror(errno));
	raw = 1;
	atexit(raw_off);
	fputs("\x1b[?1049h\x1b[?25l", stdout);	/* alt screen, hide cursor */
}

static void
addline(char *s, size_t len)
{
	char **p;
	size_t *q;

	if ((p = realloc(lines, (nlines + 1) * sizeof(*lines))) == NULL)
		die("out of memory");
	lines = p;
	if ((q = realloc(lens, (nlines + 1) * sizeof(*lens))) == NULL)
		die("out of memory");
	lens = q;
	lines[nlines] = s;
	lens[nlines++] = len;
}

/* Appends the file's lines; -1 with errno set if it cannot be opened. */
static int
load(const char *path)
{
	FILE *f;
	char *buf = NULL;
	size_t cap = 0;
	ssize_t len;

	if ((f = fopen(path, "r")) == NULL)
		return -1;
	while ((len = getline(&buf, &cap, f)) != -1) {
		addline(buf, (size_t)len);
		buf = NULL;
		cap = 0;
	}
	free(buf);
	if (ferror(f))
		die("read %s: %s", path, strerror(errno));
	fclose(f);
	return 0;
}

static void
snapshot(void)
{
	size_t i;

	if ((saved = realloc(saved, nlines ? nlines : 1)) == NULL)
		die("out of memory");
	for (i = 0; i < nlines; i++)
		saved[i] = lines[i][0];
}

static int
dirty(void)
{
	size_t i;

	for (i = 0; i < nlines; i++)
		if (lines[i][0] != saved[i])
			return 1;
	return 0;
}

/*
 * Write a unique temp file, sync it and rename over the day, so a crash
 * leaves either the old file or the new one, never half of it.
 */
static void
save(const char *path)
{
	char tmp[PATH_MAX];
	FILE *f;
	size_t i;
	int fd;

	if ((size_t)snprintf(tmp, sizeof(tmp), "%s.XXXXXX", path) >= sizeof(tmp))
		die("path too long: %s", path);
	if ((fd = mkstemp(tmp)) == -1)
		die("mkstemp %s: %s", tmp, strerror(errno));
	if (fchmod(fd, 0644) == -1 || (f = fdopen(fd, "w")) == NULL) {
		unlink(tmp);
		die("open %s: %s", tmp, strerror(errno));
	}
	for (i = 0; i < nlines; i++)
		fwrite(lines[i], 1, lens[i], f);
	if (fflush(f) == EOF || ferror(f) || fsync(fd) == -1 ||
	    fclose(f) == EOF) {
		unlink(tmp);
		die("write %s: %s", tmp, strerror(errno));
	}
	if (rename(tmp, path) == -1) {
		unlink(tmp);
		die("rename %s: %s", tmp, strerror(errno));
	}
	snapshot();
}

/* "- x" good, "+ x" good done, "! x" bad, "* x" bad done (a lapse). */
static int
ishabit(const char *l)
{
	return l[0] != '\0' && strchr("-+!*", l[0]) != NULL && l[1] == ' ';
}

static int
isbad(const char *l)
{
	return l[0] == '!' || l[0] == '*';
}

/*
 * Line index of the k-th habit in display order: good habits first, then
 * bad, each in file order. Keys follow this order. nlines if none.
 */
static size_t
nth(size_t k)
{
	size_t i;
	int bad;

	for (bad = 0; bad < 2; bad++)
		for (i = 0; i < nlines; i++)
			if (ishabit(lines[i]) && isbad(lines[i]) == bad && k-- == 0)
				return i;
	return nlines;
}

static int
toggle(size_t k)
{
	size_t i;
	char *l;

	if ((i = nth(k)) == nlines)
		return 0;
	l = lines[i];
	switch (l[0]) {
	case '-': l[0] = '+'; break;
	case '+': l[0] = '-'; break;
	case '!': l[0] = '*'; break;
	case '*': l[0] = '!'; break;
	}
	return 1;
}

/* Code points in s[0..n): enough for Cyrillic, wide glyphs will misalign. */
static size_t
ulen(const char *s, size_t n)
{
	size_t i, w = 0;

	for (i = 0; i < n; i++)
		if (((unsigned char)s[i] & 0xC0) != 0x80)
			w++;
	return w;
}

/* Byte length of the first max code points of s[0..n). */
static size_t
ucut(const char *s, size_t n, size_t max)
{
	size_t i;

	for (i = 0; i < n; i++)
		if (((unsigned char)s[i] & 0xC0) != 0x80 && max-- == 0)
			break;
	return i;
}

static void
pad(size_t n)
{
	while (n-- > 0)
		putchar(' ');
}

static void
hline(const char *l, const char *m, const char *r, size_t nw)
{
	size_t i;

	fputs(LINE, stdout);
	fputs(l, stdout);
	fputs("───", stdout);
	fputs(m, stdout);
	for (i = 0; i < nw + 2; i++)
		fputs("─", stdout);
	fputs(m, stdout);
	fputs("─────────", stdout);
	fputs(r, stdout);
	fputs("\r\n", stdout);
}

static void
row(char key, const char *l, size_t nw)
{
	const char *name = l + 2;
	size_t n = strcspn(name, "\r\n"), w = ulen(name, n);
	const char *state;

	switch (l[0]) {
	case '+': state = ACID "■ DONE "; break;
	case '*': state = WARN "■ LAPSE"; break;
	case '-': state = LINE "· TODO "; break;
	default:  state = LINE "· ---- "; break;
	}

	printf(LINE "│" FG BOLD " %c " NOBOLD LINE "│ " FG, key);
	if (w > nw) {
		fwrite(name, 1, ucut(name, n, nw - 1), stdout);
		fputs("…", stdout);
	} else {
		fwrite(name, 1, n, stdout);
		pad(nw - w);
	}
	printf(" " LINE "│ %s " LINE "│\r\n", state);
}

static void
render(const char *date, int saving)
{
	struct winsize ws;
	size_t i, k, w = 80, nw;
	int d = dirty();
	const char *bc, *bt;

	if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) != -1 && ws.ws_col > 0)
		w = ws.ws_col;
	if (w > MAXW)
		w = MAXW;
	nw = w > FIXW + MINNW ? w - FIXW : MINNW;

	/* OPOST is off: newlines need an explicit \r. */
	fputs(BASE "\x1b[2J\x1b[H\r\n", stdout);

	if (saving) {
		bc = B_DIM;
		bt = " SAVING ";
	} else if (d) {
		bc = B_WARN;
		bt = " UNSAVED ";
	} else {
		bc = B_ACID;
		bt = " SAVED ";
	}
	/* "HABITS // YYYY-MM-DD" is 20 columns; badge flush with the table. */
	printf(FG BOLD "HABITS" NOBOLD LINE " // " DIM "%s", date);
	pad(nw + FIXW > 20 + strlen(bt) ? nw + FIXW - 20 - strlen(bt) : 1);
	printf("%s%s" BASE "\r\n\r\n", bc, bt);

	hline("┌", "┬", "┐", nw);
	printf(LINE "│ " DIM "K" LINE " │ " DIM "HABIT");
	pad(nw - 5);
	fputs(LINE " │ " DIM "STATE   " LINE "│\r\n", stdout);
	hline("├", "┼", "┤", nw);
	for (k = 0; (i = nth(k)) < nlines; k++) {
		if (k > 0 && isbad(lines[i]) && !isbad(lines[nth(k - 1)]))
			hline("├", "┼", "┤", nw);
		row(k < NKEYS ? keys[k] : ' ', lines[i], nw);
	}
	hline("└", "┴", "┘", nw);

	fputs("\r\n", stdout);
	if (k > 0)
		printf(FG BOLD "a-%c" NOBOLD DIM " toggle" LINE "  ·  ",
		    keys[(k < NKEYS ? k : NKEYS) - 1]);
	fputs(d ? WARN BOLD "q" NOBOLD " save & quit" : FG BOLD "q" NOBOLD DIM " save & quit",
	    stdout);
	fputs(BASE "\r\n", stdout);
	fflush(stdout);
}

int
main(void)
{
	const char *dir, *cfg, *p;
	char date[11], day[PATH_MAX], tmpl[PATH_MAX], hdr[16], *s;
	time_t now;
	struct tm *tm;
	ssize_t n;
	char c;

	if (!isatty(STDIN_FILENO))
		die("stdin is not a tty");

	if ((dir = getenv("HABITS_DIR")) == NULL)
		dir = ".";
	if ((cfg = getenv("HABITS_CONFIG")) == NULL)
		cfg = ".";
	now = time(NULL);
	if ((tm = localtime(&now)) == NULL ||
	    strftime(date, sizeof(date), "%Y-%m-%d", tm) == 0)
		die("cannot get local date");
	if ((size_t)snprintf(day, sizeof(day), "%s/%s.md", dir, date) >= sizeof(day))
		die("path too long: %s", dir);
	if ((size_t)snprintf(tmpl, sizeof(tmpl), "%s/template", cfg) >= sizeof(tmpl))
		die("path too long: %s", cfg);

	if (load(day) == -1) {
		if (errno != ENOENT)
			die("open %s: %s", day, strerror(errno));
		snprintf(hdr, sizeof(hdr), "# %s\n", date);
		if ((s = strdup(hdr)) == NULL)
			die("out of memory");
		addline(s, strlen(s));
		if (load(tmpl) == -1)
			die("open %s: %s", tmpl, strerror(errno));
		save(day);
	} else
		snapshot();

	raw_on();
	render(date, 0);
	for (;;) {
		if ((n = read(STDIN_FILENO, &c, 1)) == -1)
			die("read: %s", strerror(errno));
		if (n == 1 && c == 3)		/* ^C: discard */
			break;
		if (n == 0 || c == 'q') {	/* EOF, q */
			if (dirty()) {
				/* The write may block for a while on iCloud. */
				render(date, 1);
				save(day);
			}
			break;
		}
		if (c != '\0' && (p = strchr(keys, c)) != NULL &&
		    toggle((size_t)(p - keys)))
			render(date, 0);
	}
	return EXIT_SUCCESS;
}
