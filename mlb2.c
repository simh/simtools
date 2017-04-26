#include <stdlib.h>
#include <string.h>
#include "mlb.h"

MLB_VTBL *mlb_vtbls[] = {
    &mlb_rsx_vtbl,
    NULL
};

MLB     *mlb_open(
    char *name,
    int allow_object_library)
{
    MLB_VTBL *vtbl;
    MLB *mlb = NULL;
    int i;

    for (i = 0; (vtbl = mlb_vtbls[i]); i++) {
        mlb = vtbl->mlb_open(name, allow_object_library);
        if (mlb != NULL) {
            mlb->name = strdup(name);
            break;
        }
    }

    return mlb;
}

BUFFER  *mlb_entry(
    MLB *mlb,
    char *name)
{
    return mlb->vtbl->mlb_entry(mlb, name);
}

void     mlb_close(
    MLB *mlb)
{
    free(mlb->name);
    mlb->vtbl->mlb_close(mlb);
}

void     mlb_extract(
    MLB *mlb)
{
    mlb->vtbl->mlb_extract(mlb);
}


