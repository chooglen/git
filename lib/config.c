#include "lib/config.h"
#include "gettext.h"
#include "utf8.h"

/* FIXME this is copied from config.c. This should be unduped at some point. */
static inline int iskeychar(int c)
{
	return isalnum(c) || c == '-';
}

static int get_next_char(struct config_source *cs)
{
	int c = cs->do_fgetc(cs);

	if (c == '\r') {
		/* DOS like systems */
		c = cs->do_fgetc(cs);
		if (c != '\n') {
			if (c != EOF)
				cs->do_ungetc(c, cs);
			c = '\r';
		}
	}

	if (c != EOF && ++cs->total_len > INT_MAX) {
		/*
		 * This is an absurdly long config file; refuse to parse
		 * further in order to protect downstream code from integer
		 * overflows. Note that we can't return an error specifically,
		 * but we can mark EOF and put trash in the return value,
		 * which will trigger a parse error.
		 */
		cs->eof = 1;
		return 0;
	}

	if (c == '\n')
		cs->linenr++;
	if (c == EOF) {
		cs->eof = 1;
		cs->linenr++;
		c = '\n';
	}
	return c;
}

static char *parse_value(struct config_source *cs)
{
	int quote = 0, comment = 0, space = 0;

	strbuf_reset(&cs->value);
	for (;;) {
		int c = get_next_char(cs);
		if (c == '\n') {
			if (quote) {
				cs->linenr--;
				return NULL;
			}
			return cs->value.buf;
		}
		if (comment)
			continue;
		if (isspace(c) && !quote) {
			if (cs->value.len)
				space++;
			continue;
		}
		if (!quote) {
			if (c == ';' || c == '#') {
				comment = 1;
				continue;
			}
		}
		for (; space; space--)
			strbuf_addch(&cs->value, ' ');
		if (c == '\\') {
			c = get_next_char(cs);
			switch (c) {
			case '\n':
				continue;
			case 't':
				c = '\t';
				break;
			case 'b':
				c = '\b';
				break;
			case 'n':
				c = '\n';
				break;
			/* Some characters escape as themselves */
			case '\\': case '"':
				break;
			/* Reject unknown escape sequences */
			default:
				return NULL;
			}
			strbuf_addch(&cs->value, c);
			continue;
		}
		if (c == '"') {
			quote = 1-quote;
			continue;
		}
		strbuf_addch(&cs->value, c);
	}
}

static int get_value(struct config_source *cs, config_fn_t fn, void *data,
		     struct strbuf *name)
{
	int c;
	char *value;
	int ret;

	/* Get the full name */
	for (;;) {
		c = get_next_char(cs);
		if (cs->eof)
			break;
		if (!iskeychar(c))
			break;
		strbuf_addch(name, tolower(c));
	}

	while (c == ' ' || c == '\t')
		c = get_next_char(cs);

	value = NULL;
	if (c != '\n') {
		if (c != '=')
			return -1;
		value = parse_value(cs);
		if (!value)
			return -1;
	}
	/*
	 * We already consumed the \n, but we need linenr to point to
	 * the line we just parsed during the call to fn to get
	 * accurate line number in error messages.
	 */
	cs->linenr--;
	ret = fn(name->buf, value, data);
	if (ret >= 0)
		cs->linenr++;
	return ret;
}

static int get_extended_base_var(struct config_source *cs, struct strbuf *name,
				 int c)
{
	cs->subsection_case_sensitive = 0;
	do {
		if (c == '\n')
			goto error_incomplete_line;
		c = get_next_char(cs);
	} while (isspace(c));

	/* We require the format to be '[base "extension"]' */
	if (c != '"')
		return -1;
	strbuf_addch(name, '.');

	for (;;) {
		int c = get_next_char(cs);
		if (c == '\n')
			goto error_incomplete_line;
		if (c == '"')
			break;
		if (c == '\\') {
			c = get_next_char(cs);
			if (c == '\n')
				goto error_incomplete_line;
		}
		strbuf_addch(name, c);
	}

	/* Final ']' */
	if (get_next_char(cs) != ']')
		return -1;
	return 0;
error_incomplete_line:
	cs->linenr--;
	return -1;
}

static int get_base_var(struct config_source *cs, struct strbuf *name)
{
	cs->subsection_case_sensitive = 1;
	for (;;) {
		int c = get_next_char(cs);
		if (cs->eof)
			return -1;
		if (c == ']')
			return 0;
		if (isspace(c))
			return get_extended_base_var(cs, name, c);
		if (!iskeychar(c) && c != '.')
			return -1;
		strbuf_addch(name, tolower(c));
	}
}

struct parse_event_data {
	enum config_event_t previous_type;
	size_t previous_offset;
	const struct config_options *opts;
};

static int do_event(struct config_source *cs, enum config_event_t type,
		    struct parse_event_data *data)
{
	size_t offset;

	if (!data->opts || !data->opts->event_fn)
		return 0;

	if (type == CONFIG_EVENT_WHITESPACE &&
	    data->previous_type == type)
		return 0;

	offset = cs->do_ftell(cs);
	/*
	 * At EOF, the parser always "inserts" an extra '\n', therefore
	 * the end offset of the event is the current file position, otherwise
	 * we will already have advanced to the next event.
	 */
	if (type != CONFIG_EVENT_EOF)
		offset--;

	if (data->previous_type != CONFIG_EVENT_EOF &&
	    data->opts->event_fn(data->previous_type, data->previous_offset,
				 offset, data->opts->event_fn_data) < 0)
		return -1;

	data->previous_type = type;
	data->previous_offset = offset;

	return 0;
}

static int git_parse_source(struct config_source *cs, config_fn_t fn, void *data,
			    const struct config_options *opts)
{
	int comment = 0;
	size_t baselen = 0;
	struct strbuf *var = &cs->var;
	int error_return = 0;
	char *error_msg = NULL;

	/* U+FEFF Byte Order Mark in UTF8 */
	const char *bomptr = utf8_bom;

	/* For the parser event callback */
	struct parse_event_data event_data = {
		CONFIG_EVENT_EOF, 0, opts
	};

	for (;;) {
		int c;

		c = get_next_char(cs);
		if (bomptr && *bomptr) {
			/* We are at the file beginning; skip UTF8-encoded BOM
			 * if present. Sane editors won't put this in on their
			 * own, but e.g. Windows Notepad will do it happily. */
			if (c == (*bomptr & 0377)) {
				bomptr++;
				continue;
			} else {
				/* Do not tolerate partial BOM. */
				if (bomptr != utf8_bom)
					break;
				/* No BOM at file beginning. Cool. */
				bomptr = NULL;
			}
		}
		if (c == '\n') {
			if (cs->eof) {
				if (do_event(cs, CONFIG_EVENT_EOF, &event_data) < 0)
					return -1;
				return 0;
			}
			if (do_event(cs, CONFIG_EVENT_WHITESPACE, &event_data) < 0)
				return -1;
			comment = 0;
			continue;
		}
		if (comment)
			continue;
		if (isspace(c)) {
			if (do_event(cs, CONFIG_EVENT_WHITESPACE, &event_data) < 0)
					return -1;
			continue;
		}
		if (c == '#' || c == ';') {
			if (do_event(cs, CONFIG_EVENT_COMMENT, &event_data) < 0)
					return -1;
			comment = 1;
			continue;
		}
		if (c == '[') {
			if (do_event(cs, CONFIG_EVENT_SECTION, &event_data) < 0)
					return -1;

			/* Reset prior to determining a new stem */
			strbuf_reset(var);
			if (get_base_var(cs, var) < 0 || var->len < 1)
				break;
			strbuf_addch(var, '.');
			baselen = var->len;
			continue;
		}
		if (!isalpha(c))
			break;

		if (do_event(cs, CONFIG_EVENT_ENTRY, &event_data) < 0)
			return -1;

		/*
		 * Truncate the var name back to the section header
		 * stem prior to grabbing the suffix part of the name
		 * and the value.
		 */
		strbuf_setlen(var, baselen);
		strbuf_addch(var, tolower(c));
		if (get_value(cs, fn, data, var) < 0)
			break;
	}

	if (do_event(cs, CONFIG_EVENT_ERROR, &event_data) < 0)
		return -1;

	switch (cs->origin_type) {
	case CONFIG_ORIGIN_BLOB:
		error_msg = xstrfmt(_("bad config line %d in blob %s"),
				      cs->linenr, cs->name);
		break;
	case CONFIG_ORIGIN_FILE:
		error_msg = xstrfmt(_("bad config line %d in file %s"),
				      cs->linenr, cs->name);
		break;
	case CONFIG_ORIGIN_STDIN:
		error_msg = xstrfmt(_("bad config line %d in standard input"),
				      cs->linenr);
		break;
	case CONFIG_ORIGIN_SUBMODULE_BLOB:
		error_msg = xstrfmt(_("bad config line %d in submodule-blob %s"),
				       cs->linenr, cs->name);
		break;
	case CONFIG_ORIGIN_CMDLINE:
		error_msg = xstrfmt(_("bad config line %d in command line %s"),
				       cs->linenr, cs->name);
		break;
	default:
		error_msg = xstrfmt(_("bad config line %d in %s"),
				      cs->linenr, cs->name);
	}

	switch (opts && opts->error_action ?
		opts->error_action :
		cs->default_error_action) {
	case CONFIG_ERROR_DIE:
		die("%s", error_msg);
		break;
	case CONFIG_ERROR_ERROR:
		error_return = error("%s", error_msg);
		break;
	case CONFIG_ERROR_SILENT:
		error_return = -1;
		break;
	case CONFIG_ERROR_UNSET:
		BUG("config error action unset");
	}

	free(error_msg);
	return error_return;
}

static int config_file_fgetc(struct config_source *conf)
{
	return getc_unlocked(conf->u.file);
}

static int config_file_ungetc(int c, struct config_source *conf)
{
	return ungetc(c, conf->u.file);
}

static long config_file_ftell(struct config_source *conf)
{
	return ftell(conf->u.file);
}

static int config_buf_fgetc(struct config_source *conf)
{
	if (conf->u.buf.pos < conf->u.buf.len)
		return conf->u.buf.buf[conf->u.buf.pos++];

	return EOF;
}

static int config_buf_ungetc(int c, struct config_source *conf)
{
	if (conf->u.buf.pos > 0) {
		conf->u.buf.pos--;
		if (conf->u.buf.buf[conf->u.buf.pos] != c)
			BUG("config_buf can only ungetc the same character");
		return c;
	}

	return EOF;
}

static long config_buf_ftell(struct config_source *conf)
{
	return conf->u.buf.pos;
}

static void config_source_init(struct config_source *source)
{
	source->linenr = 1;
	source->eof = 0;
	source->total_len = 0;
	strbuf_init(&source->value, 1024);
	strbuf_init(&source->var, 1024);
}

static void config_source_release(struct config_source *source)
{
	strbuf_release(&source->value);
	strbuf_release(&source->var);
}

static void config_source_init_file(struct config_source *source,
				    const enum config_origin_type origin_type,
				    const char *name, const char *path, FILE *f)
{
	config_source_init(source);
	source->u.file = f;
	source->origin_type = origin_type;
	source->name = name;
	source->path = path;
	source->default_error_action = CONFIG_ERROR_DIE;
	source->do_fgetc = config_file_fgetc;
	source->do_ungetc = config_file_ungetc;
	source->do_ftell = config_file_ftell;
}

static void config_source_init_mem(struct config_source *source,
				   const enum config_origin_type origin_type,
				   const char *name, const char *buf,
				   size_t len)
{
	config_source_init(source);
	source->u.buf.buf = buf;
	source->u.buf.len = len;
	source->u.buf.pos = 0;
	source->origin_type = origin_type;
	source->name = name;
	source->path = NULL;
	source->default_error_action = CONFIG_ERROR_ERROR;
	source->do_fgetc = config_buf_fgetc;
	source->do_ungetc = config_buf_ungetc;
	source->do_ftell = config_buf_ftell;
}

/*
 * All source specific fields in the union, die_on_error, name and the callbacks
 * fgetc, ungetc, ftell of top need to be initialized before calling
 * this function.
 */
static int do_config_from(struct config_source *top, config_fn_t fn, void *data,
			  const struct config_options *opts,
			  config_parser_source_event_fn_t before_parse,
			  config_parser_source_event_fn_t after_parse)
{
	int ret;

	if (before_parse)
		before_parse(top);

	ret = git_parse_source(top, fn, data, opts);

	if (after_parse)
		after_parse(top);
	config_source_release(top);

	return ret;
}

static int do_config_from_file(config_fn_t fn,
			       const enum config_origin_type origin_type,
			       const char *name, const char *path, FILE *f,
			       void *data, const struct config_options *opts,
			       config_parser_source_event_fn_t before_parse,
			       config_parser_source_event_fn_t after_parse)
{
	struct config_source top = CONFIG_SOURCE_INIT;
	int ret;

	config_source_init_file(&top, origin_type, name, path, f);
	flockfile(f);
	ret = do_config_from(&top, fn, data, opts, before_parse, after_parse);
	funlockfile(f);
	return ret;
}

int git_config_from_stdin_lib(config_fn_t fn, void *data,
			      config_parser_source_event_fn_t before_parse,
			      config_parser_source_event_fn_t after_parse)
{
	return do_config_from_file(fn, CONFIG_ORIGIN_STDIN, "", NULL, stdin,
				   data, NULL, before_parse, after_parse);
}

int git_config_from_file_with_options_lib(config_fn_t fn,const char *filename,
					  void *data,
					  const struct config_options *opts,
					  config_parser_source_event_fn_t before_parse,
					  config_parser_source_event_fn_t after_parse)
{
	int ret = -1;
	FILE *f;

	if (!filename)
		BUG("filename cannot be NULL");
	f = fopen_or_warn(filename, "r");
	if (f) {
		ret = do_config_from_file(fn, CONFIG_ORIGIN_FILE, filename,
					  filename, f, data, opts, before_parse,
					  after_parse);
		fclose(f);
	}
	return ret;
}

int git_config_from_mem_lib(config_fn_t fn,
			    const enum config_origin_type origin_type,
			    const char *name, const char *buf, size_t len,
			    void *data, const struct config_options *opts,
			    config_parser_source_event_fn_t before_parse,
			    config_parser_source_event_fn_t after_parse)
{
	int ret;
	struct config_source top = CONFIG_SOURCE_INIT;

	config_source_init_mem(&top, origin_type, name, buf, len);

	ret = do_config_from(&top, fn, data, opts, before_parse, after_parse);

	return ret;
}
