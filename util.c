#include "util.h"

const char *isotime(time_t t) {
        static char buf[] = "YYYY-MM-DDTHH:MM:SSZ";

        strftime(buf, sizeof(buf), "%FT%TZ", gmtime(&t));
        return(buf);
}
