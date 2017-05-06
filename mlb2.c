/*
Copyright (c) 2017, Olaf Seibert
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are
met:

o Redistributions of source code must retain the above copyright
  notice, this list of conditions and the following disclaimer.

o Redistributions in binary form must reproduce the above copyright
  notice, this list of conditions and the following disclaimer in the
  documentation and/or other materials provided with the distribution.

o Neither the name of the copyright holder nor the names of its
  contributors may be used to endorse or promote products derived from
  this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
"AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS
OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR
TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE
USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH
DAMAGE.
*/

#include <stdlib.h>
#include <string.h>
#include "util.h"
#include "mlb.h"

MLB_VTBL *mlb_vtbls[] = {
    &mlb_rsx_vtbl,
    &mlb_rt11_vtbl,
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
            mlb->name = memcheck(strdup(name));
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


