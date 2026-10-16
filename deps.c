#include "mole.h"

int
find_recipe(const char *name, const struct config *cfg, char *out, size_t outsz)
{
	char path[MAX_PATH];

	/* check repo/core/<name>/recipe then repo/extra/<name>/recipe */
	snprintf(path, sizeof(path), "%s/core/%s/recipe", cfg->repo, name);
	if (access(path, F_OK) == 0) {
		snprintf(out, outsz, "%s", path);
		return 0;
	}

	snprintf(path, sizeof(path), "%s/extra/%s/recipe", cfg->repo, name);
	if (access(path, F_OK) == 0) {
		snprintf(out, outsz, "%s", path);
		return 0;
	}

	fprintf(stderr, "  error: no recipe found for '%s'\n", name);
	return -1;
}

int
deps_check_cycle(const char *name, const char **stack, int depth)
{
	for (int i = 0; i < depth; i++) {
		if (strcmp(stack[i], name) == 0) {
			fprintf(stderr, "  error: circular dependency detected: ");
			for (int j = i; j < depth; j++)
				fprintf(stderr, "%s -> ", stack[j]);
			fprintf(stderr, "%s\n", name);
			return -1;
		}
	}
	return 0;
}

static int
resolve_recursive(const char *name, const struct config *cfg,
                  const char **stack, int depth)
{
	char recipe_path[MAX_PATH];
	struct recipe r;
	int ret;

	/* cycle check */
	if (deps_check_cycle(name, stack, depth) != 0)
		return -1;

	/* already installed — skip */
	if (den_is_installed(name)) {
		printf("  -> %s " C_DIRT "(already installed)" C_RESET "\n", name);
		return 0;
	}

	/* find and parse recipe */
	if (find_recipe(name, cfg, recipe_path, sizeof(recipe_path)) != 0)
		return -1;

	if (recipe_parse(recipe_path, &r) != 0)
		return -1;

	if (recipe_validate(&r) != 0)
		return -1;

	/* push onto stack */
	stack[depth] = name;

	/* resolve deps first (depth-first) */
	for (int i = 0; i < r.ndeps; i++) {
		ret = resolve_recursive(r.deps[i], cfg, stack, depth + 1);
		if (ret != 0)
			return ret;
	}

	/* print dep chain */
	printf("  -> " C_BROWN "%s" C_RESET " %s\n", r.name, r.version);

	/* build and install */
	ret = build_install(&r, cfg);
	if (ret != 0) {
		printf("\n  aborted.\n");
		return ret;
	}

	return 0;
}

int
deps_resolve(const char *name, const struct config *cfg)
{
	const char *stack[256];
	char recipe_path[MAX_PATH];
	struct recipe r;

	/* print root deps for display */
	if (find_recipe(name, cfg, recipe_path, sizeof(recipe_path)) != 0)
		return -1;
	if (recipe_parse(recipe_path, &r) != 0)
		return -1;

	if (r.ndeps > 0) {
		printf("\n  -> resolving roots...\n");
		printf("     %s -> ", name);
		for (int i = 0; i < r.ndeps; i++) {
			if (i > 0)
				printf(", ");
			printf("%s", r.deps[i]);
		}
		printf("\n\n");
	}

	return resolve_recursive(name, cfg, stack, 0);
}

int
deps_reverse(const char *name)
{
	DIR *d;
	struct dirent *ent;
	char path[MAX_PATH];
	struct recipe r;
	int found = 0;

	d = opendir(DEN_DIR);
	if (!d)
		return 0;

	while ((ent = readdir(d)) != NULL) {
		if (ent->d_name[0] == '.')
			continue;
		if (strcmp(ent->d_name, name) == 0)
			continue;

		snprintf(path, sizeof(path), "%s/%s/recipe", DEN_DIR, ent->d_name);
		if (recipe_parse(path, &r) != 0)
			continue;

		for (int i = 0; i < r.ndeps; i++) {
			if (strcmp(r.deps[i], name) == 0) {
				if (!found)
					fprintf(stderr, "  warning: the following packages depend on '%s':\n", name);
				fprintf(stderr, "    %s\n", ent->d_name);
				found = 1;
				break;
			}
		}
	}

	closedir(d);
	return found;
}
