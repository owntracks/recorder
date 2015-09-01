/*
 * Copyright (C) 2015 Jan-Piet Mens <jpmens@gmail.com> and OwnTracks
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * JAN-PIET MENS OR OWNTRACKS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

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
