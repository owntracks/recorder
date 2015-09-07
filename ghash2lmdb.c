/* ghash2lmdb: read key<space>value separated lines and add them to an LMDB database */
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
#include <string.h>
#include "gcache.h"

int main(int argc,char * argv[])
{
	char buf[2048], *bp;
	struct gcache *gc;

	if (argc != 2) {
		fprintf(stderr, "Usage: %s /path/to/store\n", *argv);
		return (1);
	}

	gc = gcache_open(argv[1], NULL, 0);
	if (gc == NULL) {
		fprintf(stderr, "%s Cannot open ghash store at %s\n", argv[0], argv[1]);
		return (1);
	}


	while (fgets(buf, sizeof(buf), stdin) != NULL) {

		if ((bp = strchr(buf, '\n')) != NULL)
			*bp = 0;

		if ((bp = strchr(buf, ' ')) != NULL) {
			*bp = 0;

			gcache_put(gc, buf, bp+1);
		}
	}

	gcache_close(gc);
	return (0);
}
