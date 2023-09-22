#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <assert.h>

#include <windows.h>

#include "npapi/npapi.h"
#include "npapi/npfunctions.h"
#include "npapi/npruntime.h"
#include "npapi/nptypes.h"

#include "ffrunner.h"

void
handle_requests(void)
{
    int i;
    NPReason res;

    for (i = 0; i < nrequests; i++) {
        if (requests[i].completed)
            continue;

        res = NPRES_DONE;

        if (strncmp(requests[i].url, REVISIONS_PLIST, strlen(REVISIONS_PLIST)) == 0)
            res = NPRES_NETWORK_ERR;

        NPStream npstream = {
            .url = requests[i].url,
            .notifyData = requests[i].notifyData,
        };

        /* TODO: implement this
        pluginFuncs.newstream(&npp "TODO/MIMETYPE", &npstream, 0, NP_NORMAL);
        pluginFuncs.writeready(&npp, &npstream);
        pluginFuncs.write(&npp, &npstream, ...);
        pluginFuncs.destroystream(&npp, NPRES_DONE);
        */

        printf("> NPP_URLNotify %s\n", requests[i].url);
        pluginFuncs.urlnotify(&npp, requests[i].url, NPRES_NETWORK_ERR, requests[i].notifyData);

        requests[i].completed = true;
    }
}
