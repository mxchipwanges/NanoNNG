#include "nng/supplemental/nanolib/conf.h"
#include "core/nng_impl.h"
#include "nng/nng.h"
#include "nng/supplemental/nanolib/cJSON.h"
#include "nng/supplemental/nanolib/cvector.h"
#include "nng/supplemental/nanolib/file.h"
#include "nng/supplemental/nanolib/log.h"
#include "nng/supplemental/nanolib/hocon.h"
#include <ctype.h>
#include <string.h>

// Read json value into struct
// use same struct fields and json keys
#define hocon_read_str_base(structure, field, key, jso)          \
	do {                                                     \
		cJSON *jso_key = cJSON_GetObjectItem(jso, key);  \
		if (NULL == jso_key) {                           \
			log_error("Read config %s failed!", key); \
			break;                                   \
		}                                                \
		switch (jso_key->type) {                         \
		case cJSON_String:                               \
			(structure)->field =                     \
			    nng_strdup(jso_key->valuestring);    \
			break;                                   \
		default:                                         \
			break;                                   \
		}                                                \
	} while (0);