#include "mole.h"
#include <sys/wait.h>
#include <fcntl.h>

int
util_run(const char *cmd)
{
	int status;

	status = system(cmd);
	if (status == -1)
		return -1;
	if (WIFEXITED(status))
		return WEXITSTATUS(status);
	return -1;
}

int
util_run_quiet(const char *cmd, const char *logfile)
{
	char wrapped[MAX_CMD];
	int ret;

	snprintf(wrapped, sizeof(wrapped), "(%s) >>'%s' 2>&1", cmd, logfile);
	ret = util_run(wrapped);
	if (ret != 0) {
		/* dump last 20 lines on failure */
		char tail[MAX_CMD];
		printf("\n     BUILD FAILED — last 20 lines:\n");
		printf("     -------\n");
		snprintf(tail, sizeof(tail), "tail -20 '%s' | sed 's/^/     /'", logfile);
		util_run(tail);
		printf("     -------\n");
		printf("     full log: %s\n", logfile);
	}
	return ret;
}

int
util_mkdir_p(const char *path)
{
	char tmp[MAX_PATH];
	char *p;
	size_t len;

	snprintf(tmp, sizeof(tmp), "%s", path);
	len = strlen(tmp);
	if (len > 0 && tmp[len - 1] == '/')
		tmp[len - 1] = '\0';

	for (p = tmp + 1; *p; p++) {
		if (*p == '/') {
			*p = '\0';
			if (mkdir(tmp, 0755) == -1 && errno != EEXIST)
				return -1;
			*p = '/';
		}
	}
	return mkdir(tmp, 0755) == -1 && errno != EEXIST ? -1 : 0;
}

int
util_rm_rf(const char *path)
{
	char cmd[MAX_CMD];

	snprintf(cmd, sizeof(cmd), "rm -rf '%s'", path);
	return util_run(cmd);
}

int
util_copy_tree(const char *src, const char *dst)
{
	char cmd[MAX_CMD];

	snprintf(cmd, sizeof(cmd), "cp -a --remove-destination '%s'/. '%s'", src, dst);
	return util_run(cmd);
}

int
util_sha256_file(const char *path, char *out)
{
	char cmd[MAX_CMD];
	char line[MAX_LINE];
	FILE *fp;

	/* try sha256sum (linux), fall back to shasum (macos) */
	snprintf(cmd, sizeof(cmd), "sha256sum '%s' 2>/dev/null || shasum -a 256 '%s' 2>/dev/null", path, path);
	fp = popen(cmd, "r");
	if (!fp)
		return -1;

	if (!fgets(line, sizeof(line), fp)) {
		pclose(fp);
		return -1;
	}
	pclose(fp);

	/* output is "hash  filename\n" */
	if (strlen(line) < 64)
		return -1;
	memcpy(out, line, 64);
	out[64] = '\0';
	return 0;
}

void
util_template(char *dst, size_t dstsz, const char *src,
              const struct config *cfg, const char *destdir)
{
	const char *p;
	char *d, *end;
	char jobs[16];

	snprintf(jobs, sizeof(jobs), "%d", cfg->jobs);
	d = dst;
	end = dst + dstsz - 1;

	for (p = src; *p && d < end; ) {
		if (strncmp(p, "{{PREFIX}}", 10) == 0) {
			d += snprintf(d, end - d, "%s", cfg->prefix);
			p += 10;
		} else if (strncmp(p, "{{DESTDIR}}", 11) == 0) {
			d += snprintf(d, end - d, "%s", destdir);
			p += 11;
		} else if (strncmp(p, "{{JOBS}}", 8) == 0) {
			d += snprintf(d, end - d, "%s", jobs);
			p += 8;
		} else if (strncmp(p, "{{CFLAGS}}", 10) == 0) {
			d += snprintf(d, end - d, "%s", cfg->cflags);
			p += 10;
		} else {
			*d++ = *p++;
		}
	}
	*d = '\0';
}

int
config_parse(const char *path, struct config *cfg)
{
	FILE *fp;
	char line[MAX_LINE];
	char *eq;

	/* defaults */
	snprintf(cfg->prefix, sizeof(cfg->prefix), "/usr/local");
	cfg->repo[0] = '\0';
	snprintf(cfg->cflags, sizeof(cfg->cflags), "-O2 -pipe");
	cfg->jobs = 4;

	fp = fopen(path, "r");
	if (!fp)
		return 0; /* no config is fine, use defaults */

	while (fgets(line, sizeof(line), fp)) {
		/* strip newline */
		line[strcspn(line, "\n")] = '\0';

		/* skip empty lines and comments */
		if (line[0] == '\0' || line[0] == '#')
			continue;

		eq = strchr(line, '=');
		if (!eq)
			continue;
		*eq = '\0';

		if (strcmp(line, "PREFIX") == 0)
			snprintf(cfg->prefix, sizeof(cfg->prefix), "%s", eq + 1);
		else if (strcmp(line, "REPO") == 0)
			snprintf(cfg->repo, sizeof(cfg->repo), "%s", eq + 1);
		else if (strcmp(line, "CFLAGS") == 0)
			snprintf(cfg->cflags, sizeof(cfg->cflags), "%s", eq + 1);
		else if (strcmp(line, "JOBS") == 0)
			cfg->jobs = atoi(eq + 1);
	}

	fclose(fp);
	return 0;
}

int
util_get_source_hash(const struct recipe *r, char *out, size_t outsz)
{
	char cmd[MAX_CMD];
	char line[MAX_LINE];
	FILE *fp;
	const char *src;

	/* for local: sources, get git HEAD if it's a git repo */
	if (strncmp(r->source, "local:", 6) == 0) {
		src = r->source + 6;
		snprintf(cmd, sizeof(cmd),
		         "git -C '%s' rev-parse HEAD 2>/dev/null", src);
		fp = popen(cmd, "r");
		if (fp && fgets(line, sizeof(line), fp)) {
			line[strcspn(line, "\n")] = '\0';
			if (strlen(line) >= 7) {
				snprintf(out, outsz, "%s", line);
				pclose(fp);
				return 0;
			}
		}
		if (fp) pclose(fp);

		/* not a git repo, use mtime of Makefile as fallback */
		snprintf(cmd, sizeof(cmd),
		         "stat -c %%Y '%s/Makefile' 2>/dev/null || stat -f %%m '%s/Makefile' 2>/dev/null",
		         src, src);
		fp = popen(cmd, "r");
		if (fp && fgets(line, sizeof(line), fp)) {
			line[strcspn(line, "\n")] = '\0';
			snprintf(out, outsz, "mtime:%s", line);
			pclose(fp);
			return 0;
		}
		if (fp) pclose(fp);
	}

	/* for git: sources, same as local but path would be build dir */
	if (strncmp(r->source, "git:", 4) == 0) {
		snprintf(out, outsz, "git");
		return 0;
	}

	/* for remote sources, version is the hash */
	snprintf(out, outsz, "%s", r->version);
	return 0;
}
