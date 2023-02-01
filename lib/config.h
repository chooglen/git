#ifndef GIT_LIB_CONFIG_H
#define GIT_LIB_CONFIG_H

#include "git-compat-util.h"
#include "strbuf.h"

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
 * FIXME these will be removed when the do_config_from_*() machinery is moved to
 * lib/config.c.
 */
void config_source_init_file(struct config_source *source,
			     const enum config_origin_type origin_type,
			     const char *name, const char *path, FILE *f);
void config_source_init_mem(struct config_source *source,
			    const enum config_origin_type origin_type,
			    const char *name, const char *buf, size_t len);
#endif /* GIT_LIB_CONFIG_H */
