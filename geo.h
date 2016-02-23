#include "json.h"
#include "udata.h"

JsonNode *revgeo(struct udata *ud, double lat, double lon, UT_string *addr, UT_string *cc);
void revgeo_init();
void revgeo_free();
