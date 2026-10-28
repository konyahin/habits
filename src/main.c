#define _POSIX_C_SOURCE 200809L

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

static struct termios orig;
static int raw;

/* The day file, line by line, newlines included: written back verbatim. */
static char **lines;
static size_t nlines;
/* First char of every line as last saved: toggling back is not a change. */
static char *saved;

static void
raw_off(void)
{
	if (raw) {
		fputs("\x1b[?25h", stdout);	/* show cursor */
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
	fputs("\x1b[?25l", stdout);	/* hide cursor */
}

static void
addline(char *s)
{
	char **p;

	if ((p = realloc(lines, (nlines + 1) * sizeof(*lines))) == NULL)
		die("out of memory");
	lines = p;
	lines[nlines++] = s;
}

/* Appends the file's lines; -1 with errno set if it cannot be opened. */
static int
load(const char *path)
{
	FILE *f;
	char *buf = NULL;
	size_t cap = 0;

	if ((f = fopen(path, "r")) == NULL)
		return -1;
	while (getline(&buf, &cap, f) != -1) {
		addline(buf);
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

/* Write to a temp file and rename, so a crash never leaves half a day. */
static void
save(const char *path)
{
	char tmp[PATH_MAX];
	FILE *f;
	size_t i;

	if ((size_t)snprintf(tmp, sizeof(tmp), "%s.tmp", path) >= sizeof(tmp))
		die("path too long: %s", path);
	if ((f = fopen(tmp, "w")) == NULL)
		die("open %s: %s", tmp, strerror(errno));
	for (i = 0; i < nlines; i++)
		fputs(lines[i], f);
	if (fclose(f) == EOF)
		die("write %s: %s", tmp, strerror(errno));
	if (rename(tmp, path) == -1)
		die("rename %s: %s", tmp, strerror(errno));
	snapshot();
}

/* "- x" good, "+ x" good done, "! x" bad, "* x" bad done (a lapse). */
static int
ishabit(const char *l)
{
	return l[0] != '\0' && strchr("-+!*", l[0]) != NULL && l[1] == ' ';
}

static int
toggle(size_t k)
{
	size_t i;

	for (i = 0; i < nlines; i++) {
		char *l = lines[i];

		if (!ishabit(l) || k-- > 0)
			continue;
		switch (l[0]) {
		case '-': l[0] = '+'; break;
		case '+': l[0] = '-'; break;
		case '!': l[0] = '*'; break;
		case '*': l[0] = '!'; break;
		}
		return 1;
	}
	return 0;
}

static void
render(const char *date)
{
	size_t i, k = 0;

	/* OPOST is off: newlines need an explicit \r. */
	fputs("\x1b[2J\x1b[H", stdout);
	printf("%s\r\n\r\n", date);
	for (i = 0; i < nlines; i++) {
		const char *l = lines[i];

		if (!ishabit(l))
			continue;
		printf("%c %.*s\r\n", k < NKEYS ? keys[k] : ' ',
		    (int)strcspn(l, "\r\n"), l);
		k++;
	}
	fputs(dirty() ? "\r\n\x1b[31mq  save and quit\x1b[0m\r\n" :
	    "\r\nq  save and quit\r\n", stdout);	fflush(stdout);
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
		addline(s);
		if (load(tmpl) == -1)
			die("open %s: %s", tmpl, strerror(errno));
		save(day);
	} else
		snapshot();

	raw_on();
	render(date);
	for (;;) {
		if ((n = read(STDIN_FILENO, &c, 1)) == -1)
			die("read: %s", strerror(errno));
		if (n == 1 && c == 3)		/* ^C: discard */
			break;
		if (n == 0 || c == 'q') {	/* EOF, q */
			if (dirty())
				save(day);
			break;
		}
		if (c != '\0' && (p = strchr(keys, c)) != NULL &&
		    toggle((size_t)(p - keys)))
			render(date);
	}
	return EXIT_SUCCESS;
}
