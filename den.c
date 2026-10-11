#include "mole.h"

int
den_is_installed(const char *name)
{
	char path[MAX_PATH];

	snprintf(path, sizeof(path), "%s/%s/version", DEN_DIR, name);
	return access(path, F_OK) == 0;
}

char *
den_get_version(const char *name)
{
	static char ver[MAX_NAME];
	char path[MAX_PATH];
	FILE *fp;

	snprintf(path, sizeof(path), "%s/%s/version", DEN_DIR, name);
	fp = fopen(path, "r");
	if (!fp)
		return NULL;

	if (!fgets(ver, sizeof(ver), fp)) {
		fclose(fp);
		return NULL;
	}
	fclose(fp);
	ver[strcspn(ver, "\n")] = '\0';
	return ver;
}

int
den_record(const char *name, const char *version, const struct recipe *r,
           const char *stagedir)
{
	char dir[MAX_PATH];
	char path[MAX_PATH];
	char cmd[MAX_CMD];
	char recipe_path[MAX_PATH];
	FILE *fp;

	/* create den entry directory */
	snprintf(dir, sizeof(dir), "%s/%s", DEN_DIR, name);
	util_mkdir_p(dir);

	/* write version */
	snprintf(path, sizeof(path), "%s/version", dir);
	fp = fopen(path, "w");
	if (!fp)
		return -1;
	fprintf(fp, "%s\n", version);
	fclose(fp);

	/* write manifest — list of installed files */
	snprintf(path, sizeof(path), "%s/manifest", dir);
	snprintf(cmd, sizeof(cmd), "find '%s' -type f | sed 's|^%s||' > '%s'",
	         stagedir, stagedir, path);
	util_run(cmd);

	/* copy recipe */
	snprintf(recipe_path, sizeof(recipe_path), "%s/recipe", dir);
	fp = fopen(recipe_path, "w");
	if (!fp)
		return -1;
	fprintf(fp, "name=%s\n", r->name);
	fprintf(fp, "version=%s\n", r->version);
	fprintf(fp, "source=%s\n", r->source);
	if (r->sha256[0])
		fprintf(fp, "sha256=%s\n", r->sha256);
	if (r->ndeps > 0) {
		fprintf(fp, "deps=");
		for (int i = 0; i < r->ndeps; i++) {
			if (i > 0)
				fprintf(fp, ",");
			fprintf(fp, "%s", r->deps[i]);
		}
		fprintf(fp, "\n");
	}
	fprintf(fp, "build=%s\n", r->build);
	fprintf(fp, "install=%s\n", r->install);
	fclose(fp);

	return 0;
}

static int
den_remove_internal(const char *name, int quiet)
{
	char manifest_path[MAX_PATH];
	char den_path[MAX_PATH];
	char line[MAX_PATH];
	FILE *fp;
	int count = 0;

	if (!den_is_installed(name)) {
		fprintf(stderr, "  error: '%s' is not installed\n", name);
		return -1;
	}

	snprintf(manifest_path, sizeof(manifest_path), "%s/%s/manifest",
	         DEN_DIR, name);
	fp = fopen(manifest_path, "r");
	if (!fp) {
		fprintf(stderr, "  error: cannot read manifest for '%s'\n", name);
		return -1;
	}

	/* delete each installed file */
	while (fgets(line, sizeof(line), fp)) {
		line[strcspn(line, "\n")] = '\0';
		if (line[0] == '\0')
			continue;
		if (unlink(line) == 0)
			count++;
	}
	fclose(fp);

	/* remove den entry */
	snprintf(den_path, sizeof(den_path), "%s/%s", DEN_DIR, name);
	util_rm_rf(den_path);

	if (!quiet)
		printf("  removed %s (%d files)\n", name, count);
	return 0;
}

int
den_remove(const char *name)
{
	return den_remove_internal(name, 0);
}

int
den_remove_quiet(const char *name)
{
	return den_remove_internal(name, 1);
}

int
den_list(void)
{
	DIR *d;
	struct dirent *ent;
	char *ver;

	d = opendir(DEN_DIR);
	if (!d) {
		printf("  no packages installed\n");
		return 0;
	}

	while ((ent = readdir(d)) != NULL) {
		if (ent->d_name[0] == '.')
			continue;
		ver = den_get_version(ent->d_name);
		printf("  %s %s\n", ent->d_name, ver ? ver : "?");
	}

	closedir(d);
	return 0;
}

int
den_info(const char *name)
{
	char path[MAX_PATH];
	struct recipe r;
	char *ver;
	char cmd[MAX_CMD];

	if (!den_is_installed(name)) {
		fprintf(stderr, "  error: '%s' is not installed\n", name);
		return -1;
	}

	ver = den_get_version(name);

	snprintf(path, sizeof(path), "%s/%s/recipe", DEN_DIR, name);
	if (recipe_parse(path, &r) != 0)
		return -1;

	printf("  name     %s\n", r.name);
	printf("  version  %s\n", ver ? ver : r.version);
	if (r.ndeps > 0) {
		printf("  roots    ");
		for (int i = 0; i < r.ndeps; i++) {
			if (i > 0)
				printf(", ");
			printf("%s", r.deps[i]);
		}
		printf("\n");
	}

	/* count files */
	snprintf(path, sizeof(path), "%s/%s/manifest", DEN_DIR, name);
	snprintf(cmd, sizeof(cmd), "wc -l < '%s'", path);
	printf("  files    ");
	fflush(stdout);
	util_run(cmd);

	printf("  den      %s/%s/\n", DEN_DIR, name);
	return 0;
}

int
den_owns(const char *filepath)
{
	DIR *d;
	struct dirent *ent;
	char manifest[MAX_PATH];
	char line[MAX_PATH];
	FILE *fp;
	int found = 0;

	d = opendir(DEN_DIR);
	if (!d) {
		printf("  no packages installed\n");
		return 0;
	}

	while ((ent = readdir(d)) != NULL) {
		if (ent->d_name[0] == '.')
			continue;

		snprintf(manifest, sizeof(manifest), "%s/%s/manifest",
		         DEN_DIR, ent->d_name);
		fp = fopen(manifest, "r");
		if (!fp)
			continue;

		while (fgets(line, sizeof(line), fp)) {
			line[strcspn(line, "\n")] = '\0';
			if (strcmp(line, filepath) == 0) {
				char *ver = den_get_version(ent->d_name);
				printf("  %s %s\n", ent->d_name, ver ? ver : "?");
				found = 1;
				break;
			}
		}
		fclose(fp);
	}

	closedir(d);
	if (!found)
		printf("  no package owns '%s'\n", filepath);
	return 0;
}
