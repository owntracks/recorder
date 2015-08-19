#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <ctype.h>
#include <time.h>
#include "file.h"
#include "misc.h"

static void ut_lower(UT_string *us)
{
        char *p;

        for (p = utstring_body(us); p && *p; p++) {
                if (!isalnum(*p) || isspace(*p))
                        *p = '-';
                else if (isupper(*p))
                        *p = tolower(*p);

        }
}

static void ut_clean(UT_string *us)
{
        char *p;

        for (p = utstring_body(us); p && *p; p++) {
                if (isspace(*p))
                        *p = '-';
        }
}

static const char *yyyymm(time_t t) {
        static char buf[] = "YYYY-MM";

        strftime(buf, sizeof(buf), "%Y-%m", gmtime(&t));
        return(buf);
}

/* Return an open append file pointer to storage for user/device,
   creating directories on the fly. If device is NULL, omit it.
 */

FILE *pathn(char *mode, char *prefix, UT_string *user, UT_string *device, char *suffix)
{
        static UT_string *path = NULL;
	time_t now;

        utstring_renew(path);

        ut_lower(user);

	if (device) {
		ut_lower(device);
		utstring_printf(path, "%s/%s/%s/%s", STORAGEDIR, prefix, utstring_body(user), utstring_body(device));
	} else {
		utstring_printf(path, "%s/%s/%s", STORAGEDIR, prefix, utstring_body(user));
	}

        ut_clean(path);

        if (mkpath(utstring_body(path)) < 0) {
                perror(utstring_body(path));
                return (NULL);
        }


#if 0
	if (device) {
		utstring_printf(path, "/%s-%s.%s",
			utstring_body(user), utstring_body(device), suffix);
	} else {
		utstring_printf(path, "/%s.%s",
			utstring_body(user), suffix);
	}
#endif

	if (strcmp(prefix, "rec") == 0) {
		time(&now);
		utstring_printf(path, "/%s.%s", yyyymm(now), suffix);
	} else {
		utstring_printf(path, "/%s.%s",
			utstring_body(user), suffix);
	}

        ut_clean(path);

        return (fopen(utstring_body(path), mode));

}

#if 0

int main(int argc, char **argv)
{
        UT_string *user, *dev;
        FILE *fp;

        utstring_new(user);
        utstring_new(dev);

        utstring_printf(user, "%s", argv[1]);
        utstring_printf(dev, "%s", argv[2]);
        fp = pathn(user, dev);
        fputs("hello\n", fp);
        return 0;
}

#endif
