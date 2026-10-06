#ifndef MOLE_H
#define MOLE_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <dirent.h>
#include <errno.h>

#define MOLE_VERSION "0.1.0"

#define DEN_DIR      "/var/pkg/installed"
#define CACHE_DIR    "/var/cache/pkg"
#define SOURCE_DIR   "/var/cache/pkg/sources"
#define BUILD_DIR    "/var/cache/pkg/build"
#define CONFIG_FILE  "/etc/pkg/config"

#define MAX_DEPS     64
#define MAX_NAME     128
#define MAX_PATH     2048
#define MAX_LINE     4096
#define MAX_CMD      8192

/* recipe — parsed from a burrow file */
struct recipe {
	char name[MAX_NAME];
	char version[MAX_NAME];
	char desc[MAX_LINE];
	char source[MAX_PATH];
	char sha256[65];
	char deps[MAX_DEPS][MAX_NAME];
	int ndeps;
	char build[MAX_CMD];
	char install[MAX_CMD];
};

/* global config — parsed from /etc/pkg/config */
struct config {
	char prefix[MAX_PATH];
	char repo[MAX_PATH];
	char cflags[MAX_CMD];
	int jobs;
};

/* recipe.c */
int recipe_parse(const char *path, struct recipe *r);
int recipe_validate(const struct recipe *r);

/* build.c */
int build_install(const struct recipe *r, const struct config *cfg);
int build_fetch_source(const struct recipe *r);
int build_verify(const struct recipe *r);
int build_extract(const struct recipe *r);
int build_compile(const struct recipe *r, const struct config *cfg);
int build_stage(const struct recipe *r, const struct config *cfg);

/* deps.c */
int deps_resolve(const char *name, const struct config *cfg);
int deps_check_cycle(const char *name, const char **stack, int depth);
int deps_reverse(const char *name);

/* den.c */
int den_is_installed(const char *name);
int den_record(const char *name, const char *version, const struct recipe *r,
               const char *stagedir);
int den_remove(const char *name);
int den_remove_quiet(const char *name);
int den_list(void);
int den_info(const char *name);
int den_owns(const char *path);
char *den_get_version(const char *name);

/* util.c */
int util_run(const char *cmd);
int util_run_quiet(const char *cmd, const char *logfile);
int util_mkdir_p(const char *path);
int util_rm_rf(const char *path);
int util_copy_tree(const char *src, const char *dst);
int util_sha256_file(const char *path, char *out);
void util_template(char *dst, size_t dstsz, const char *src,
                   const struct config *cfg, const char *destdir);
int config_parse(const char *path, struct config *cfg);

#endif
