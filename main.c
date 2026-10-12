#include "mole.h"

static void
usage(void)
{
	fprintf(stderr,
	    "usage: mole <command> [args]\n"
	    "\n"
	    "commands:\n"
	    "  install <name>    build and install a package\n"
	    "  remove <name>     remove an installed package\n"
	    "  upgrade [name]    upgrade one or all packages\n"
	    "  list              list installed packages\n"
	    "  info <name>       show package details\n"
	    "  search <query>    search available recipes\n"
	    "  sync              update the recipe warren\n"
	    "  owns <path>       find which package owns a file\n"
	    "  version           show mole version\n");
	exit(1);
}

static int
cmd_install(int argc, char **argv, struct config *cfg)
{
	if (argc < 1) {
		fprintf(stderr, "  error: install requires a package name\n");
		return 1;
	}

	printf("\n  mole install %s\n", argv[0]);
	if (den_is_installed(argv[0])) {
		printf("  -> %s already in den\n", argv[0]);
		return 0;
	}

	if (deps_resolve(argv[0], cfg) != 0)
		return 1;

	printf("\n  done.\n\n");
	return 0;
}

static int
cmd_remove(int argc, char **argv, struct config *cfg)
{
	int force = 0;

	(void)cfg;

	if (argc < 1) {
		fprintf(stderr, "  error: remove requires a package name\n");
		return 1;
	}

	/* check for --force */
	for (int i = 1; i < argc; i++) {
		if (strcmp(argv[i], "--force") == 0)
			force = 1;
	}

	/* check reverse deps */
	if (!force && deps_reverse(argv[0])) {
		fprintf(stderr, "  use --force to remove anyway\n");
		return 1;
	}

	return den_remove(argv[0]);
}

static int
cmd_upgrade(int argc, char **argv, struct config *cfg)
{
	DIR *d;
	struct dirent *ent;

	if (argc > 0) {
		/* upgrade single package */
		printf("\n  mole upgrade %s\n", argv[0]);
		if (!den_is_installed(argv[0])) {
			fprintf(stderr, "  error: '%s' is not installed\n", argv[0]);
			return 1;
		}
		den_remove_quiet(argv[0]);
		return deps_resolve(argv[0], cfg);
	}

	/* upgrade all */
	printf("\n  mole upgrade (all)\n\n");
	d = opendir(DEN_DIR);
	if (!d) {
		printf("  no packages installed\n");
		return 0;
	}

	while ((ent = readdir(d)) != NULL) {
		if (ent->d_name[0] == '.')
			continue;
		den_remove_quiet(ent->d_name);
		if (deps_resolve(ent->d_name, cfg) != 0) {
			closedir(d);
			return 1;
		}
	}

	closedir(d);
	printf("\n  done.\n\n");
	return 0;
}

static int
generate_index(struct config *cfg)
{
	char indexpath[MAX_PATH];
	char cmd[MAX_CMD];
	char line[MAX_PATH];
	FILE *fp, *idx;
	struct recipe r;
	int count = 0;

	snprintf(indexpath, sizeof(indexpath), "%s/INDEX", cfg->repo);
	idx = fopen(indexpath, "w");
	if (!idx) {
		fprintf(stderr, "  error: cannot write index\n");
		return -1;
	}

	/* find all recipe files */
	snprintf(cmd, sizeof(cmd), "find '%s' -name recipe -type f | sort", cfg->repo);
	fp = popen(cmd, "r");
	if (!fp) {
		fclose(idx);
		return -1;
	}

	while (fgets(line, sizeof(line), fp)) {
		line[strcspn(line, "\n")] = '\0';
		if (recipe_parse(line, &r) == 0 && r.name[0]) {
			fprintf(idx, "%s|%s|%s\n", r.name, r.version, r.desc);
			count++;
		}
	}

	pclose(fp);
	fclose(idx);
	printf("  indexed %d packages\n", count);
	return 0;
}

static int
cmd_sync(struct config *cfg)
{
	char cmd[MAX_CMD];
	int ret;

	if (cfg->repo[0] == '\0') {
		fprintf(stderr, "  error: no REPO set in %s\n", CONFIG_FILE);
		return 1;
	}

	/* if repo dir exists, try pull; otherwise clone */
	if (access(cfg->repo, F_OK) == 0) {
		char gitdir[MAX_PATH];
		snprintf(gitdir, sizeof(gitdir), "%s/.git", cfg->repo);
		if (access(gitdir, F_OK) == 0) {
			printf("  syncing warren...\n");
			snprintf(cmd, sizeof(cmd), "cd '%s' && git pull", cfg->repo);
			util_run(cmd);
		}
	} else {
		printf("  cloning warren...\n");
		snprintf(cmd, sizeof(cmd), "git clone '%s' '%s'", cfg->repo, cfg->repo);
		ret = util_run(cmd);
		if (ret != 0)
			return ret;
	}

	return generate_index(cfg);
}

static int
cmd_search(int argc, char **argv, struct config *cfg)
{
	char indexpath[MAX_PATH];
	char line[MAX_LINE];
	char *name, *version, *desc;
	FILE *fp;
	int found = 0;

	if (argc < 1) {
		fprintf(stderr, "  error: search requires a query\n");
		return 1;
	}

	if (cfg->repo[0] == '\0') {
		fprintf(stderr, "  error: no REPO set in %s\n", CONFIG_FILE);
		return 1;
	}

	snprintf(indexpath, sizeof(indexpath), "%s/INDEX", cfg->repo);
	fp = fopen(indexpath, "r");
	if (!fp) {
		fprintf(stderr, "  error: no index found, run 'mole sync' first\n");
		return 1;
	}

	while (fgets(line, sizeof(line), fp)) {
		line[strcspn(line, "\n")] = '\0';

		/* parse name|version|desc */
		name = line;
		version = strchr(name, '|');
		if (!version) continue;
		*version++ = '\0';
		desc = strchr(version, '|');
		if (!desc) continue;
		*desc++ = '\0';

		/* match query against name or description */
		if (strcasestr(name, argv[0]) || strcasestr(desc, argv[0])) {
			printf("  %s %s", name, version);
			if (desc[0])
				printf(" - %s", desc);
			printf("\n");
			found++;
		}
	}

	fclose(fp);
	if (!found)
		printf("  no packages matching '%s'\n", argv[0]);
	return 0;
}

int
main(int argc, char **argv)
{
	struct config cfg;

	if (argc < 2)
		usage();

	config_parse(CONFIG_FILE, &cfg);

	if (strcmp(argv[1], "install") == 0)
		return cmd_install(argc - 2, argv + 2, &cfg);
	else if (strcmp(argv[1], "remove") == 0)
		return cmd_remove(argc - 2, argv + 2, &cfg);
	else if (strcmp(argv[1], "upgrade") == 0)
		return cmd_upgrade(argc - 2, argv + 2, &cfg);
	else if (strcmp(argv[1], "list") == 0)
		return den_list();
	else if (strcmp(argv[1], "info") == 0) {
		if (argc < 3) {
			fprintf(stderr, "  error: info requires a package name\n");
			return 1;
		}
		return den_info(argv[2]);
	} else if (strcmp(argv[1], "search") == 0)
		return cmd_search(argc - 2, argv + 2, &cfg);
	else if (strcmp(argv[1], "sync") == 0)
		return cmd_sync(&cfg);
	else if (strcmp(argv[1], "owns") == 0) {
		if (argc < 3) {
			fprintf(stderr, "  error: owns requires a file path\n");
			return 1;
		}
		return den_owns(argv[2]);
	} else if (strcmp(argv[1], "version") == 0) {
		printf("mole %s\n", MOLE_VERSION);
		return 0;
	} else {
		fprintf(stderr, "  error: unknown command '%s'\n", argv[1]);
		usage();
	}

	return 0;
}
