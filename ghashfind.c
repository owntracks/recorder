#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ftw.h>
#include "util.h"

long counter = 0L;

int fn(const char *filename, const struct stat *sb, int flag)
{
	JsonNode *obj, *node;
	char ghash[64], *p, *bp, *gp, *js;

	if (flag != FTW_F)
		return (0);

	if (strstr(filename, ".json") == NULL)
		return (0);

	/* Find ghash from filename */
	if ((p = strrchr(filename, '/')) == NULL) {
		return (0);
	}
	for (gp = ghash, bp = p + 1; bp && *bp && *bp != '.'; bp++) {
		*gp++ = *bp;
	}
	*gp = 0;
	if (!strcmp(ghash, "7zzzzzz"))
		return (0);
	
	obj = json_mkobject();
	if (json_copy_from_file(obj, (char *)filename) != TRUE) {
		json_delete(obj);
		return (1);
	}

	json_foreach(node, obj) {
		if (strcmp(node->key, "addr") == 0 || strcmp(node->key, "cc") == 0)
			continue;
		json_remove_from_parent(node);
	}

	if ((js = json_stringify(obj, NULL)) == NULL) {
			json_delete(obj);
			return (1);
	}
	++counter;
	printf("%s %s\n", ghash, js);
	free(js);
	json_delete(obj);

	return (0);
}

int main(int argc, char **argv)
{
	char *ghash_dir;

	if (argc != 2) {
		fprintf(stderr, "Usage: %s ghashdirectory\n", *argv);
		return (-1);
	}
	ghash_dir = argv[1];

	if (ftw(ghash_dir, fn, 10) != 0) {
		fprintf(stderr, "%s tree walk failed\n", *argv);
		return (2);
	}

	fprintf(stderr, "%s: processed %ld files\n", *argv, counter);
	return (0);
}
