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
#include <unistd.h>
#include <stdlib.h>
#include "gcache.h"
#include "util.h"

/*
 * dbname is an named LMDB database; may be NULL.
 */

struct gcache *gcache_open(char *path, char *dbname, int rdonly)
{
	MDB_txn *txn = NULL;
	int rc;
	unsigned int flags = 0, dbiflags = 0, perms = 0664;
	struct gcache *gc;

	if (!is_directory(path)) {
		olog(LOG_ERR, "gcache_open: %s is not a directory", path);
		return (NULL);
	}

	if ((gc = malloc(sizeof (struct gcache))) == NULL)
		return (NULL);

	memset(gc, 0, sizeof(struct gcache));

	if (rdonly) {
		flags |= MDB_RDONLY;
		perms = 0444;
		perms = 0664;
	} else {
		dbiflags = MDB_CREATE;
	}

	rc = mdb_env_create(&gc->env);
	if (rc != 0) {
		olog(LOG_ERR, "gcache_open: mdb_env_create: %s", mdb_strerror(rc));
		free(gc);
		return (NULL);
	}

	mdb_env_set_mapsize(gc->env, LMDB_DB_SIZE);

	rc = mdb_env_set_maxdbs(gc->env, 10);
	if (rc != 0) {
		olog(LOG_ERR, "gcache_open: mdb_env_set_maxdbs%s", mdb_strerror(rc));
		free(gc);
		return (NULL);
	}

	rc = mdb_env_open(gc->env, path, flags, perms);
	if (rc != 0) {
		olog(LOG_ERR, "gcache_open: mdb_env_open: %s", mdb_strerror(rc));
		free(gc);
		return (NULL);
	}

	/* Open a pseudo TX so that we can open DBI */

	mdb_txn_begin(gc->env, NULL, flags, &txn);
	if (rc != 0) {
		olog(LOG_ERR, "gcache_open: mdb_txn_begin: %s", mdb_strerror(rc));
		mdb_env_close(gc->env);
		free(gc);
		return (NULL);
	}

	rc = mdb_dbi_open(txn, dbname, dbiflags, &gc->dbi);
	if (rc != 0) {
		olog(LOG_ERR, "gcache_open: mdb_dbi_open: %s", mdb_strerror(rc));
		mdb_txn_abort(txn);
		mdb_env_close(gc->env);
		free(gc);
		return (NULL);
	}

	rc = mdb_txn_commit(txn);
	if (rc != 0) {
		olog(LOG_ERR, "cogcache_open: mmit after open %s", mdb_strerror(rc));
		mdb_env_close(gc->env);
		free(gc);
		return (NULL);
	}

	return (gc);
}

void gcache_close(struct gcache *gc)
{
	if (gc == NULL)
		return;

	mdb_env_close(gc->env);
	free(gc);
}

static int gcache_del(struct gcache *gc, char *keystr)
{
	int rc;
	MDB_val key;
	MDB_txn *txn;

	rc = mdb_txn_begin(gc->env, NULL, 0, &txn);
	if (rc != 0) {
		olog(LOG_ERR, "gcache_del: mdb_txn_begin: %s", mdb_strerror(rc));
		return (rc);
	}

	key.mv_data	= keystr;
	key.mv_size	= strlen(keystr);

	rc = mdb_del(txn, gc->dbi, &key, NULL);
	if (rc != 0 && rc != MDB_NOTFOUND) {
		olog(LOG_ERR, "gcache_del: mdb_del: %s", mdb_strerror(rc));
		/* fall through to commit */
	}

	rc = mdb_txn_commit(txn);
	if (rc) {
		olog(LOG_ERR, "gcache_del: mdb_txn_commit: (%d) %s", rc, mdb_strerror(rc));
		mdb_txn_abort(txn);
	}
	return (rc);
}

int gcache_put(struct gcache *gc, char *keystr, char *payload)
{
	int rc;
	MDB_val key, data;
	MDB_txn *txn;

	if (gc == NULL)
		return (1);

	if (strcmp(payload, "DELETE") == 0)
		return gcache_del(gc, keystr);

	rc = mdb_txn_begin(gc->env, NULL, 0, &txn);
	if (rc != 0)
		olog(LOG_ERR, "gcache_put: mdb_txn_begin: %s", mdb_strerror(rc));

	key.mv_data	= keystr;
	key.mv_size	= strlen(keystr);
	data.mv_data	= payload;
	data.mv_size	= strlen(payload) + 1;	/* including nul-byte so we can
						 * later decode string directly
						 * from this buffer */

	rc = mdb_put(txn, gc->dbi, &key, &data, 0);
	if (rc != 0)
		olog(LOG_ERR, "gcache_put: mdb_put: %s", mdb_strerror(rc));

	rc = mdb_txn_commit(txn);
	if (rc) {
		olog(LOG_ERR, "gcache_put: mdb_txn_commit: (%d) %s", rc, mdb_strerror(rc));
		mdb_txn_abort(txn);
	}
	return (rc);
}

int gcache_json_put(struct gcache *gc, char *keystr, JsonNode *json)
{
	int rc;
	char *js;

	if (gc == NULL)
		return (1);

	if ((js = json_stringify(json, NULL)) == NULL) {
		olog(LOG_ERR, "gcache_json_put: CAN'T stringify JSON");
		return (1);
	}

	rc = gcache_put(gc, keystr, js);
	free(js);
	return (rc);
}

long gcache_get(struct gcache *gc, char *k, char *buf, long buflen)
{
	MDB_val key, data;
	MDB_txn *txn;
	int rc;
	long len = -1;

	if (gc == NULL)
		return (-1);

	rc = mdb_txn_begin(gc->env, NULL, MDB_RDONLY, &txn);
	if (rc) {
		olog(LOG_ERR, "gcache_get: mdb_txn_begin: (%d) %s", rc, mdb_strerror(rc));
		return (-1);
	}

	key.mv_data = k;
	key.mv_size = strlen(k);

	rc = mdb_get(txn, gc->dbi, &key, &data);
	if (rc != 0) {
		if (rc != MDB_NOTFOUND) {
			printf("get: %s\n", mdb_strerror(rc));
		} else {
			// printf(" [%s] not found\n", k);
		}
	} else {
		len =  (data.mv_size < buflen) ? data.mv_size : buflen;
		memcpy(buf, data.mv_data, len);
		// printf("%s\n", (char *)data.mv_data);
	}
	mdb_txn_commit(txn);
	return (len);
}

/*
 * Attempt to get key `k` from LMDB. If found, decode the JSON string in it and
 * return a JSON object, else NULL.
 */

JsonNode *gcache_json_get(struct gcache *gc, char *k)
{
	MDB_val key, data;
	MDB_txn *txn;
	int rc;
	JsonNode *json = NULL;

	if (gc == NULL)
		return (NULL);

	rc = mdb_txn_begin(gc->env, NULL, MDB_RDONLY, &txn);
	if (rc) {
		olog(LOG_ERR, "gcache_json_get: mdb_txn_begin: (%d) %s", rc, mdb_strerror(rc));
		return (NULL);
	}

	key.mv_data = k;
	key.mv_size = strlen(k);

	rc = mdb_get(txn, gc->dbi, &key, &data);
	if (rc != 0) {
		if (rc != MDB_NOTFOUND) {
			olog(LOG_ERR, "gcache_json_get(%s): %s", k, mdb_strerror(rc));
		} else {
			// printf(" [%s] not found\n", k);
			json = NULL;
		}
	} else {
		// printf("%s\n", (char *)data.mv_data);
		if ((json = json_decode((char *)data.mv_data)) == NULL) {
			olog(LOG_ERR, "gcache_json_get: Cannot decode JSON from lmdb");
		}
	}

	mdb_txn_commit(txn);

	return (json);
}

void gcache_dump(char *path, char *lmdbname)
{
	struct gcache *gc;
	MDB_val key, data;
	MDB_txn *txn;
	MDB_cursor *cursor;
	int rc;

	if ((gc = gcache_open(path, lmdbname, TRUE)) == NULL) {
		fprintf(stderr, "Cannot open lmdb/%s at %s\n",
			lmdbname ? lmdbname : "NULL", path);
		return;
	}

	key.mv_size = 0;
	key.mv_data = NULL;

	rc = mdb_txn_begin(gc->env, NULL, MDB_RDONLY, &txn);
	if (rc) {
		olog(LOG_ERR, "gcache_dump: mdb_txn_begin: (%d) %s", rc, mdb_strerror(rc));
		gcache_close(gc);
		return;
	}

	rc = mdb_cursor_open(txn, gc->dbi, &cursor);

	/* -1 because we 0-terminate strings */
	while ((rc = mdb_cursor_get(cursor, &key, &data, MDB_NEXT)) == 0) {
		printf("%*.*s %*.*s\n",
			(int)key.mv_size, (int)key.mv_size, (char *)key.mv_data,
			(int)data.mv_size - 1, (int)data.mv_size - 1, (char *)data.mv_data);
	}
	mdb_cursor_close(cursor);
	mdb_txn_commit(txn);
	gcache_close(gc);
}

void gcache_load(char *path, char *lmdbname)
{
	struct gcache *gc;
	char buf[8192], *bp;

	if ((gc = gcache_open(path, lmdbname, FALSE)) == NULL) {
		olog(LOG_ERR, "gcache_load: gcache_open");
		return;
	}

	while (fgets(buf, sizeof(buf), stdin) != NULL) {

		if ((bp = strchr(buf, '\r')) != NULL)
			*bp = 0;
		if ((bp = strchr(buf, '\n')) != NULL)
			*bp = 0;

		if ((bp = strchr(buf, ' ')) != NULL) {
			*bp = 0;

			gcache_put(gc, buf, bp+1);
		}
	}

	gcache_close(gc);
}
