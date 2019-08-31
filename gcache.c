/*
 * OwnTracks Recorder
 * Copyright (C) 2015-2016 Jan-Piet Mens <jpmens@gmail.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include "udata.h"
#include "fences.h"
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
		return NULL;
	}

	if ((gc = malloc(sizeof (struct gcache))) == NULL)
		return NULL;

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
		return NULL;
	}

	mdb_env_set_mapsize(gc->env, LMDB_DB_SIZE);

	rc = mdb_env_set_maxdbs(gc->env, 10);
	if (rc != 0) {
		olog(LOG_ERR, "gcache_open: mdb_env_set_maxdbs%s", mdb_strerror(rc));
		free(gc);
		return NULL;
	}

	rc = mdb_env_open(gc->env, path, flags, perms);
	if (rc != 0) {
		olog(LOG_ERR, "gcache_open: mdb_env_open: %s", mdb_strerror(rc));
		free(gc);
		return NULL;
	}

	/* Open a pseudo TX so that we can open DBI */

	mdb_txn_begin(gc->env, NULL, flags, &txn);
	if (rc != 0) {
		olog(LOG_ERR, "gcache_open: mdb_txn_begin: %s", mdb_strerror(rc));
		mdb_env_close(gc->env);
		free(gc);
		return NULL;
	}

	rc = mdb_dbi_open(txn, dbname, dbiflags, &gc->dbi);
	if (rc != 0) {
		olog(LOG_ERR, "gcache_open: mdb_dbi_open for `%s': %s", dbname, mdb_strerror(rc));
		mdb_txn_abort(txn);
		mdb_env_close(gc->env);
		free(gc);
		return NULL;
	}

	rc = mdb_txn_commit(txn);
	if (rc != 0) {
		olog(LOG_ERR, "cogcache_open: commit after open %s", mdb_strerror(rc));
		mdb_env_close(gc->env);
		free(gc);
		return NULL;
	}

	return gc;
}

void gcache_close(struct gcache *gc)
{
	if (gc == NULL)
		return;

	mdb_env_close(gc->env);
	free(gc);
}

int gcache_del(struct gcache *gc, char *keystr)
{
	int rc;
	MDB_val key;
	MDB_txn *txn;

	rc = mdb_txn_begin(gc->env, NULL, 0, &txn);
	if (rc != 0) {
		olog(LOG_ERR, "gcache_del: mdb_txn_begin: %s", mdb_strerror(rc));
		return rc;
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
	return rc;
}

int gcache_put(struct gcache *gc, char *keystr, char *payload)
{
	int rc;
	MDB_val key, data;
	MDB_txn *txn;

	if (gc == NULL)
		return 1;

	if (strcmp(payload, "DELETE") == 0)
		return gcache_del(gc, keystr);

	rc = mdb_txn_begin(gc->env, NULL, 0, &txn);
	if (rc != 0) {
		olog(LOG_ERR, "gcache_put: mdb_txn_begin: %s", mdb_strerror(rc));
		return -1;
	}

	key.mv_data	= keystr;
	key.mv_size	= strlen(keystr);
	data.mv_data	= payload;
	data.mv_size	= strlen(payload) + 1;	/* including nul-byte so we can
						 * later decode string directly
						 * from this buffer */

	rc = mdb_put(txn, gc->dbi, &key, &data, 0);
	if (rc != 0) {
		olog(LOG_ERR, "gcache_put: mdb_put: %s", mdb_strerror(rc));
		/* fall through to commit */
	}

	rc = mdb_txn_commit(txn);
	if (rc) {
		olog(LOG_ERR, "gcache_put: mdb_txn_commit: (%d) %s", rc, mdb_strerror(rc));
		mdb_txn_abort(txn);
	}
	return rc;
}

int gcache_json_put(struct gcache *gc, char *keystr, JsonNode *json)
{
	int rc;
	char *js;

	if (gc == NULL)
		return 1;

	if ((js = json_stringify(json, NULL)) == NULL) {
		olog(LOG_ERR, "gcache_json_put: CAN'T stringify JSON");
		return 1;
	}

	rc = gcache_put(gc, keystr, js);
	free(js);
	return rc;
}

long gcache_get(struct gcache *gc, char *k, char *buf, long buflen)
{
	MDB_val key, data;
	MDB_txn *txn;
	int rc;
	long len = -1;

	if (gc == NULL)
		return -1;

	rc = mdb_txn_begin(gc->env, NULL, MDB_RDONLY, &txn);
	if (rc) {
		olog(LOG_ERR, "gcache_get: mdb_txn_begin: (%d) %s", rc, mdb_strerror(rc));
		return -1;
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
	return len;
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
		return NULL;

	rc = mdb_txn_begin(gc->env, NULL, MDB_RDONLY, &txn);
	if (rc) {
		olog(LOG_ERR, "gcache_json_get: mdb_txn_begin: (%d) %s", rc, mdb_strerror(rc));
		return NULL;
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

	return json;
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

			if (gcache_put(gc, buf, bp+1) != 0) {
				fprintf(stderr, "Cannot load key\n");
			}
		}
	}

	gcache_close(gc);
}

/*
 * Enumerate (list) keys in lmdb `gc` and invoke func() on each. If func() returns true
 * update the data.
 */

bool gcache_enum(char *user, char *device, struct gcache *gc, char *key_part, int (*func)(char *key, wpoint *wp, double lat, double lon), double lat, double lon, struct udata *ud, char *topic, JsonNode *jsonpayload)
{
	MDB_val key, data;
	MDB_txn *txn;
	MDB_cursor *cursor;
	int rc, op;
	static UT_string *ks; 	/* key string */
	wpoint wp;

	if (gc == NULL)
		return NULL;

	rc = mdb_txn_begin(gc->env, NULL, MDB_RDONLY, &txn);
	if (rc) {
		olog(LOG_ERR, "gcache_enum: mdb_txn_begin: (%d) %s", rc, mdb_strerror(rc));
		return NULL;
	}

	key.mv_data = key_part;
	key.mv_size = strlen(key_part);

	rc = mdb_cursor_open(txn, gc->dbi, &cursor);

	op = MDB_SET_RANGE;
	do {
		JsonNode *json, *jlat, *jlon, *jrad, *jio, *jdesc;
		size_t len;

		rc = mdb_cursor_get(cursor, &key, &data, op);
		if (rc != 0)
			break;

		len = (strlen(key_part) < key.mv_size) ? strlen(key_part) : key.mv_size;
		if (memcmp(key_part, key.mv_data, len) != 0) {
			break;
		}

		/* -1 because we 0-terminate strings */
		//printf("ENUM---- %*.*s %*.*s\n",
		//	(int)key.mv_size, (int)key.mv_size, (char *)key.mv_data,
		//	(int)data.mv_size - 1, (int)data.mv_size - 1, (char *)data.mv_data);

		utstring_renew(ks);
		utstring_printf(ks, "%*.*s",
			(int)key.mv_size, (int)key.mv_size, (char *)key.mv_data);

		if ((json = json_decode(data.mv_data)) == NULL)
			continue;

		if ((jlat = json_find_member(json, "lat")) == NULL) continue;
		if ((jlon = json_find_member(json, "lon")) == NULL) continue;
		if ((jrad = json_find_member(json, "rad")) == NULL) continue;
		if ((jdesc = json_find_member(json, "desc")) == NULL) continue;
		if ((jio = json_find_member(json, "io")) == NULL) {
			json_append_member(json, "io", json_mkbool(false));
			jio = json_find_member(json, "io");
		}

		wp.lat	  = jlat->number_;
		wp.lon	  = jlon->number_;
		wp.rad	  = (long)jrad->number_;
		wp.io	  = jio->bool_;
		wp.desc	  = strdup(jdesc->string_);

		wp.ud	  = ud;
		wp.user   = user;
		wp.device = device;
		wp.topic  = topic;
		wp.json   = jsonpayload;

		if (func && func(UB(ks), &wp, lat, lon) == true) {
			json_delete(jio);
			json_append_member(json, "io", json_mkbool(wp.io));
			if (gcache_json_put(gc, UB(ks), json) != 0) {
				olog(LOG_ERR, "gcache_enum: cannot rewrite key %s", UB(ks));
			}
		}
		free(wp.desc);
		json_delete(json);

		op = MDB_NEXT;
	} while (rc == 0);

	mdb_cursor_close(cursor);
	mdb_txn_commit(txn);

	return true;
}
