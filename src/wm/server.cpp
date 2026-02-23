#include "wlr_bridge.h"

int main() {
    havel_wlr_server_t* server = havel_wlr_create();
    if (!server) {
        return 1;
    }

    int rc = havel_wlr_run(server);
    havel_wlr_destroy(server);
    return rc;
}
