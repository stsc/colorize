/*
 * colorize - Read text from standard input stream or file and print
 *            it colorized through use of ANSI escape sequences
 *
 * Copyright (c) 2011-2013 Steven Schubiger
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#define _POSIX_SOURCE
#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <getopt.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define DEBUG 0

#define str(arg) #arg
#define to_str(arg) str(arg)

#define streq(s1, s2) (strcmp (s1, s2) == 0)

#define xmalloc(size)       malloc_wrap(size,       __FILE__, __LINE__)
#define xrealloc(ptr, size) realloc_wrap(ptr, size, __FILE__, __LINE__)
#define xstrdup(str)        strdup_wrap(str,        __FILE__, __LINE__)

#if !defined BUF_SIZE || BUF_SIZE <= 0
# undef BUF_SIZE
# define BUF_SIZE 4096 + 1
#endif

#define LF 0x01
#define CR 0x02

#define SKIP_LINE_ENDINGS(flags) (((flags) & CR) && ((flags) & LF) ? 2 : 1)

#define STACK_VAR(ptr) do {                                   \
    stack_var (&vars_list, &stacked_vars, stacked_vars, ptr); \
} while (false);

#define RELEASE_VAR(ptr) do {                             \
    release_var (vars_list, stacked_vars, (void **)&ptr); \
} while (false);

#if DEBUG
# define MEM_ALLOC_FAIL(file, line) do {                                                    \
    fprintf (stderr, "memory allocation failure in source file %s, line %d\n", file, line); \
    exit (2);                                                                               \
} while (false);
#else
# define MEM_ALLOC_FAIL(file, line) do {                               \
    fprintf (stderr, "%s: memory allocation failure\n", program_name); \
    exit (2);                                                          \
} while (false);
#endif

#define ABORT_TRACE()                                                              \
    fprintf (stderr, "aborting in source file %s, line %d\n", __FILE__, __LINE__); \
    abort ();                                                                      \

#define CHECK_COLORS_RANDOM(color1, color2)        \
     streq (color_names[color1]->name, "random")   \
 && (streq (color_names[color2]->name, "none")     \
  || streq (color_names[color2]->name, "default")) \

#define VERSION "0.48"

typedef unsigned short bool;

enum { false, true };

struct color_name {
    char *name;
    char *orig;
};

static struct color_name *color_names[3] = { NULL, NULL, NULL };

struct color {
    const char *name;
    const char *code;
};

static const struct color fg_colors[] = {
    { "none",     NULL },
    { "black",   "30m" },
    { "red",     "31m" },
    { "green",   "32m" },
    { "yellow",  "33m" },
    { "blue",    "34m" },
    { "cyan",    "35m" },
    { "magenta", "36m" },
    { "white",   "37m" },
    { "default", "39m" },
};
static const struct color bg_colors[] = {
    { "none",     NULL },
    { "black",   "40m" },
    { "red",     "41m" },
    { "green",   "42m" },
    { "yellow",  "43m" },
    { "blue",    "44m" },
    { "cyan",    "45m" },
    { "magenta", "46m" },
    { "white",   "47m" },
    { "default", "49m" },
};

enum fmts {
    FMT_GENERIC,
    FMT_COLOR,
    FMT_RANDOM,
    FMT_ERROR,
    FMT_FILE
};
static const char *formats[] = {
    "%s",                    /* generic */
    "%s color '%s' %s",      /* color   */
    "%s color '%s' %s '%s'", /* random  */
    "less than %d bytes %s", /* error   */
    "%s: %s",                /* file    */
};

enum { FOREGROUND, BACKGROUND };

static const struct {
    struct color const *entries;
    unsigned int count;
    const char *desc;
} tables[] = {
    { fg_colors, sizeof (fg_colors) / sizeof (struct color), "foreground" },
    { bg_colors, sizeof (bg_colors) / sizeof (struct color), "background" },
};

enum stream_mode { SCAN_FIRST = 1, SCAN_ALWAYS };

struct ending {
    unsigned int flags;
    const char newline[3];
};

static const struct ending endings[] = {
    { CR & LF, "\r\n" },
    { CR,      "\r"   },
    { LF,      "\n"   },
};

static FILE *stream = NULL;

static unsigned int stacked_vars = 0;
static void **vars_list = NULL;

static char *exclude = NULL;

static const char *program_name;

static void print_help (void);
static void print_version (void);
static void cleanup (void);
static void free_color_names (struct color_name **);
static void process_options (unsigned int, char **, bool *, const struct color **, const char **, FILE **);
static void read_print_stream (bool, const struct color **, const char *, FILE *, enum stream_mode);
static void find_color_entries (struct color_name **, const struct color **);
static void find_color_entry (const char *const, unsigned int, const struct color **);
static void print_line (const struct color **, bool, const char * const, unsigned int);
static void *malloc_wrap (size_t, const char *, unsigned int);
static void *realloc_wrap (void *, size_t, const char *, unsigned int);
static char *strdup_wrap (const char *, const char *, unsigned int);
static void vfprintf_fail (const char *, ...);
static void stack_var (void ***, unsigned int *, unsigned int, void *);
static void release_var (void **, unsigned int, void **);

extern char *optarg;
extern int optind;

int
main (int argc, char **argv)
{
    unsigned int arg_cnt = 0;

    bool invalid_opt = false;

    int opt;
    struct option long_opts[] = {
        { "exclude-random", required_argument, NULL, 'e' },
        { "help",           no_argument,       NULL, 'h' },
        { "version",        no_argument,       NULL, 'v' },
        {  0,               0,                 0,     0  },
    };

    bool bold = false;

    const struct color *colors[2] = {
        NULL, /* foreground */
        NULL, /* background */
    };

    const char *file;

    enum stream_mode mode = SCAN_FIRST;

    program_name = argv[0];
    atexit (cleanup);

    setvbuf (stdout, NULL, _IOLBF, 0);

    while ((opt = getopt_long (argc, argv, "hv", long_opts, NULL)) != -1)
      {
        switch (opt)
          {
            case 'e': {
              char *p;
              exclude = xstrdup (optarg);
              STACK_VAR (exclude);
              for (p = exclude; *p; p++)
                *p = tolower (*p);
              if (streq (exclude, "random"))
                vfprintf_fail (formats[FMT_GENERIC], "--exclude-random switch must be provided a color");
              break;
            }
            case 'h':
              print_help ();
              exit (EXIT_SUCCESS);
            case 'v':
              print_version ();
              exit (EXIT_SUCCESS);
            case '?':
              invalid_opt = true;
              break;
            default: /* never reached */
              ABORT_TRACE ();
          }
      }

    arg_cnt = argc - optind;

    if (arg_cnt == 0 || arg_cnt > 2 || invalid_opt)
      {
        print_help ();
        exit (EXIT_FAILURE);
      }

    process_options (arg_cnt, &argv[optind], &bold, colors, &file, &stream);
    read_print_stream (bold, colors, file, stream, mode);

    RELEASE_VAR (exclude);

    exit (EXIT_SUCCESS);
}

static void
print_help (void)
{
    unsigned int i;

    printf ("Usage: %s (foreground) OR (foreground)/(background) [-|file]\n\n", program_name);
    printf ("\tColors (foreground) (background)\n");
    for (i = 0; i < tables[FOREGROUND].count; i++)
      {
        const struct color *entry = &tables[FOREGROUND].entries[i];
        const char *name = entry->name;
        const char *code = entry->code;
        if (code)
          printf ("\t\t{\033[%s#\033[0m} [%c%c]%s%*s%s\n",
                   code, toupper (*name), *name, name + 1, 10 - (int)strlen (name), " ", name);
        else
          printf ("\t\t{-} %s%*s%s\n", name, 13 - (int)strlen (name), " ", name);
      }
    printf ("\t\t{*} [Rr]%s%*s%s [--exclude-random=<foreground color>]\n", "andom", 10 - (int)strlen ("random"), " ", "random");

    printf ("\n\tFirst character of color name in upper case denotes increased intensity,\n");
    printf ("\twhereas for lower case colors will be of normal intensity.\n");

    printf ("\n\tOptions\n");
    printf ("\t\t-h, --help\n");
    printf ("\t\t-v, --version\n\n");
}

static void
print_version (void)
{
    const char *c_flags;
    printf ("%s v%s (compiled at %s, %s)\n", "colorize", VERSION, __DATE__, __TIME__);
#ifdef CFLAGS
    c_flags = to_str (CFLAGS);
#else
    c_flags = "unknown";
#endif
    printf ("Compiler flags: %s\n", c_flags);
    printf ("Buffer size: %d bytes\n", BUF_SIZE - 1);
}

static void
cleanup (void)
{
    free_color_names (color_names);

    if (stream && fileno (stream) != STDIN_FILENO)
      fclose (stream);

    if (vars_list)
      {
        unsigned int i;
        for (i = 0; i < stacked_vars; i++)
          if (vars_list[i])
            {
              free (vars_list[i]);
              vars_list[i] = NULL;
            }
        free (vars_list);
        vars_list = NULL;
      }
}

static void
free_color_names (struct color_name **color_names)
{
    unsigned int i;
    for (i = 0; color_names[i]; i++)
      {
        free (color_names[i]->name);
        color_names[i]->name = NULL;
        free (color_names[i]->orig);
        color_names[i]->orig = NULL;
        free (color_names[i]);
        color_names[i] = NULL;
      }
}

static void
process_options (unsigned int arg_cnt, char **option_strings, bool *bold, const struct color **colors, const char **file, FILE **stream)
{
    int ret;
    unsigned int index;
    char *color, *p, *str;
    struct stat sb;

    const char *color_string = arg_cnt >= 1 ? option_strings[0] : NULL;
    const char *file_string  = arg_cnt == 2 ? option_strings[1] : NULL;

    assert (color_string);

    if (streq (color_string, "-"))
      {
        if (file_string)
          vfprintf_fail (formats[FMT_GENERIC], "hyphen cannot be used as color string");
        else
          vfprintf_fail (formats[FMT_GENERIC], "hyphen must be preceeded by color string");
      }

    ret = stat (color_string, &sb);

    /* Ensure that we don't fail if there's a file with one or more
       color names in its path.  */
    if (ret != -1)
      {
        bool have_file;
        unsigned int c;
        const char *color = color_string;

        for (c = 1; c <= 2 && *color; c++)
          {
            bool matched = false;
            unsigned int i;
            for (i = 0; i < tables[FOREGROUND].count; i++)
              {
                const struct color *entry = &tables[FOREGROUND].entries[i];
                char *p;
                if ((p = strstr (color, entry->name)) && p == color)
                  {
                    color = p + strlen (entry->name);
                    matched = true;
                    break;
                  }
              }
            if (matched && *color == '/' && *(color + 1))
              color++;
            else
              break;
          }

        have_file = (*color != '\0');

        if (have_file)
          vfprintf_fail (formats[FMT_GENERIC], "file must be preceeded by color string");
      }

    if ((p = strchr (color_string, '/')))
      {
        if (p == color_string)
          vfprintf_fail (formats[FMT_GENERIC], "foreground color missing");
        else if (p == color_string + strlen (color_string) - 1)
          vfprintf_fail (formats[FMT_GENERIC], "background color missing");
        else if (strchr (++p, '/'))
          vfprintf_fail (formats[FMT_GENERIC], "one color pair allowed only");
      }

    str = xstrdup (color_string);
    STACK_VAR (str);

    for (index = 0, color = str; *color; index++, color = p)
      {
        char *ch, *sep;
        if ((sep = strchr (color, '/')))
          {
            *sep = '\0';
            p = sep + 1;
          }
        else
          p = color + strlen (color);

        for (ch = color; *ch; ch++)
          if (!isalpha (*ch))
            vfprintf_fail (formats[FMT_COLOR], tables[index].desc, color, "cannot be made of non-alphabetic characters");

        for (ch = color + 1; *ch; ch++)
          if (!islower (*ch))
            vfprintf_fail (formats[FMT_COLOR], tables[index].desc, color, "cannot be in mixed lower/upper case");

        if (streq (color, "None"))
          vfprintf_fail (formats[FMT_COLOR], tables[index].desc, color, "cannot be bold");

        if (isupper (*color))
          {
            switch (index)
              {
                case FOREGROUND:
                  *bold = true;
                  break;
                case BACKGROUND:
                  vfprintf_fail (formats[FMT_COLOR], tables[BACKGROUND].desc, color, "cannot be bold");
                  break;
                default: /* never reached */
                  ABORT_TRACE ();
              }
          }

        color_names[index] = xmalloc (sizeof (struct color_name));

        color_names[index]->orig = xstrdup (color);

        for (ch = color; *ch; ch++)
          *ch = tolower (*ch);

        color_names[index]->name = xstrdup (color);
      }

    RELEASE_VAR (str);

    assert (color_names[FOREGROUND]);

    if (color_names[BACKGROUND])
      {
        unsigned int i;
        unsigned int color_sets[2][2] = { { FOREGROUND, BACKGROUND }, { BACKGROUND, FOREGROUND } };
        for (i = 0; i < 2; i++)
          {
            unsigned int color1 = color_sets[i][0];
            unsigned int color2 = color_sets[i][1];
            if (CHECK_COLORS_RANDOM (color1, color2))
              vfprintf_fail (formats[FMT_RANDOM], tables[color1].desc, color_names[color1]->orig, "cannot be combined with", color_names[color2]->orig);
          }
      }

    find_color_entries (color_names, colors);
    free_color_names (color_names);

    if (!colors[FOREGROUND]->code && colors[BACKGROUND] && colors[BACKGROUND]->code)
      find_color_entry ("default", FOREGROUND, colors);

    if (file_string)
      {
        if (streq (file_string, "-"))
          *stream = stdin;
        else
          {
            FILE *s;
            const char *file = file_string;
            struct stat sb;
            int errno, ret;

            errno = 0;
            ret = stat (file, &sb);

            if (ret == -1)
              vfprintf_fail (formats[FMT_FILE], file, strerror (errno));

            if (!(S_ISREG (sb.st_mode) || S_ISLNK (sb.st_mode) || S_ISFIFO (sb.st_mode)))
              vfprintf_fail (formats[FMT_FILE], file, "unrecognized file type");

            errno = 0;

            s = fopen (file, "r");
            if (!s)
              vfprintf_fail (formats[FMT_FILE], file, strerror (errno));
            *stream = s;
          }
        *file = file_string;
      }
    else
      {
        *stream = stdin;
        *file = "stdin";
      }

    assert (*stream);
}

static void
read_print_stream (bool bold, const struct color **colors, const char *file, FILE *stream, enum stream_mode mode)
{
    char buf[BUF_SIZE];
    unsigned int flags = 0;
    bool first = false, always = false;

    switch (mode)
      {
        case SCAN_FIRST:
          first = true;
          break;
        case SCAN_ALWAYS:
          always = true;
          break;
        default: /* never reached */
          ABORT_TRACE ();
      }

    while (!feof (stream))
      {
        size_t bytes_read;
        char *eol;
        const char *line;
        memset (buf, '\0', BUF_SIZE);
        bytes_read = fread (buf, 1, BUF_SIZE - 1, stream);
        if (bytes_read != (BUF_SIZE - 1) && ferror (stream))
          vfprintf_fail (formats[FMT_ERROR], BUF_SIZE - 1, "read");
        line = buf;
        LOOP: while ((eol = strpbrk (line, "\n\r")))
          {
            char *p;
            if (first || always)
              {
                first = false;
                flags &= ~(CR|LF);
                if (*eol == '\r')
                  {
                    flags |= CR;
                    if (*(eol + 1) == '\n')
                      flags |= LF;
                  }
                else if (*eol == '\n')
                  flags |= LF;
                else
                  vfprintf_fail (formats[FMT_FILE], file, "unrecognized line ending");
              }
            if (always)
              p = eol + SKIP_LINE_ENDINGS (flags);
            else /* first */
              {
                unsigned int i;
                unsigned int count = sizeof (endings) / sizeof (struct ending);
                for (i = 0; i < count; i++)
                  {
                    if (flags & endings[i].flags)
                      {
                        char *p;
                        if ((p = strstr (eol, endings[i].newline)) && p == eol)
                          break;
                        else
                          {
                            always = true;
                            goto LOOP;
                          }
                      }
                  }
                p = eol + SKIP_LINE_ENDINGS (flags);
              }
            *eol = '\0';
            print_line (colors, bold, line, flags);
            line = p;
          }
        print_line (colors, bold, line, 0);
      }
}

static void
find_color_entries (struct color_name **color_names, const struct color **colors)
{
    struct timeval tv;
    unsigned int index;

    /* randomness */
    gettimeofday (&tv, NULL);
    srand (tv.tv_usec * tv.tv_sec);

    for (index = 0; color_names[index]; index++)
      {
        const char *color_name = color_names[index]->name;

        const unsigned int count                = tables[index].count;
        const struct color *const color_entries = tables[index].entries;

        if (streq (color_name, "random"))
          {
            bool excludable;
            unsigned int i;
            do {
              excludable = false;
              i = rand() % (count - 2) + 1; /* omit color none and default */
              switch (index)
                {
                  case FOREGROUND:
                    /* --exclude-random */
                    if (exclude && streq (exclude, color_entries[i].name))
                      excludable = true;
                    else if (color_names[BACKGROUND] && streq (color_names[BACKGROUND]->name, color_entries[i].name))
                      excludable = true;
                    break;
                  case BACKGROUND:
                    if (streq (colors[FOREGROUND]->name, color_entries[i].name))
                      excludable = true;
                    break;
                  default: /* never reached */
                    ABORT_TRACE ();
                 }
            } while (excludable);
            colors[index] = (struct color *)&color_entries[i];
          }
        else
          find_color_entry (color_name, index, colors);
      }
}

static void
find_color_entry (const char *const color_name, unsigned int index, const struct color **colors)
{
    bool found = false;
    unsigned int i;

    const unsigned int count                = tables[index].count;
    const struct color *const color_entries = tables[index].entries;

    for (i = 0; i < count; i++)
      if (streq (color_name, color_entries[i].name))
        {
          colors[index] = (struct color *)&color_entries[i];
          found = true;
          break;
        }
    if (!found)
      vfprintf_fail (formats[FMT_COLOR], tables[index].desc, color_name, "not recognized");
}

static void
print_line (const struct color **colors, bool bold, const char *const line, unsigned int flags)
{
    if (colors[BACKGROUND] && colors[BACKGROUND]->code)
      printf ("\033[%s", colors[BACKGROUND]->code);
    if (colors[FOREGROUND]->code)
      printf ("\033[%s%s%s\033[0m", bold ? "1;" : "", colors[FOREGROUND]->code, line);
    else
      printf (formats[FMT_GENERIC], line);
    if (flags & CR)
      putchar ('\r');
    if (flags & LF)
      putchar ('\n');
}

static void *
malloc_wrap (size_t size, const char *file, unsigned int line)
{
    void *p = malloc (size);
    if (!p)
      MEM_ALLOC_FAIL (file, line);
    return p;
}

static void *
realloc_wrap (void *ptr, size_t size, const char *file, unsigned int line)
{
    void *p = realloc (ptr, size);
    if (!p)
      MEM_ALLOC_FAIL (file, line);
    return p;
}

static char *
strdup_wrap (const char *str, const char *file, unsigned int line)
{
    const unsigned int len = strlen (str) + 1;
    char *p = malloc (len);
    if (!p)
      MEM_ALLOC_FAIL (file, line);
    strncpy (p, str, len);
    return p;
}

static void
vfprintf_fail (const char *fmt, ...)
{
    va_list ap;
    fprintf (stderr, "%s: ", program_name);
    va_start (ap, fmt);
    vfprintf (stderr, fmt, ap);
    va_end (ap);
    fprintf (stderr, "\n");
    exit (EXIT_FAILURE);
}

static void
stack_var (void ***list, unsigned int *stacked, unsigned int index, void *ptr)
{
    /* nothing to stack */
    if (ptr == NULL)
      return;
    if (!*list)
      *list = xmalloc (sizeof (void *));
    else
      {
        unsigned int i;
        for (i = 0; i < *stacked; i++)
          if (!(*list)[i])
            {
              (*list)[i] = ptr;
              return; /* reused */
            }
        *list = xrealloc (*list, (*stacked + 1) * sizeof (void *));
      }
    (*list)[index] = ptr;
    (*stacked)++;
}

static void
release_var (void **list, unsigned int stacked, void **ptr)
{
    unsigned int i;
    /* nothing to release */
    if (*ptr == NULL)
      return;
    for (i = 0; i < stacked; i++)
      if (list[i] == *ptr)
        {
          free (*ptr);
          *ptr = NULL;
          list[i] = NULL;
          return;
        }
}
