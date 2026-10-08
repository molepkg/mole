#include "mole.h"

int
recipe_parse(const char *path, struct recipe *r)
{
	FILE *fp;
	char line[MAX_LINE];
	char *eq;

	memset(r, 0, sizeof(*r));

	fp = fopen(path, "r");
	if (!fp) {
		fprintf(stderr, "  error: cannot open recipe '%s'\n", path);
		return -1;
	}

	while (fgets(line, sizeof(line), fp)) {
		line[strcspn(line, "\n")] = '\0';

		if (line[0] == '\0' || line[0] == '#')
			continue;

		eq = strchr(line, '=');
		if (!eq)
			continue;
		*eq = '\0';

		if (strcmp(line, "name") == 0)
			snprintf(r->name, sizeof(r->name), "%s", eq + 1);
		else if (strcmp(line, "version") == 0)
			snprintf(r->version, sizeof(r->version), "%s", eq + 1);
		else if (strcmp(line, "desc") == 0)
			snprintf(r->desc, sizeof(r->desc), "%s", eq + 1);
		else if (strcmp(line, "source") == 0)
			snprintf(r->source, sizeof(r->source), "%s", eq + 1);
		else if (strcmp(line, "sha256") == 0)
			snprintf(r->sha256, sizeof(r->sha256), "%s", eq + 1);
		else if (strcmp(line, "deps") == 0) {
			/* comma or space separated dep list */
			char *tok, *s;
			s = eq + 1;
			while ((tok = strsep(&s, ", ")) != NULL) {
				if (tok[0] == '\0')
					continue;
				if (r->ndeps >= MAX_DEPS) {
					fprintf(stderr, "  error: too many deps in '%s'\n", path);
					fclose(fp);
					return -1;
				}
				snprintf(r->deps[r->ndeps], MAX_NAME, "%s", tok);
				r->ndeps++;
			}
		} else if (strcmp(line, "build") == 0)
			snprintf(r->build, sizeof(r->build), "%s", eq + 1);
		else if (strcmp(line, "install") == 0)
			snprintf(r->install, sizeof(r->install), "%s", eq + 1);
	}

	fclose(fp);
	return 0;
}

int
recipe_validate(const struct recipe *r)
{
	int ok = 1;

	if (r->name[0] == '\0') {
		fprintf(stderr, "  error: recipe missing 'name'\n");
		ok = 0;
	}
	if (r->version[0] == '\0') {
		fprintf(stderr, "  error: recipe missing 'version'\n");
		ok = 0;
	}
	if (r->source[0] == '\0') {
		fprintf(stderr, "  error: recipe missing 'source'\n");
		ok = 0;
	}
	if (r->build[0] == '\0') {
		fprintf(stderr, "  error: recipe missing 'build'\n");
		ok = 0;
	}
	if (r->install[0] == '\0') {
		fprintf(stderr, "  error: recipe missing 'install'\n");
		ok = 0;
	}

	/* sha256 required for remote sources, not for local: or git: */
	if (r->sha256[0] == '\0' &&
	    strncmp(r->source, "local:", 6) != 0 &&
	    strncmp(r->source, "git:", 4) != 0) {
		fprintf(stderr, "  error: recipe missing 'sha256' for remote source\n");
		ok = 0;
	}

	/* check for self-dependency */
	for (int i = 0; i < r->ndeps; i++) {
		if (strcmp(r->deps[i], r->name) == 0) {
			fprintf(stderr, "  error: recipe '%s' depends on itself\n", r->name);
			ok = 0;
		}
	}

	return ok ? 0 : -1;
}
