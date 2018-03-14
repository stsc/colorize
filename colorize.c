/*
 * colorize - Read text from standard input stream or file and print
 *            it colorized through use of ANSI escape sequences
 *
 * Copyright (c) 2011-2017 Steven Schubiger
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

#define _DEFAULT_SOURCE
#define _BSD_SOURCE
#define _XOPEN_SOURCE 700
#define _FILE_OFFSET_BITS 64
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

#ifndef DEBUG
# define DEBUG 0
#endif

#define str(arg) #arg
#define to_str(arg) str(arg)

#define streq(s1, s2) (strcmp (s1, s2) == 0)
#define strneq(s1, s2, n) (strncmp (s1, s2, n) == 0)

#if !DEBUG
# define xmalloc(size)          malloc_wrap(size)
# define xcalloc(nmemb, size)   calloc_wrap(nmemb, size)
# define xrealloc(ptr, size)    realloc_wrap(ptr, size)
# define xstrdup(str)           strdup_wrap(str,            NULL, 0)
# define str_concat(str1, str2) str_concat_wrap(str1, str2, NULL, 0)
#else
# define xmalloc(size)          malloc_wrap_debug(size,        __FILE__, __LINE__)
# define xcalloc(nmemb, size)   calloc_wrap_debug(nmemb, size, __FILE__, __LINE__)
# define xrealloc(ptr, size)    realloc_wrap_debug(ptr, size,  __FILE__, __LINE__)
# define xstrdup(str)           strdup_wrap(str,               __FILE__, __LINE__)
# define str_concat(str1, str2) str_concat_wrap(str1, str2,    __FILE__, __LINE__)
#endif

#define free_null(ptr) free_wrap((void **)&ptr)

#if defined(BUF_SIZE) && (BUF_SIZE <= 0 || BUF_SIZE > 65536)
# undef BUF_SIZE
#endif
#ifndef BUF_SIZE
# define BUF_SIZE 4096
#endif

#define LF 0x01
#define CR 0x02

#define SKIP_LINE_ENDINGS(flags) ((flags) == (CR|LF) ? 2 : 1)

#define VALID_FILE_TYPE(mode) (S_ISREG (mode) || S_ISLNK (mode) || S_ISFIFO (mode))

#define STACK_VAR(ptr) do {                                   \
    stack_var (&vars_list, &stacked_vars, stacked_vars, ptr); \
} while (false)

#define RELEASE_VAR(ptr) do {                             \
    release_var (vars_list, stacked_vars, (void **)&ptr); \
} while (false)

#if !DEBUG
# define MEM_ALLOC_FAIL() do {                                         \
    fprintf (stderr, "%s: memory allocation failure\n", program_name); \
    exit (EXIT_FAILURE);                                               \
} while (false)
#else
# define MEM_ALLOC_FAIL_DEBUG(file, line) do {                                              \
    fprintf (stderr, "Memory allocation failure in source file %s, line %u\n", file, line); \
    exit (EXIT_FAILURE);                                                                    \
} while (false)
#endif

#define ABORT_TRACE()                                                              \
    fprintf (stderr, "Aborting in source file %s, line %u\n", __FILE__, __LINE__); \
    abort ();

#define CHECK_COLORS_RANDOM(color1, color2)        \
     streq (color_names[color1]->name, "random")   \
 && (streq (color_names[color2]->name, "none")     \
  || streq (color_names[color2]->name, "default"))

#define ALLOC_COMPLETE_PART_LINE 8

#if defined(COLOR_SEP_CHAR_COLON)
# define COLOR_SEP_CHAR ':'
#elif defined(COLOR_SEP_CHAR_SLASH)
# define COLOR_SEP_CHAR '/'
#else
# define COLOR_SEP_CHAR '/'
#endif

#if DEBUG
# define DEBUG_FILE "debug.txt"
#endif

#define MAX_ATTRIBUTE_CHARS (6 * 2)

#define PROGRAM_NAME "colorize"

#define VERSION "0.63"

typedef enum { false, true } bool;

struct color_name {
    char *name;
    char *orig;
};

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
    { "magenta", "35m" },
    { "cyan",    "36m" },
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
    { "magenta", "45m" },
    { "cyan",    "46m" },
    { "white",   "47m" },
    { "default", "49m" },
};

struct bytes_size {
    unsigned int size;
    char unit;
};

enum {
    FMT_GENERIC,
    FMT_STRING,
    FMT_QUOTE,
    FMT_COLOR,
    FMT_RANDOM,
    FMT_ERROR,
    FMT_FILE,
    FMT_TYPE
};
static const char *formats[] = {
    "%s",                     /* generic */
    "%s '%s'",                /* string  */
    "%s `%s' %s",             /* quote   */
    "%s color '%s' %s",       /* color   */
    "%s color '%s' %s '%s'",  /* random  */
    "less than %lu bytes %s", /* error   */
    "%s: %s",                 /* file    */
    "%s: %s: %s",             /* type    */
};

enum { GENERIC, FOREGROUND = 0, BACKGROUND };

static const struct {
    const struct color *entries;
    unsigned int count;
    const char *desc;
} tables[] = {
    { fg_colors, sizeof (fg_colors) / sizeof (struct color), "foreground" },
    { bg_colors, sizeof (bg_colors) / sizeof (struct color), "background" },
};

enum {
    OPT_ATTR = 1,
    OPT_CLEAN,
    OPT_CLEAN_ALL,
    OPT_EXCLUDE_RANDOM,
    OPT_OMIT_COLOR_EMPTY,
    OPT_HELP,
    OPT_VERSION
};
static int opt_type;
static const struct option long_opts[] = {
    { "attr",             required_argument, &opt_type, OPT_ATTR             },
    { "clean",            no_argument,       &opt_type, OPT_CLEAN            },
    { "clean-all",        no_argument,       &opt_type, OPT_CLEAN_ALL        },
    { "exclude-random",   required_argument, &opt_type, OPT_EXCLUDE_RANDOM   },
    { "omit-color-empty", no_argument,       &opt_type, OPT_OMIT_COLOR_EMPTY },
    { "help",             no_argument,       &opt_type, OPT_HELP             },
    { "version",          no_argument,       &opt_type, OPT_VERSION          },
    {  NULL,              0,                 NULL,      0                    },
};

enum attr_type {
    ATTR_BOLD = 0x01,
    ATTR_UNDERSCORE = 0x02,
    ATTR_BLINK = 0x04,
    ATTR_REVERSE = 0x08,
    ATTR_CONCEALED = 0x10
};
struct attr {
    const char *name;
    unsigned int val;
    enum attr_type type;
};

static FILE *stream;
#if DEBUG
static FILE *log;
#endif

static unsigned int stacked_vars;
static void **vars_list;

static bool clean;
static bool clean_all;
static bool omit_color_empty;

static char attr[MAX_ATTRIBUTE_CHARS + 1];
static char *exclude;

static const char *program_name;

static void process_opts (int, char **);
static void process_opt_attr (const char *);
static void write_attr (const struct attr *, unsigned int *);
static void print_hint (void);
static void print_help (void);
static void print_version (void);
static void cleanup (void);
static void free_color_names (struct color_name **);
static void process_args (unsigned int, char **, char *, const struct color **, const char **, FILE **);
static void process_file_arg (const char *, const char **, FILE **);
static void skip_path_colors (const char *, const char *, const struct stat *);
static void gather_color_names (const char *, char *, struct color_name **);
static void read_print_stream (const char *, const struct color **, const char *, FILE *);
static void merge_print_line (const char *, const char *, FILE *);
static void complete_part_line (const char *, char **, FILE *);
static bool get_next_char (char *, const char **, FILE *, bool *);
static void save_char (char, char **, size_t *, size_t *);
static void find_color_entries (struct color_name **, const struct color **);
static void find_color_entry (const struct color_name *, unsigned int, const struct color **);
static void print_line (const char *, const struct color **, const char * const, unsigned int, bool);
static void print_clean (const char *);
static bool is_esc (const char *);
static const char *get_end_of_esc (const char *);
static const char *get_end_of_text (const char *);
static void print_text (const char *, size_t);
static bool gather_esc_offsets (const char *, const char **, const char **);
static bool validate_esc_clean_all (const char **);
static bool validate_esc_clean (int, unsigned int, unsigned int *, const char **, bool *);
static bool is_reset (int, unsigned int, const char **);
static bool is_attr (int, unsigned int, unsigned int, const char **);
static bool is_fg_color (int, const char **);
static bool is_bg_color (int, unsigned int, const char **);
#if !DEBUG
static void *malloc_wrap (size_t);
static void *calloc_wrap (size_t, size_t);
static void *realloc_wrap (void *, size_t);
#else
static void *malloc_wrap_debug (size_t, const char *, unsigned int);
static void *calloc_wrap_debug (size_t, size_t, const char *, unsigned int);
static void *realloc_wrap_debug (void *, size_t, const char *, unsigned int);
#endif
static void free_wrap (void **);
static char *strdup_wrap (const char *, const char *, unsigned int);
static char *str_concat_wrap (const char *, const char *, const char *, unsigned int);
static bool get_bytes_size (unsigned long, struct bytes_size *);
static char *get_file_type (mode_t);
static bool has_color_name (const char *, const char *);
static FILE *open_file (const char *, const char *);
static void vfprintf_diag (const char *, ...);
static void vfprintf_fail (const char *, ...);
static void stack_var (void ***, unsigned int *, unsigned int, void *);
static void release_var (void **, unsigned int, void **);

extern int optind;

int
main (int argc, char **argv)
{
    unsigned int arg_cnt;

    const struct color *colors[2] = {
        NULL, /* foreground */
        NULL, /* background */
    };

    const char *file = NULL;

    program_name = argv[0];
    atexit (cleanup);

    setvbuf (stdout, NULL, _IOLBF, 0);

#if DEBUG
    log = open_file (DEBUG_FILE, "w");
#endif

    attr[0] = '\0';

    process_opts (argc, argv);

    arg_cnt = argc - optind;

    if (clean || clean_all)
      {
        if (clean && clean_all)
          vfprintf_fail (formats[FMT_GENERIC], "--clean and --clean-all switch are mutually exclusive");
        if (arg_cnt > 1)
          {
            const char *format = "%s %s";
            const char *message = "switch cannot be used with more than one file";
            if (clean)
              vfprintf_fail (format, "--clean", message);
            else if (clean_all)
              vfprintf_fail (format, "--clean-all", message);
          }
      }
    else
      {
        if (arg_cnt == 0 || arg_cnt > 2)
          {
            vfprintf_diag ("%u arguments provided, expected 1-2 arguments or clean option", arg_cnt);
            print_hint ();
            exit (EXIT_FAILURE);
          }
      }

    if (clean || clean_all)
      process_file_arg (argv[optind], &file, &stream);
    else
      process_args (arg_cnt, &argv[optind], &attr[0], colors, &file, &stream);
    read_print_stream (&attr[0], colors, file, stream);

    RELEASE_VAR (exclude);

    exit (EXIT_SUCCESS);
}

#define PRINT_HELP_EXIT() \
    print_help ();        \
    exit (EXIT_SUCCESS);

#define PRINT_VERSION_EXIT() \
    print_version ();        \
    exit (EXIT_SUCCESS);

extern char *optarg;

static void
process_opts (int argc, char **argv)
{
    int opt;
    while ((opt = getopt_long (argc, argv, "hV", long_opts, NULL)) != -1)
      {
        switch (opt)
          {
            case 0: /* long opts */
              switch (opt_type)
                {
                  case OPT_ATTR:
                    process_opt_attr (optarg);
                    break;
                  case OPT_CLEAN:
                    clean = true;
                    break;
                  case OPT_CLEAN_ALL:
                    clean_all = true;
                    break;
                  case OPT_EXCLUDE_RANDOM: {
                    bool valid = false;
                    unsigned int i;
                    exclude = xstrdup (optarg);
                    STACK_VAR (exclude);
                    for (i = 1; i < tables[GENERIC].count - 1; i++) /* skip color none and default */
                      {
                        const struct color *entry = &tables[GENERIC].entries[i];
                        if (streq (exclude, entry->name))
                          {
                            valid = true;
                            break;
                          }
                      }
                    if (!valid)
                      vfprintf_fail (formats[FMT_GENERIC], "--exclude-random switch must be provided a plain color");
                    break;
                  }
                  case OPT_OMIT_COLOR_EMPTY:
                    omit_color_empty = true;
                    break;
                  case OPT_HELP:
                    PRINT_HELP_EXIT ();
                  case OPT_VERSION:
                    PRINT_VERSION_EXIT ();
                  default: /* never reached */
                    ABORT_TRACE ();
                }
              break;
            case 'h':
              PRINT_HELP_EXIT ();
            case 'V':
              PRINT_VERSION_EXIT ();
            case '?':
              print_hint ();
              exit (EXIT_FAILURE);
            default: /* never reached */
              ABORT_TRACE ();
          }
      }
}

static void
process_opt_attr (const char *p)
{
    /* If attributes are added to this "list", also increase MAX_ATTRIBUTE_CHARS!  */
    const struct attr attrs[] = {
        { "bold",       1, ATTR_BOLD       },
        { "underscore", 4, ATTR_UNDERSCORE },
        { "blink",      5, ATTR_BLINK      },
        { "reverse",    7, ATTR_REVERSE    },
        { "concealed",  8, ATTR_CONCEALED  },
    };
    unsigned int attr_types = 0;

    while (*p)
      {
        const char *s;
        if (!isalnum (*p))
          vfprintf_fail (formats[FMT_GENERIC], "--attr switch must be provided a string");
        s = p;
        while (isalnum (*p))
          p++;
        if (*p != '\0' && *p != ',')
          vfprintf_fail (formats[FMT_GENERIC], "--attr switch must have strings separated by ,");
        else
          {
            bool valid_attr = false;
            unsigned int i;
            for (i = 0; i < sizeof (attrs) / sizeof (struct attr); i++)
              {
                const size_t name_len = strlen (attrs[i].name);
                if ((size_t)(p - s) == name_len && strneq (s, attrs[i].name, name_len))
                  {
                    write_attr (&attrs[i], &attr_types);
                    valid_attr = true;
                    break;
                  }
              }
            if (!valid_attr)
              {
                char *attr_invalid = xmalloc ((p - s) + 1);
                STACK_VAR (attr_invalid);
                strncpy (attr_invalid, s, p - s);
                attr_invalid[p - s] = '\0';
                vfprintf_fail ("--attr switch attribute '%s' is not valid", attr_invalid);
                RELEASE_VAR (attr_invalid); /* never reached */
              }
          }
        if (*p)
          p++;
      }
}

static void
write_attr (const struct attr *attr_i, unsigned int *attr_types)
{
    const unsigned int val = attr_i->val;
    const enum attr_type attr_type = attr_i->type;
    const char *attr_name = attr_i->name;

    if (*attr_types & attr_type)
      vfprintf_fail ("--attr switch has attribute '%s' twice or more", attr_name);
    snprintf (attr + strlen (attr), 3, "%u;", val);
    *attr_types |= attr_type;
}

static void
print_hint (void)
{
    fprintf (stderr, "Type `%s --help' for help screen.\n", program_name);
}

static void
print_help (void)
{
    struct opt_data {
        const char *name;
        const char *short_opt;
        const char *arg;
    };
    const struct opt_data opts_data[] = {
        { "attr",           NULL, "=ATTR1,ATTR2,..." },
        { "exclude-random", NULL, "=COLOR"           },
        { "help",           "h",  NULL               },
        { "version",        "V",  NULL               },
    };
    const struct option *opt = long_opts;
    unsigned int i;

    printf ("Usage: %s (foreground) OR (foreground)%c(background) OR --clean[-all] [-|file]\n\n", program_name, COLOR_SEP_CHAR);
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
    for (; opt->name; opt++)
      {
        const struct opt_data *opt_data = NULL;
        unsigned int i;
        for (i = 0; i < sizeof (opts_data) / sizeof (struct opt_data); i++)
          if (streq (opt->name, opts_data[i].name))
            {
              opt_data = &opts_data[i];
              break;
            }
        if (opt_data)
          {
            if (opt_data->short_opt)
              printf ("\t\t-%s, --%s\n", opt_data->short_opt, opt->name);
            else
              printf ("\t\t    --%s%s\n", opt->name, opt_data->arg);
          }
        else
          printf ("\t\t    --%s\n", opt->name);
      }
    printf ("\n");
}

static void
print_version (void)
{
#ifdef HAVE_VERSION
# include "version.h"
#else
    const char *version = NULL;
#endif
    const char *version_prefix, *version_string;
    const char *c_flags, *ld_flags, *cpp_flags;
    const char *const desc_flags_unknown = "unknown";
    struct bytes_size bytes_size;
    bool debug;
#ifdef CFLAGS
    c_flags = to_str (CFLAGS);
#else
    c_flags = desc_flags_unknown;
#endif
#ifdef LDFLAGS
    ld_flags = to_str (LDFLAGS);
#else
    ld_flags = desc_flags_unknown;
#endif
#ifdef CPPFLAGS
    cpp_flags = to_str (CPPFLAGS);
#else
    cpp_flags = desc_flags_unknown;
#endif
#if DEBUG
    debug = true;
#else
    debug = false;
#endif
    version_prefix = version ? "" : "v";
    version_string = version ? version : VERSION;
    printf ("%s %s%s (compiled at %s, %s)\n", PROGRAM_NAME, version_prefix, version_string, __DATE__, __TIME__);

    printf ("Compiler flags: %s\n", c_flags);
    printf ("Linker flags: %s\n", ld_flags);
    printf ("Preprocessor flags: %s\n", cpp_flags);
    if (get_bytes_size (BUF_SIZE, &bytes_size))
      {
        if (BUF_SIZE % 1024 == 0)
          printf ("Buffer size: %u%c\n", bytes_size.size, bytes_size.unit);
        else
          printf ("Buffer size: %u%c, %u byte%s\n", bytes_size.size, bytes_size.unit,
                   BUF_SIZE % 1024, BUF_SIZE % 1024 > 1 ? "s" : "");
      }
    else
      printf ("Buffer size: %lu byte%s\n", (unsigned long)BUF_SIZE, BUF_SIZE > 1 ? "s" : "");
    printf ("Color separator: '%c'\n", COLOR_SEP_CHAR);
    printf ("Debugging: %s\n", debug ? "yes" : "no");
}

static void
cleanup (void)
{
    if (stream && fileno (stream) != STDIN_FILENO)
      fclose (stream);
#if DEBUG
    if (log)
      fclose (log);
#endif

    if (vars_list)
      {
        unsigned int i;
        for (i = 0; i < stacked_vars; i++)
          free (vars_list[i]);
        free_null (vars_list);
      }
}

static void
free_color_names (struct color_name **color_names)
{
    unsigned int i;
    for (i = 0; color_names[i]; i++)
      {
        RELEASE_VAR (color_names[i]->name);
        RELEASE_VAR (color_names[i]->orig);
        RELEASE_VAR (color_names[i]);
      }
}

static void
process_args (unsigned int arg_cnt, char **arg_strings, char *attr, const struct color **colors, const char **file, FILE **stream)
{
    int ret;
    char *p;
    struct stat sb;
    struct color_name *color_names[3] = { NULL, NULL, NULL };

    const char *color_string = arg_cnt >= 1 ? arg_strings[0] : NULL;
    const char *file_string  = arg_cnt == 2 ? arg_strings[1] : NULL;

    assert (color_string != NULL);

    if (streq (color_string, "-"))
      {
        if (file_string)
          vfprintf_fail (formats[FMT_GENERIC], "hyphen cannot be used as color string");
        else
          vfprintf_fail (formats[FMT_GENERIC], "hyphen must be preceded by color string");
      }

    ret = lstat (color_string, &sb);

    /* Ensure that we don't fail if there's a file with one or more
       color names in its path.  */
    if (ret == 0) /* success */
      skip_path_colors (color_string, file_string, &sb);

    if ((p = strchr (color_string, COLOR_SEP_CHAR)))
      {
        if (p == color_string)
          vfprintf_fail (formats[FMT_STRING], "foreground color missing in string", color_string);
        else if (p == color_string + strlen (color_string) - 1)
          vfprintf_fail (formats[FMT_STRING], "background color missing in string", color_string);
        else if (strchr (++p, COLOR_SEP_CHAR))
          vfprintf_fail (formats[FMT_STRING], "one color pair allowed only for string", color_string);
      }

    gather_color_names (color_string, attr, color_names);

    assert (color_names[FOREGROUND] != NULL);

    if (color_names[BACKGROUND])
      {
        unsigned int i;
        const unsigned int color_sets[2][2] = { { FOREGROUND, BACKGROUND }, { BACKGROUND, FOREGROUND } };
        for (i = 0; i < 2; i++)
          {
            const unsigned int color1 = color_sets[i][0];
            const unsigned int color2 = color_sets[i][1];
            if (CHECK_COLORS_RANDOM (color1, color2))
              vfprintf_fail (formats[FMT_RANDOM], tables[color1].desc, color_names[color1]->orig, "cannot be combined with", color_names[color2]->orig);
          }
      }

    find_color_entries (color_names, colors);
    assert (colors[FOREGROUND] != NULL);
    free_color_names (color_names);

    if (!colors[FOREGROUND]->code && colors[BACKGROUND] && colors[BACKGROUND]->code)
      {
        struct color_name color_name;
        color_name.name = color_name.orig = "default";

        find_color_entry (&color_name, FOREGROUND, colors);
        assert (colors[FOREGROUND]->code != NULL);
      }

    process_file_arg (file_string, file, stream);
}

static void
process_file_arg (const char *file_string, const char **file, FILE **stream)
{
    if (file_string)
      {
        if (streq (file_string, "-"))
          *stream = stdin;
        else
          {
            const char *file = file_string;
            struct stat sb;
            int ret;

            errno = 0;
            ret = stat (file, &sb);

            if (ret == -1)
              vfprintf_fail (formats[FMT_FILE], file, strerror (errno));

            if (!VALID_FILE_TYPE (sb.st_mode))
              vfprintf_fail (formats[FMT_TYPE], file, "unrecognized type", get_file_type (sb.st_mode));

            *stream = open_file (file, "r");
          }
        *file = file_string;
      }
    else
      {
        *stream = stdin;
        *file = "stdin";
      }

    assert (*stream != NULL);
    assert (*file != NULL);
}

static void
skip_path_colors (const char *color_string, const char *file_string, const struct stat *sb)
{
    bool have_file;
    unsigned int c;
    const char *color = color_string;
    const mode_t mode = sb->st_mode;

    for (c = 1; c <= 2 && *color; c++)
      {
        bool matched = false;
        unsigned int i;
        for (i = 0; i < tables[GENERIC].count; i++)
          {
            const struct color *entry = &tables[GENERIC].entries[i];
            if (has_color_name (color, entry->name))
              {
                color += strlen (entry->name);
                matched = true;
                break;
              }
          }
        if (!matched && has_color_name (color, "random"))
          {
            color += strlen ("random");
            matched = true;
          }
        if (matched && *color == COLOR_SEP_CHAR && *(color + 1))
          color++;
        else
          break;
      }

    have_file = (*color != '\0');

    if (have_file)
      {
        const char *file_existing = color_string;
        if (file_string)
          vfprintf_fail (formats[FMT_QUOTE], get_file_type (mode), file_existing, "cannot be used as color string");
        else
          {
            if (VALID_FILE_TYPE (mode))
              vfprintf_fail (formats[FMT_QUOTE], get_file_type (mode), file_existing, "must be preceded by color string");
            else
              vfprintf_fail (formats[FMT_QUOTE], get_file_type (mode), file_existing, "is not a valid file type");
          }
      }
}

static void
gather_color_names (const char *color_string, char *attr, struct color_name **color_names)
{
    unsigned int index;
    char *color, *p, *str;

    str = xstrdup (color_string);
    STACK_VAR (str);

    for (index = 0, color = str; *color; index++, color = p)
      {
        char *ch, *sep;

        p = NULL;
        if ((sep = strchr (color, COLOR_SEP_CHAR)))
          {
            *sep = '\0';
            p = sep + 1;
          }
        else
          p = color + strlen (color);
        assert (p != NULL);

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
                  snprintf (attr + strlen (attr), 3, "1;");
                  break;
                case BACKGROUND:
                  vfprintf_fail (formats[FMT_COLOR], tables[BACKGROUND].desc, color, "cannot be bold");
                default: /* never reached */
                  ABORT_TRACE ();
              }
          }

        color_names[index] = xcalloc (1, sizeof (struct color_name));
        STACK_VAR (color_names[index]);

        color_names[index]->orig = xstrdup (color);
        STACK_VAR (color_names[index]->orig);

        for (ch = color; *ch; ch++)
          *ch = tolower (*ch);

        color_names[index]->name = xstrdup (color);
        STACK_VAR (color_names[index]->name);
      }

    RELEASE_VAR (str);
}

static void
read_print_stream (const char *attr, const struct color **colors, const char *file, FILE *stream)
{
    char buf[BUF_SIZE + 1];
    unsigned int flags = 0;

    while (!feof (stream))
      {
        size_t bytes_read;
        char *eol;
        const char *line;
        bytes_read = fread (buf, 1, BUF_SIZE, stream);
        if (bytes_read != BUF_SIZE && ferror (stream))
          vfprintf_fail (formats[FMT_ERROR], BUF_SIZE, "read");
        buf[bytes_read] = '\0';
        line = buf;
        while ((eol = strpbrk (line, "\n\r")))
          {
            const bool has_text = (eol > line);
            const char *p;
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
            p = eol + SKIP_LINE_ENDINGS (flags);
            *eol = '\0';
            print_line (attr, colors, line, flags,
                        omit_color_empty ? has_text : true);
            line = p;
          }
        if (feof (stream))
          {
            if (*line != '\0')
              print_line (attr, colors, line, 0, true);
          }
        else if (*line != '\0')
          {
            char *p;
            if ((clean || clean_all) && (p = strrchr (line, '\033')))
              merge_print_line (line, p, stream);
            else
              print_line (attr, colors, line, 0, true);
          }
      }
}

static void
merge_print_line (const char *line, const char *p, FILE *stream)
{
    char *buf = NULL;
    char *merged_esc = NULL;
    const char *esc = "";
    const char char_restore = *p;

    complete_part_line (p + 1, &buf, stream);

    if (buf)
      {
        /* form escape sequence */
        esc = merged_esc = str_concat (p, buf);
        /* shorten partial line accordingly */
        *(char *)p = '\0';
        free (buf);
      }

#ifdef TEST_MERGE_PART_LINE
    printf ("%s%s", line, esc);
    fflush (stdout);
    _exit (EXIT_SUCCESS);
#else
    print_clean (line);
    *(char *)p = char_restore;
    print_clean (esc);
    free (merged_esc);
#endif
}

static void
complete_part_line (const char *p, char **buf, FILE *stream)
{
    bool got_next_char = false, read_from_stream;
    char ch;
    size_t i = 0, size;

    if (get_next_char (&ch, &p, stream, &read_from_stream))
      {
        if (ch == '[')
          {
            if (read_from_stream)
              save_char (ch, buf, &i, &size);
          }
        else
          {
            if (read_from_stream)
              ungetc ((int)ch, stream);
            return; /* cancel */
          }
      }
    else
      return; /* cancel */

    while (get_next_char (&ch, &p, stream, &read_from_stream))
      {
        if (isdigit (ch) || ch == ';')
          {
            if (read_from_stream)
              save_char (ch, buf, &i, &size);
          }
        else /* got next character */
          {
            got_next_char = true;
            break;
          }
      }

    if (got_next_char)
      {
        if (ch == 'm')
          {
            if (read_from_stream)
              save_char (ch, buf, &i, &size);
          }
        else
          {
            if (read_from_stream)
              ungetc ((int)ch, stream);
            return; /* cancel */
          }
      }
    else
      return; /* cancel */
}

static bool
get_next_char (char *ch, const char **p, FILE *stream, bool *read_from_stream)
{
    if (**p == '\0')
      {
        int c;
        if ((c = fgetc (stream)) != EOF)
          {
            *ch = (char)c;
            *read_from_stream = true;
            return true;
          }
        else
          {
            *read_from_stream = false;
            return false;
          }
      }
    else
      {
        *ch = **p;
        (*p)++;
        *read_from_stream = false;
        return true;
      }
}

static void
save_char (char ch, char **buf, size_t *i, size_t *size)
{
    if (!*buf)
      {
        *size = ALLOC_COMPLETE_PART_LINE;
        *buf = xmalloc (*size);
      }
    /* +1: effective occupied size of buffer */
    else if ((*i + 1) == *size)
      {
        *size *= 2;
        *buf = xrealloc (*buf, *size);
      }
    (*buf)[*i] = ch;
    (*buf)[*i + 1] = '\0';
    (*i)++;
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
          find_color_entry (color_names[index], index, colors);
      }
}

static void
find_color_entry (const struct color_name *color_name, unsigned int index, const struct color **colors)
{
    bool found = false;
    unsigned int i;

    const unsigned int count                = tables[index].count;
    const struct color *const color_entries = tables[index].entries;

    for (i = 0; i < count; i++)
      if (streq (color_name->name, color_entries[i].name))
        {
          colors[index] = (struct color *)&color_entries[i];
          found = true;
          break;
        }
    if (!found)
      vfprintf_fail (formats[FMT_COLOR], tables[index].desc, color_name->orig, "not recognized");
}

static void
print_line (const char *attr, const struct color **colors, const char *const line, unsigned int flags, bool emit_colors)
{
    /* --clean[-all] */
    if (clean || clean_all)
      print_clean (line);
    /* skip for --omit-color-empty? */
    else if (emit_colors)
      {
        /* Foreground color code is guaranteed to be set when background color code is present.  */
        if (colors[BACKGROUND] && colors[BACKGROUND]->code)
          printf ("\033[%s", colors[BACKGROUND]->code);
        if (colors[FOREGROUND]->code)
          printf ("\033[%s%s%s\033[0m", attr, colors[FOREGROUND]->code, line);
        else
          printf (formats[FMT_GENERIC], line);
      }
    if (flags & CR)
      putchar ('\r');
    if (flags & LF)
      putchar ('\n');
}

static void
print_clean (const char *line)
{
    const char *p = line;

    if (is_esc (p))
      p = get_end_of_esc (p);

    while (*p != '\0')
      {
        const char *text_start = p;
        const char *text_end = get_end_of_text (p);
        print_text (text_start, text_end - text_start);
        p = get_end_of_esc (text_end);
      }
}

static bool
is_esc (const char *p)
{
    return gather_esc_offsets (p, NULL, NULL);
}

static const char *
get_end_of_esc (const char *p)
{
    const char *esc;
    const char *end = NULL;
    while ((esc = strchr (p, '\033')))
      {
        if (gather_esc_offsets (esc, NULL, &end))
          break;
        p = esc + 1;
      }
    return end ? end + 1 : p + strlen (p);
}

static const char *
get_end_of_text (const char *p)
{
    const char *esc;
    const char *start = NULL;
    while ((esc = strchr (p, '\033')))
      {
        if (gather_esc_offsets (esc, &start, NULL))
          break;
        p = esc + 1;
      }
    return start ? start : p + strlen (p);
}

static void
print_text (const char *p, size_t len)
{
    size_t bytes_written;
    bytes_written = fwrite (p, 1, len, stdout);
    if (bytes_written != len)
      vfprintf_fail (formats[FMT_ERROR], (unsigned long)len, "written");
}

static bool
gather_esc_offsets (const char *p, const char **start, const char **end)
{
    /* ESC[ */
    if (*p == 27 && *(p + 1) == '[')
      {
        bool valid = false;
        const char *const begin = p;
        p += 2;
        if (clean_all)
          valid = validate_esc_clean_all (&p);
        else if (clean)
          {
            bool check_values;
            unsigned int prev_iter, iter;
            const char *digit;
            prev_iter = iter = 0;
            do {
              check_values = false;
              iter++;
              if (!isdigit (*p))
                break;
              digit = p;
              while (isdigit (*p))
                p++;
              if (p - digit > 2)
                break;
              else /* check range */
                {
                  char val[3];
                  int value;
                  unsigned int i;
                  const unsigned int digits = p - digit;
                  for (i = 0; i < digits; i++)
                    val[i] = *digit++;
                  val[i] = '\0';
                  value = atoi (val);
                  valid = validate_esc_clean (value, iter, &prev_iter, &p, &check_values);
                }
            } while (check_values);
          }
        if (valid)
          {
            if (start)
              *start = begin;
            if (end)
              *end = p;
            return true;
          }
      }
    return false;
}

static bool
validate_esc_clean_all (const char **p)
{
    while (isdigit (**p) || **p == ';')
      (*p)++;
    return (**p == 'm');
}

static bool
validate_esc_clean (int value, unsigned int iter, unsigned int *prev_iter, const char **p, bool *check_values)
{
    if (is_reset (value, iter, p))
      return true;
    else if (is_attr (value, iter, *prev_iter, p))
      {
        (*p)++;
        *check_values = true;
        *prev_iter = iter;
        return false; /* partial escape sequence, need another valid value */
      }
    else if (is_fg_color (value, p))
      return true;
    else if (is_bg_color (value, iter, p))
      return true;
    else
      return false;
}

static bool
is_reset (int value, unsigned int iter, const char **p)
{
    return (value == 0 && iter == 1 && **p == 'm');
}

static bool
is_attr (int value, unsigned int iter, unsigned int prev_iter, const char **p)
{
    return ((value > 0 && value < 10) && (iter - prev_iter == 1) && **p == ';');
}

static bool
is_fg_color (int value, const char **p)
{
    return (((value >= 30 && value <= 37) || value == 39) && **p == 'm');
}

static bool
is_bg_color (int value, unsigned int iter, const char **p)
{
    return (((value >= 40 && value <= 47) || value == 49) && iter == 1 && **p == 'm');
}

#if !DEBUG
static void *
malloc_wrap (size_t size)
{
    void *p = malloc (size);
    if (!p)
      MEM_ALLOC_FAIL ();
    return p;
}

static void *
calloc_wrap (size_t nmemb, size_t size)
{
    void *p = calloc (nmemb, size);
    if (!p)
      MEM_ALLOC_FAIL ();
    return p;
}

static void *
realloc_wrap (void *ptr, size_t size)
{
    void *p = realloc (ptr, size);
    if (!p)
      MEM_ALLOC_FAIL ();
    return p;
}
#else
static void *
malloc_wrap_debug (size_t size, const char *file, unsigned int line)
{
    void *p = malloc (size);
    if (!p)
      MEM_ALLOC_FAIL_DEBUG (file, line);
    fprintf (log, "%s: malloc'ed %lu bytes [source file %s, line %u]\n", program_name, (unsigned long)size, file, line);
    return p;
}

static void *
calloc_wrap_debug (size_t nmemb, size_t size, const char *file, unsigned int line)
{
    void *p = calloc (nmemb, size);
    if (!p)
      MEM_ALLOC_FAIL_DEBUG (file, line);
    fprintf (log, "%s: calloc'ed %lu bytes [source file %s, line %u]\n", program_name, (unsigned long)(nmemb * size), file, line);
    return p;
}

static void *
realloc_wrap_debug (void *ptr, size_t size, const char *file, unsigned int line)
{
    void *p = realloc (ptr, size);
    if (!p)
      MEM_ALLOC_FAIL_DEBUG (file, line);
    fprintf (log, "%s: realloc'ed %lu bytes [source file %s, line %u]\n", program_name, (unsigned long)size, file, line);
    return p;
}
#endif /* !DEBUG */

static void
free_wrap (void **ptr)
{
    free (*ptr);
    *ptr = NULL;
}

#if !DEBUG
# define do_malloc(len, file, line) malloc_wrap(len)
#else
# define do_malloc(len, file, line) malloc_wrap_debug(len, file, line)
#endif

static char *
strdup_wrap (const char *str, const char *file, unsigned int line)
{
    const size_t len = strlen (str) + 1;
    char *p = do_malloc (len, file, line);
    strncpy (p, str, len);
    return p;
}

static char *
str_concat_wrap (const char *str1, const char *str2, const char *file, unsigned int line)
{
    const size_t len = strlen (str1) + strlen (str2) + 1;
    char *p, *str;

    p = str = do_malloc (len, file, line);
    strncpy (p, str1, strlen (str1));
    p += strlen (str1);
    strncpy (p, str2, strlen (str2));
    p += strlen (str2);
    *p = '\0';

    return str;
}

static bool
get_bytes_size (unsigned long bytes, struct bytes_size *bytes_size)
{
    const char *unit, units[] = { '0', 'K', 'M', 'G', '\0' };
    unsigned long size = bytes;
    if (bytes < 1024)
      return false;
    unit = units;
    while (size >= 1024 && *(unit + 1))
      {
        size /= 1024;
        unit++;
      }
    bytes_size->size = (unsigned int)size;
    bytes_size->unit = *unit;
    return true;
}

static char *
get_file_type (mode_t mode)
{
    if (S_ISREG (mode))
      return "file";
    else if (S_ISDIR (mode))
      return "directory";
    else if (S_ISCHR (mode))
      return "character device";
    else if (S_ISBLK (mode))
      return "block device";
    else if (S_ISFIFO (mode))
      return "named pipe";
    else if (S_ISLNK (mode))
      return "symbolic link";
    else if (S_ISSOCK (mode))
      return "socket";
    else
      return "file";
}

static bool
has_color_name (const char *str, const char *name)
{
    char *p;

    assert (strlen (str) > 0);
    assert (strlen (name) > 0);

    if (!(*str == *name || *str == toupper (*name)))
      return false;
    else if (*(name + 1) != '\0'
     && !((p = strstr (str + 1, name + 1)) && p == str + 1))
      return false;
    else
      return true;
}

static FILE *
open_file (const char *file, const char *mode)
{
    FILE *stream;

    errno = 0;
    stream = fopen (file, mode);
    if (!stream)
      vfprintf_fail (formats[FMT_FILE], file, strerror (errno));

    return stream;
}

#define DO_VFPRINTF(fmt)                    \
    va_list ap;                             \
    fprintf (stderr, "%s: ", program_name); \
    va_start (ap, fmt);                     \
    vfprintf (stderr, fmt, ap);             \
    va_end (ap);                            \
    fprintf (stderr, "\n");

static void
vfprintf_diag (const char *fmt, ...)
{
    DO_VFPRINTF (fmt);
}

static void
vfprintf_fail (const char *fmt, ...)
{
    DO_VFPRINTF (fmt);
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
