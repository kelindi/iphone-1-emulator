#ifndef AMC_TRANSPORT_OBSERVER_H
#define AMC_TRANSPORT_OBSERVER_H
#include "soc.h"
#include "destination.h"
#include <stdio.h>
#define TRANSPORT_VALUES 4096
/* Observer-owned scalar ledger; never serialized as emulated machine state. */
typedef struct { const char *names[TRANSPORT_VALUES]; uint64_t values[TRANSPORT_VALUES]; unsigned count; } transport_values;
bool transport_manager(const destination_observer*,const uint8_t*,uint32_t,uint32_t,uint8_t[64]);
bool transport_lli(const s5l8900_t*,unsigned,unsigned,uint8_t[16]);
bool transport_collect(const s5l8900_t*,transport_values*);
bool transport_export(const s5l8900_t*,const destination_observer*,const char*,const char*,bool);
#endif
