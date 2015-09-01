/* ghash2lmdb: read key<space>value separated lines and add them to an LMDB database */
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

	gc = gcache_open(argv[1], 0);
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
