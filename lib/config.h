#ifndef GIT_LIB_CONFIG_H
#define GIT_LIB_CONFIG_H

#include "git-compat-util.h"
#include "strbuf.h"

enum config_event_t {
	CONFIG_EVENT_SECTION,
	CONFIG_EVENT_ENTRY,
	CONFIG_EVENT_WHITESPACE,
	CONFIG_EVENT_COMMENT,
	CONFIG_EVENT_EOF,
	CONFIG_EVENT_ERROR
};

/*
 * The parser event function (if not NULL) is called with the event type and
 * the begin/end offsets of the parsed elements.
 *
 * Note: for CONFIG_EVENT_ENTRY (i.e. config variables), the trailing newline
 * character is considered part of the element.
 */
typedef int (*config_parser_event_fn_t)(enum config_event_t type,
					size_t begin_offset, size_t end_offset,
					void *event_fn_data);

/**
 * A config callback function takes three parameters:
 *
 * - the name of the parsed variable. This is in canonical "flat" form: the
 *   section, subsection, and variable segments will be separated by dots,
 *   and the section and variable segments will be all lowercase. E.g.,
 *   `core.ignorecase`, `diff.SomeType.textconv`.
 *
 * - the value of the found variable, as a string. If the variable had no
 *   value specified, the value will be NULL (typically this means it
 *   should be interpreted as boolean true).
 *
 * - a void pointer passed in by the caller of the config API; this can
 *   contain callback-specific data
 *
 * A config callback should return 0 for success, or -1 if the variable
 * could not be parsed properly.
 */
typedef int (*config_fn_t)(const char *, const char *, void *);


enum config_origin_type {
	CONFIG_ORIGIN_UNKNOWN = 0,
	CONFIG_ORIGIN_BLOB,
	CONFIG_ORIGIN_FILE,
	CONFIG_ORIGIN_STDIN,
	CONFIG_ORIGIN_SUBMODULE_BLOB,
	CONFIG_ORIGIN_CMDLINE
};

enum config_error_action {
	CONFIG_ERROR_UNSET = 0, /* use source-specific default */
	CONFIG_ERROR_DIE, /* die() on error */
	CONFIG_ERROR_ERROR, /* error() on error, return -1 */
	CONFIG_ERROR_SILENT, /* return -1 */
};

struct config_options {
	unsigned int respect_includes : 1;
	unsigned int ignore_repo : 1;
	unsigned int ignore_worktree : 1;
	unsigned int ignore_cmdline : 1;
	unsigned int system_gently : 1;

	/*
	 * For internal use. Include all includeif.hasremoteurl paths without
	 * checking if the repo has that remote URL, and when doing so, verify
	 * that files included in this way do not configure any remote URLs
	 * themselves.
	 */
	unsigned int unconditional_remote_url : 1;

	const char *commondir;
	const char *git_dir;
	/*
	 * FIXME I'd have loved to put the before_parse() and after_parse()
	 * functions on this struct, but it's quite badly overloaded, e.g. all
	 * of the options above are only used by config_with_options(). Perhaps
	 * I'll do this after some refactoring.
	 */
	config_parser_event_fn_t event_fn;
	void *event_fn_data;
	enum config_error_action error_action;
};

struct config_source {
	struct config_source *prev;
	union {
		FILE *file;
		struct config_buf {
			const char *buf;
			size_t len;
			size_t pos;
		} buf;
	} u;
	enum config_origin_type origin_type;
	const char *name;
	const char *path;
	enum config_error_action default_error_action;
	int linenr;
	int eof;
	size_t total_len;
	struct strbuf value;
	struct strbuf var;
	unsigned subsection_case_sensitive : 1;

	int (*do_fgetc)(struct config_source *c);
	int (*do_ungetc)(int c, struct config_source *conf);
	long (*do_ftell)(struct config_source *c);
};
#define CONFIG_SOURCE_INIT { 0 }

/*
 * Called when the config source is initialized (just before parsing) and when
 * the parser done with it. This gives callers a chance to store a reference to
 * the config_source to read relevant properties, e.g. reading the line number
 * to give helpful diagnostics to users.
 */
typedef void(*config_parser_source_event_fn_t)(struct config_source *);

int git_config_from_mem_lib(config_fn_t fn,
			    const enum config_origin_type origin_type,
			    const char *name, const char *buf,
			    size_t len, void *data,
			    const struct config_options *opts,
			    config_parser_source_event_fn_t before_parse,
			    config_parser_source_event_fn_t after_parse);

int git_config_from_file_with_options_lib(config_fn_t fn,
					  const char *filename,
					  void *data,
					  const struct config_options *opts,
					  config_parser_source_event_fn_t before_parse,
					  config_parser_source_event_fn_t after_parse);

int git_config_from_stdin_lib(config_fn_t fn, void *data,
			      config_parser_source_event_fn_t before_parse,
			      config_parser_source_event_fn_t after_parse);
#endif /* GIT_LIB_CONFIG_H */
