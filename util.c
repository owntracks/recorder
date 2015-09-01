#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include "util.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#ifndef LINESIZE
# define LINESIZE 8192
#endif

int is_directory(char *path)
{
	struct stat sb;

	if (stat(path, &sb) != 0)
		return (0);
	return (S_ISDIR(sb.st_mode));
}

const char *isotime(time_t t) {
        static char buf[] = "YYYY-MM-DDTHH:MM:SSZ";

        strftime(buf, sizeof(buf), "%FT%TZ", gmtime(&t));
        return(buf);
}

char *slurp_file(char *filename, int fold_newlines)
{
	FILE *fp;
	char *buf, *bp;
	off_t len;
	int ch;

	if ((fp = fopen(filename, "rb")) == NULL)
		return (NULL);

	if (fseeko(fp, 0, SEEK_END) != 0) {
		fclose(fp);
		return (NULL);
	}
	len = ftello(fp);
	fseeko(fp, 0, SEEK_SET);

	if ((bp = buf = malloc(len + 1)) == NULL) {
		fclose(fp);
		return (NULL);
	}
	while ((ch = fgetc(fp)) != EOF) {
		if (ch == '\n') {
			if (!fold_newlines)
				*bp++ = ch;
		} else *bp++ = ch;
	}
	*bp = 0;
	fclose(fp);

	return (buf);
}

/* Copy the node into obj */
int json_copy_element_to_object(JsonNode *obj, char *key, JsonNode *node)
{
	if (obj->tag != JSON_OBJECT)
		return (FALSE);

	if (node->tag == JSON_STRING)
		json_append_member(obj, key, json_mkstring(node->string_));
	else if (node->tag == JSON_NUMBER)
		json_append_member(obj, key, json_mknumber(node->number_));
	else if (node->tag == JSON_BOOL)
		json_append_member(obj, key, json_mkbool(node->bool_));
	else if (node->tag == JSON_NULL)
		json_append_member(obj, key, json_mknull());

	return (TRUE);
}

int json_copy_to_object(JsonNode * obj, JsonNode * object_or_array, int clobber)
{
	JsonNode *node;

	if (obj->tag != JSON_OBJECT && obj->tag != JSON_ARRAY)
		return (FALSE);

	json_foreach(node, object_or_array) {
		if (!clobber & (json_find_member(obj, node->key) != NULL))
			continue;	/* Don't clobber existing keys */
		if (obj->tag == JSON_OBJECT) {
			if (node->tag == JSON_STRING)
				json_append_member(obj, node->key, json_mkstring(node->string_));
			else if (node->tag == JSON_NUMBER)
				json_append_member(obj, node->key, json_mknumber(node->number_));
			else if (node->tag == JSON_BOOL)
				json_append_member(obj, node->key, json_mkbool(node->bool_));
			else if (node->tag == JSON_NULL)
				json_append_member(obj, node->key, json_mknull());
			else if (node->tag == JSON_ARRAY) {
				JsonNode       *array = json_mkarray();
				json_copy_to_object(array, node, clobber);
				json_append_member(obj, node->key, array);
			} else if (node->tag == JSON_OBJECT) {
				JsonNode       *newobj = json_mkobject();
				json_copy_to_object(newobj, node, clobber);
				json_append_member(obj, node->key, newobj);
			} else
				printf("PANIC: unhandled JSON type %d\n", node->tag);
		} else if (obj->tag == JSON_ARRAY) {
			if (node->tag == JSON_STRING)
				json_append_element(obj, json_mkstring(node->string_));
			if (node->tag == JSON_NUMBER)
				json_append_element(obj, json_mknumber(node->number_));
			if (node->tag == JSON_BOOL)
				json_append_element(obj, json_mkbool(node->bool_));
			if (node->tag == JSON_NULL)
				json_append_element(obj, json_mknull());
		}
	}
	return (TRUE);
}

/*
 * Open filename for reading; slurp in the whole file and attempt
 * to decode JSON from it into the JSON object at `obj'. TRUE on success, FALSE on failure.
 */

int json_copy_from_file(JsonNode *obj, char *filename)
{
	char *js_string;
	JsonNode *node;

	if ((js_string = slurp_file(filename, TRUE)) == NULL) {
		return (FALSE);
	}

	if ((node = json_decode(js_string)) == NULL) {
		fprintf(stderr, "json_copy_from_file can't decode JSON from %s\n", filename);
		return (FALSE);
	}
	json_copy_to_object(obj, node, FALSE);
	json_delete(node);

	return (TRUE);
}

#define strprefix(s, pfx) (strncmp((s), (pfx), strlen(pfx)) == 0)

#define MAXPARTS 40

/*
 * Split the string at `s', separated by characters in `sep'
 * into individual strings, in array `parts'. The caller must
 * free `parts'.
 * Returns -1 on error, or the number of parts.
 */

int splitter(char *s, char *sep, char **parts)
{
        char *token, *p, *ds = strdup(s);
        int nt = 0;

	if (!ds)
		return (-1);

	for (token = strtok(ds, sep);
		token && *token && nt < (MAXPARTS - 1); token = strtok(NULL, sep)) {
		if ((p = strdup(token)) == NULL)
			return (-1);
		parts[nt++] = p;
        }
	parts[nt] = NULL;

        free(ds);
        return (nt);
}

/*
 * Split a string separated by characters in `sep' into a JSON
 * array and return that or NULL on error.
 */

JsonNode *json_splitter(char *s, char *sep)
{
	char *token, *ds = strdup(s);
	JsonNode *array = json_mkarray();

	if (!ds || !array)
		return (NULL);

	for (token = strtok(ds, sep); token && *token; token = strtok(NULL, sep)) {
		json_append_element(array, json_mkstring(token));
        }

	free(ds);
	return (array);
}


/* Return the numeric LOG_ value for a syslog facilty name.  */

int syslog_facility_code(char *facility)
{
	struct _log {
		char *name;
		int val;
	};
	static struct _log codes[] = {
		{"kern", LOG_KERN},
		{"user", LOG_USER},
		{"mail", LOG_MAIL},
		{"daemon", LOG_DAEMON},
		{"auth", LOG_AUTH},
		{"syslog", LOG_SYSLOG},
		{"lpr", LOG_LPR},
		{"news", LOG_NEWS},
		{"uucp", LOG_UUCP},
		{"local0", LOG_LOCAL0},
		{"local1", LOG_LOCAL1},
		{"local2", LOG_LOCAL2},
		{"local3", LOG_LOCAL3},
		{"local4", LOG_LOCAL4},
		{"local5", LOG_LOCAL5},
		{"local6", LOG_LOCAL6},
		{"local7", LOG_LOCAL7},
		{NULL, -1}
	},  *lp;


	for (lp = codes; lp->val != -1; lp++) {
		if (!strcasecmp(facility, lp->name))
			return (lp->val);
	}
	return (LOG_LOCAL0);
}

const char *yyyymm(time_t t) {
        static char buf[] = "YYYY-MM";

        strftime(buf, sizeof(buf), "%Y-%m", gmtime(&t));
        return(buf);
}

/*
 * Read the open file fp (which must have file poiter at EOF) line by line
 * backwards.
 * (http://stackoverflow.com/questions/14834267/)
 */

static char *tac_gets(char *buf, int n, FILE * fp)
{
	long fpos;
	int cpos;
	int first = 1;

	if (n <= 1 || (fpos = ftell(fp)) == -1 || fpos == 0)
		return (NULL);

	cpos = n - 1;
	buf[cpos] = '\0';

	while (1) {
		int c;

		if (fseek(fp, --fpos, SEEK_SET) != 0 ||
		    (c = fgetc(fp)) == EOF)
			return (NULL);

		if (c == '\n' && first == 0)	/* accept at most one '\n' */
			break;
		first = 0;

		if (c != '\r') {/* ignore DOS/Windows '\r' */
			unsigned char ch = c;
			if (cpos == 0) {
				memmove(buf + 1, buf, n - 2);
				++cpos;
			}
			memcpy(buf + --cpos, &ch, 1);
		}
		if (fpos == 0) {
			fseek(fp, 0, SEEK_SET);
			break;
		}
	}

	memmove(buf, buf + cpos, n - cpos);

	return (buf);
}

/*
 * Open filename and read lines from it, invoking func() on each line. Func
 * is passed the line and an arbitrary argument pointer.
 * If filename is "-", read stdin.
 */

int cat(char *filename, int (*func)(char *, void *), void *param)
{
	FILE *fp;
	char buf[LINESIZE], *bp;
	int rc = 0, doclose = FALSE;

	if (strcmp(filename, "-") != 0) {
		if ((fp = fopen(filename, "r")) == NULL) {
			fprintf(stderr, "failed to open file \'%s\'\n", filename);
			return (-1);
		}
		doclose = TRUE;
	} else {
		fp = stdin;
	}

	while (fgets(buf, sizeof(buf), fp) != NULL) {
		if ((bp = strchr(buf, '\n')) != NULL)
			*bp = 0;
		rc = func(buf, param);
		if (rc == -1)
			break;
	}
	if (doclose)
		fclose(fp);
	return (rc);
}

/*
 * Open file and read at most `lines' lines from it in reverse, invoking
 * func() on each line. The user-supplied func() is passed the line and
 * an argument. If func returns 1, the line is considered "printed"; if
 * 0 is returned it is ignored, and if -2 is returned, tac stops reading
 * the file and returns.
 */

int tac(char *filename, long lines, int (*func)(char *, void *), void *param)
{
	FILE *fp;
	long file_len;
	char buf[LINESIZE], *bp;
	int rc;

	if ((fp = fopen(filename, "r")) == NULL) {
		fprintf(stderr, "failed to open file \'%s\'\n", filename);
		return (-1);
	}
	fseek(fp, 0, SEEK_END);
	file_len = ftell(fp);

	if (file_len > 0) {
		while (tac_gets(buf, sizeof(buf), fp) != NULL) {
			if ((bp = strchr(buf, '\n')) != NULL)
				*bp = 0;
			rc = func(buf, param);
			if (rc == 1) {
				if (--lines <= 0)
					break;
			}
			else if (rc == -1)
				break;
		}
	}
	fclose(fp);
	return (0);
}
