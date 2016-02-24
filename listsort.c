#include <stdlib.h>
#include <stdio.h>
#include "listsort.h"

/* from http://www.chiark.greenend.org.uk/~sgtatham/algorithms/listsort.c */

/*
 * This file is copyright 2001 Simon Tatham.
 * 
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use,
 * copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following
 * conditions:
 * 
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT.  IN NO EVENT SHALL SIMON TATHAM BE LIABLE FOR
 * ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF
 * CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

static double cmp(JsonNode * a, JsonNode * b)
{
	JsonNode *ta = json_find_member(a, "tst");
	JsonNode *tb = json_find_member(b, "tst");
	//printf("a=%lf, b=%lf\n", ta->number_, tb->number_);

	/* user may have chosen fields without tst */

	if (!ta || !tb)
		return (0.0);

	return ta->number_ - tb->number_;
}

/*
 * This is the actual sort function. Notice that it returns the new head of
 * the list. (It has to, because the head will not generally be the same
 * element after the sort.) So unlike sorting an array, where you can do
 * 
 * sort(myarray);
 * 
 * you now have to do
 * 
 * list = listsort(mylist);
 */
JsonNode *listsort(JsonNode * list, int is_circular, int is_double)
{
	JsonNode *p, *q, *e, *tail, *oldhead;
	int insize, nmerges, psize, qsize, i;

	/*
	 * Silly special case: if `list' was passed in as NULL, return NULL
	 * immediately.
	 */
	if (!list)
		return NULL;

	insize = 1;

	while (1) {
		p = list;
		oldhead = list;	/* only used for circular linkage */
		list = NULL;
		tail = NULL;

		nmerges = 0;	/* count number of merges we do in this pass */

		while (p) {
			nmerges++;	/* there exists a merge to be done */
			/* step `insize' places along from p */
			q = p;
			psize = 0;
			for (i = 0; i < insize; i++) {
				psize++;
				if (is_circular)
					q = (q->next == oldhead ? NULL : q->next);
				else
					q = q->next;
				if (!q)
					break;
			}

			/*
			 * if q hasn't fallen off end, we have two lists to
			 * merge
			 */
			qsize = insize;

			/* now we have two lists; merge them */
			while (psize > 0 || (qsize > 0 && q)) {

				/*
				 * decide whether next element of merge comes
				 * from p or q
				 */
				if (psize == 0) {
					/* p is empty; e must come from q. */
					e = q;
					q = q->next;
					qsize--;
					if (is_circular && q == oldhead)
						q = NULL;
				} else if (qsize == 0 || !q) {
					/* q is empty; e must come from p. */
					e = p;
					p = p->next;
					psize--;
					if (is_circular && p == oldhead)
						p = NULL;
				} else if (cmp(p, q) <= 0) {
					/*
					 * First element of p is lower (or
					 * same); e must come from p.
					 */
					e = p;
					p = p->next;
					psize--;
					if (is_circular && p == oldhead)
						p = NULL;
				} else {
					/*
					 * First element of q is lower; e
					 * must come from q.
					 */
					e = q;
					q = q->next;
					qsize--;
					if (is_circular && q == oldhead)
						q = NULL;
				}

				/* add the next element to the merged list */
				if (tail) {
					tail->next = e;
				} else {
					list = e;
				}
				if (is_double) {
					/*
					 * Maintain reverse pointers in a
					 * doubly linked list.
					 */
					e->prev = tail;
				}
				tail = e;
			}

			/*
			 * now p has stepped `insize' places along, and q has
			 * too
			 */
			p = q;
		}
		if (is_circular) {
			tail->next = list;
			if (is_double)
				list->prev = tail;
		} else
			tail->next = NULL;

		/* If we have done only one merge, we're finished. */
		if (nmerges <= 1)	/* allow for nmerges==0, the empty
					 * list case */
			return list;

		/* Otherwise repeat, merging lists twice the size */
		insize *= 2;
	}
}

#ifdef TESTING
int main()
{
	JsonNode *n, *sorted, *arr = json_mkarray();
	JsonNode *o;

	o = json_mkobject();
	json_append_member(o, "tst", json_mknumber(2));
	json_append_member(o, "desc", json_mkstring("dos"));
	json_append_element(arr, o);

	o = json_mkobject();
	json_append_member(o, "tst", json_mknumber(69));
	json_append_member(o, "desc", json_mkstring("sixtynine"));
	json_append_element(arr, o);

	o = json_mkobject();
	json_append_member(o, "tst", json_mknumber(14));
	json_append_member(o, "desc", json_mkstring("fourteen"));
	json_append_element(arr, o);

	o = json_mkobject();
	json_append_member(o, "tst", json_mknumber(7));
	json_append_member(o, "desc", json_mkstring("seven"));
	json_append_element(arr, o);


	json_foreach(n, arr) {
		JsonNode *e = json_find_member(n, "tst");
		JsonNode *d = json_find_member(n, "desc");
		printf("%lf - %s\n", e->number_, d->string_);
	}


	listsort(NULL, 0, 0);
	sorted = listsort(json_first_child(arr), 0, 0);
	sorted = sorted->parent;
	printf("------- %s\n", (sorted == arr) ? "SAME" : "different");

	json_foreach(n, sorted) {
		JsonNode *e = json_find_member(n, "tst");
		JsonNode *d = json_find_member(n, "desc");
		printf("%lf - %s\n", e->number_, d->string_);
	}

	json_delete(arr);

	return 0;
}
#endif
