#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "util.h"

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
