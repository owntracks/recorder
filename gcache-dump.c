#include <stdio.h>
#include <string.h>
#include "lmdb.h"

int main(int argc,char **argv)
{
	int rc;
	MDB_env *env;
	MDB_dbi dbi;
	MDB_val key, data;
	MDB_txn *txn;
	MDB_cursor *cursor;

	if (argc != 2) {
		fprintf(stderr, "Usage: %s storage-directory\n", *argv);
		return (1);
	}

	rc = mdb_env_create(&env);
	rc = mdb_env_open(env, argv[1], 0, 0664);
	if (rc) {
		fprintf(stderr, "%s: %s: %s\n", *argv, argv[1], mdb_strerror(rc));
		return (2);
	}

	key.mv_size = 0;
	key.mv_data = NULL;
	
	rc = mdb_txn_begin(env, NULL, MDB_RDONLY, &txn);
	rc = mdb_dbi_open(txn, NULL, 0, &dbi);
	rc = mdb_cursor_open(txn, dbi, &cursor);
	while ((rc = mdb_cursor_get(cursor, &key, &data, MDB_NEXT)) == 0) {
		printf("%*.*s %*.*s\n",
			(int)key.mv_size, (int)key.mv_size, key.mv_data,
			(int)data.mv_size, (int)data.mv_size, data.mv_data);
	}
	mdb_cursor_close(cursor);
	mdb_txn_abort(txn);

	mdb_dbi_close(env, dbi);
	mdb_env_close(env);
	return (0);
}
