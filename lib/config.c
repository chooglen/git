#include "lib/config.h"

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

void config_source_init_file(struct config_source *source,
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

void config_source_init_mem(struct config_source *source,
			    const enum config_origin_type origin_type,
			    const char *name, const char *buf, size_t len)
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
