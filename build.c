#include "mole.h"
#include <sys/wait.h>
#include <time.h>

static int
is_local(const struct recipe *r)
{
	return strncmp(r->source, "local:", 6) == 0;
}

static int
is_git(const struct recipe *r)
{
	return strncmp(r->source, "git:", 4) == 0;
}

static const char *
source_url(const struct recipe *r)
{
	if (is_local(r))
		return r->source + 6;
	if (is_git(r))
		return r->source + 4;
	return r->source;
}

static const char *
source_filename(const struct recipe *r)
{
	const char *url, *slash;

	url = source_url(r);
	slash = strrchr(url, '/');
	return slash ? slash + 1 : url;
}

int
build_fetch_source(const struct recipe *r)
{
	char dst[MAX_PATH];
	char cmd[MAX_CMD];
	char builddir[MAX_PATH];

	if (is_local(r)) {
		printf("     local      %s\n", source_url(r));
		return 0;
	}

	if (is_git(r)) {
		snprintf(builddir, sizeof(builddir), "%s/%s", BUILD_DIR, r->name);
		printf("     cloning    %s\n", source_url(r));
		snprintf(cmd, sizeof(cmd), "git clone --depth 1 '%s' '%s'",
		         source_url(r), builddir);
		return util_run(cmd);
	}

	/* remote tarball */
	snprintf(dst, sizeof(dst), "%s/%s", SOURCE_DIR, source_filename(r));

	/* skip if already cached */
	if (access(dst, F_OK) == 0) {
		printf("     " C_DIRT "cached" C_RESET "     %s\n", source_filename(r));
		return 0;
	}

	printf("     " C_BROWN "fetching" C_RESET "   %s\n", source_filename(r));
	snprintf(cmd, sizeof(cmd), "curl -sfL -o '%s' '%s'", dst, source_url(r));
	return util_run(cmd);
}

int
build_verify(const struct recipe *r)
{
	char dst[MAX_PATH];
	char hash[65];

	if (is_local(r) || is_git(r))
		return 0;

	if (r->sha256[0] == '\0')
		return 0;

	snprintf(dst, sizeof(dst), "%s/%s", SOURCE_DIR, source_filename(r));

	printf("     " C_DIRT "verifying" C_RESET "  sha256");
	if (util_sha256_file(dst, hash) != 0) {
		printf(" " C_RED "FAILED" C_RESET " (could not hash)\n");
		return -1;
	}

	if (strcmp(hash, r->sha256) != 0) {
		printf(" " C_RED "FAILED" C_RESET "\n");
		fprintf(stderr, "     expected: %s\n", r->sha256);
		fprintf(stderr, "     got:      %s\n", hash);
		return -1;
	}

	printf(" " C_GREEN "ok" C_RESET "\n");
	return 0;
}

int
build_extract(const struct recipe *r)
{
	char src[MAX_PATH];
	char builddir[MAX_PATH];
	char cmd[MAX_CMD];

	snprintf(builddir, sizeof(builddir), "%s/%s", BUILD_DIR, r->name);

	if (is_local(r) || is_git(r))
		return 0; /* already in place */

	util_mkdir_p(builddir);

	snprintf(src, sizeof(src), "%s/%s", SOURCE_DIR, source_filename(r));
	printf("     " C_DIRT "extracting" C_RESET " %s\n", source_filename(r));

	/* extract and strip one leading directory component */
	snprintf(cmd, sizeof(cmd),
	         "tar -xf '%s' -C '%s' --strip-components=1", src, builddir);
	return util_run(cmd);
}

static int
count_sources(const char *dir)
{
	char cmd[MAX_CMD];
	char line[MAX_LINE];
	FILE *fp;
	int count = 0;

	snprintf(cmd, sizeof(cmd),
	         "find '%s' -name '*.c' -o -name '*.S' -o -name '*.cc' -o -name '*.cpp' | wc -l",
	         dir);
	fp = popen(cmd, "r");
	if (!fp)
		return 0;
	if (fgets(line, sizeof(line), fp))
		count = atoi(line);
	pclose(fp);
	return count;
}

static int
count_compiled(const char *logfile)
{
	char cmd[MAX_CMD];
	char line[MAX_LINE];
	FILE *fp;
	int count = 0;

	/* count lines that look like compilation */
	snprintf(cmd, sizeof(cmd),
	         "grep -cE '(^  (CC|AS|LD|AR) |^(gcc|g\\+\\+|cc|clang|cpp) |-c -o .+\\.o)' '%s' 2>/dev/null || echo 0",
	         logfile);
	fp = popen(cmd, "r");
	if (!fp)
		return 0;
	if (fgets(line, sizeof(line), fp))
		count = atoi(line);
	pclose(fp);
	return count;
}

static void
format_time(int secs, char *buf, size_t sz)
{
	if (secs < 60)
		snprintf(buf, sz, "%ds", secs);
	else if (secs < 3600)
		snprintf(buf, sz, "%dm%02ds", secs / 60, secs % 60);
	else
		snprintf(buf, sz, "%dh%02dm", secs / 3600, (secs % 3600) / 60);
}

static void
draw_progress(int compiled, int total, int elapsed)
{
	char timebuf[32];
	int barwidth = 30;
	int filled, i;
	float pct;

	format_time(elapsed, timebuf, sizeof(timebuf));

	/* line 1: stats */
	printf("\r\033[K     " C_AMBER "building" C_RESET "   %d/%d compiled, " C_DIM "%s" C_RESET, compiled, total, timebuf);
	/* line 2: progress bar */
	printf("\n\033[K     ");
	if (total > 0) {
		pct = (float)compiled / total;
		if (pct > 1.0f) pct = 1.0f;
		filled = (int)(pct * barwidth);

		printf(C_DIM "[" C_RESET);
		for (i = 0; i < barwidth; i++)
			printf(i < filled ? C_AMBER "#" C_RESET : C_DIM "-" C_RESET);
		printf(C_DIM "]" C_RESET);
	}
	/* move cursor back up one line */
	printf("\033[A");
	fflush(stdout);
}

int
build_compile(const struct recipe *r, const struct config *cfg)
{
	char builddir[MAX_PATH];
	char logfile[MAX_PATH];
	char expanded[MAX_CMD];
	char cmd[MAX_CMD];
	pid_t pid;
	int status, ret;
	int total, compiled;
	time_t start;

	if (is_local(r))
		snprintf(builddir, sizeof(builddir), "%s", source_url(r));
	else
		snprintf(builddir, sizeof(builddir), "%s/%s", BUILD_DIR, r->name);

	snprintf(logfile, sizeof(logfile), "%s/%s-build.log", BUILD_DIR, r->name);

	util_template(expanded, sizeof(expanded), r->build, cfg, "");

	/* count source files for progress */
	total = count_sources(builddir);

	/* create/truncate log */
	fclose(fopen(logfile, "w"));

	snprintf(cmd, sizeof(cmd), "cd '%s' && (%s) >>'%s' 2>&1", builddir, expanded, logfile);

	start = time(NULL);
	pid = fork();
	if (pid == 0) {
		/* child: run the build */
		execl("/bin/sh", "sh", "-c", cmd, NULL);
		_exit(127);
	} else if (pid < 0) {
		fprintf(stderr, "  error: fork failed\n");
		return -1;
	}

	/* parent: hide cursor and draw progress */
	printf("\033[?25l");
	fflush(stdout);
	draw_progress(0, total, 0);
	while (waitpid(pid, &status, WNOHANG) == 0) {
		usleep(250000); /* 250ms */
		compiled = count_compiled(logfile);
		draw_progress(compiled, total, (int)(time(NULL) - start));
	}

	/* final update — clear both progress lines and print summary */
	compiled = count_compiled(logfile);
	{
		char timebuf[32];
		format_time((int)(time(NULL) - start), timebuf, sizeof(timebuf));
		/* clear stats line and bar line */
		printf("\r\033[K\033[B\033[K\033[A");
		printf("\r     " C_GREEN "built" C_RESET "      %d compiled in " C_DIM "%s" C_RESET "\033[?25h\n", compiled, timebuf);
	}

	if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
		ret = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
		/* dump last 20 lines on failure */
		printf("\033[?25h\n     " C_RED "BUILD FAILED" C_RESET " -- last 20 lines:\n");
		printf("     -------\n");
		snprintf(cmd, sizeof(cmd), "tail -20 '%s' | sed 's/^/     /'", logfile);
		util_run(cmd);
		printf("     -------\n");
		printf("     full log: %s\n", logfile);
		return ret;
	}

	return 0;
}

int
build_stage(const struct recipe *r, const struct config *cfg)
{
	char builddir[MAX_PATH];
	char stagedir[MAX_PATH];
	char logfile[MAX_PATH];
	char expanded[MAX_CMD];
	char cmd[MAX_CMD];
	char line[MAX_LINE];
	FILE *fp;
	int ret, count = 0;

	if (is_local(r))
		snprintf(builddir, sizeof(builddir), "%s", source_url(r));
	else
		snprintf(builddir, sizeof(builddir), "%s/%s", BUILD_DIR, r->name);

	snprintf(stagedir, sizeof(stagedir), "%s/%s-stage", BUILD_DIR, r->name);
	snprintf(logfile, sizeof(logfile), "%s/%s-build.log", BUILD_DIR, r->name);
	util_mkdir_p(stagedir);

	util_template(expanded, sizeof(expanded), r->install, cfg, stagedir);

	snprintf(cmd, sizeof(cmd), "cd '%s' && %s", builddir, expanded);
	ret = util_run_quiet(cmd, logfile);
	if (ret != 0)
		return ret;

	/* count staged files */
	snprintf(cmd, sizeof(cmd), "find '%s' -type f", stagedir);
	fp = popen(cmd, "r");
	if (fp) {
		while (fgets(line, sizeof(line), fp))
			count++;
		pclose(fp);
	}
	printf("     " C_DIRT "staging" C_RESET "    %d files\n", count);

	return 0;
}

int
build_install(const struct recipe *r, const struct config *cfg)
{
	char stagedir[MAX_PATH];
	char builddir[MAX_PATH];
	int ret;

	/* fetch */
	util_mkdir_p(SOURCE_DIR);
	util_mkdir_p(BUILD_DIR);

	ret = build_fetch_source(r);
	if (ret != 0)
		return ret;

	/* verify */
	ret = build_verify(r);
	if (ret != 0)
		return ret;

	/* extract */
	ret = build_extract(r);
	if (ret != 0)
		return ret;

	/* compile */
	ret = build_compile(r, cfg);
	if (ret != 0)
		return ret;

	/* stage */
	ret = build_stage(r, cfg);
	if (ret != 0)
		return ret;

	/* install staged files to real filesystem */
	snprintf(stagedir, sizeof(stagedir), "%s/%s-stage", BUILD_DIR, r->name);
	ret = util_copy_tree(stagedir, "/");
	if (ret != 0) {
		fprintf(stderr, "\n  error: permission denied installing to %s\n", cfg->prefix);
		fprintf(stderr, "  try: sudo mole install %s\n", r->name);
		return ret;
	}

	/* record in den */
	ret = den_record(r->name, r->version, r, stagedir);
	if (ret != 0) {
		fprintf(stderr, "  error: failed to record in den\n");
		return ret;
	}

	/* store source hash for change detection */
	{
		char hash[MAX_NAME];
		if (util_get_source_hash(r, hash, sizeof(hash)) == 0)
			den_write_hash(r->name, hash);
	}

	/* cleanup build dir (keep sources cached) */
	snprintf(builddir, sizeof(builddir), "%s/%s", BUILD_DIR, r->name);
	util_rm_rf(builddir);
	util_rm_rf(stagedir);

	printf("     " C_GREEN "installed" C_RESET "  %s %s\n", r->name, r->version);
	return 0;
}
