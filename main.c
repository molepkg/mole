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
	    "  track             add a local project as a package\n"
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

	printf("\n  " C_BROWN "mole install" C_RESET " %s\n", argv[0]);
	if (den_is_installed(argv[0])) {
		printf("  -> %s " C_DIRT "already in den" C_RESET "\n", argv[0]);
		return 0;
	}

	if (deps_resolve(argv[0], cfg) != 0)
		return 1;

	printf("\n  " C_GREEN "done." C_RESET "\n\n");
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
needs_upgrade(const char *name, const struct config *cfg)
{
	char recipe_path[MAX_PATH];
	struct recipe r;
	char current_hash[MAX_NAME];
	char *stored_hash;

	if (find_recipe(name, cfg, recipe_path, sizeof(recipe_path)) != 0)
		return 1; /* can't find recipe, try anyway */

	if (recipe_parse(recipe_path, &r) != 0)
		return 1;

	/* no stored hash means old install, upgrade it */
	stored_hash = den_get_hash(name);
	if (!stored_hash)
		return 1;

	/* get current source hash */
	if (util_get_source_hash(&r, current_hash, sizeof(current_hash)) != 0)
		return 1;

	/* compare */
	if (strcmp(stored_hash, current_hash) != 0)
		return 1;

	/* also check if version changed in recipe */
	{
		char *installed_ver = den_get_version(name);
		if (installed_ver && strcmp(installed_ver, r.version) != 0)
			return 1;
	}

	return 0;
}

static int
cmd_upgrade(int argc, char **argv, struct config *cfg)
{
	DIR *d;
	struct dirent *ent;
	int upgraded = 0;
	int force = 0;

	/* check for --force */
	for (int i = 0; i < argc; i++) {
		if (strcmp(argv[i], "--force") == 0)
			force = 1;
	}

	if (argc > 0 && strcmp(argv[0], "--force") != 0) {
		/* upgrade single package */
		printf("\n  " C_BROWN "mole upgrade" C_RESET " %s\n", argv[0]);
		if (!den_is_installed(argv[0])) {
			fprintf(stderr, "  error: '%s' is not installed\n", argv[0]);
			return 1;
		}
		if (!force && !needs_upgrade(argv[0], cfg)) {
			printf("  -> %s " C_DIRT "is up to date" C_RESET "\n\n", argv[0]);
			return 0;
		}
		den_remove_quiet(argv[0]);
		return deps_resolve(argv[0], cfg);
	}

	/* upgrade all */
	printf("\n  " C_BROWN "mole upgrade" C_RESET " (all)\n\n");
	d = opendir(DEN_DIR);
	if (!d) {
		printf("  no packages installed\n");
		return 0;
	}

	while ((ent = readdir(d)) != NULL) {
		if (ent->d_name[0] == '.')
			continue;
		if (!force && !needs_upgrade(ent->d_name, cfg)) {
			printf("  -> %s " C_DIRT "is up to date" C_RESET "\n", ent->d_name);
			continue;
		}
		den_remove_quiet(ent->d_name);
		if (deps_resolve(ent->d_name, cfg) != 0) {
			closedir(d);
			return 1;
		}
		upgraded++;
	}

	closedir(d);
	if (upgraded == 0)
		printf("\n  " C_GREEN "everything is up to date." C_RESET "\n\n");
	else
		printf("\n  " C_GREEN "done." C_RESET " %d packages upgraded.\n\n", upgraded);
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

static int
cmd_track(int argc, char **argv, struct config *cfg)
{
	char *name = NULL;
	char *path = NULL;
	char *desc = NULL;
	char recipe_dir[MAX_PATH];
	char recipe_path[MAX_PATH];
	char abs_path[4096];
	FILE *fp;

	for (int i = 0; i < argc; i++) {
		if (strcmp(argv[i], "--name") == 0 && i + 1 < argc)
			name = argv[++i];
		else if (strcmp(argv[i], "--path") == 0 && i + 1 < argc)
			path = argv[++i];
		else if (strcmp(argv[i], "--desc") == 0 && i + 1 < argc)
			desc = argv[++i];
	}

	if (!name || !path) {
		fprintf(stderr, "  usage: mole track --name <name> --path <path> [--desc <desc>]\n");
		return 1;
	}

	if (cfg->repo[0] == '\0') {
		fprintf(stderr, "  error: no REPO set in %s\n", CONFIG_FILE);
		return 1;
	}

	/* resolve to absolute path */
	if (realpath(path, abs_path) == NULL) {
		fprintf(stderr, "  error: path '%s' does not exist\n", path);
		return 1;
	}

	/* check for Makefile */
	{
		char makefile[MAX_PATH];
		snprintf(makefile, sizeof(makefile), "%s/Makefile", abs_path);
		if (access(makefile, F_OK) != 0) {
			fprintf(stderr, "  error: no Makefile found in '%s'\n", abs_path);
			return 1;
		}
	}

	/* create recipe dir */
	snprintf(recipe_dir, sizeof(recipe_dir), "%s/core/%s", cfg->repo, name);
	util_mkdir_p(recipe_dir);

	/* write recipe */
	snprintf(recipe_path, sizeof(recipe_path), "%s/recipe", recipe_dir);
	fp = fopen(recipe_path, "w");
	if (!fp) {
		fprintf(stderr, "  error: cannot write recipe\n");
		return 1;
	}

	fprintf(fp, "name=%s\n", name);
	fprintf(fp, "version=git\n");
	if (desc)
		fprintf(fp, "desc=%s\n", desc);
	fprintf(fp, "source=local:%s\n", abs_path);
	fprintf(fp, "deps=\n");
	fprintf(fp, "build=make -j{{JOBS}}\n");
	fprintf(fp, "install=make DESTDIR={{DESTDIR}} PREFIX={{PREFIX}} install\n");
	fclose(fp);

	printf("  " C_GREEN "tracked" C_RESET " %s -> %s\n", name, abs_path);
	printf("  recipe at %s\n", recipe_path);
	printf("  run 'mole install %s' to build and install\n", name);
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
	} else if (strcmp(argv[1], "track") == 0)
		return cmd_track(argc - 2, argv + 2, &cfg);
	else if (strcmp(argv[1], "version") == 0) {
		printf("mole %s\n", MOLE_VERSION);
		return 0;
	} else {
		fprintf(stderr, "  error: unknown command '%s'\n", argv[1]);
		usage();
	}

	return 0;
}
