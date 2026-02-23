#pragma once

#ifdef __cplusplus
extern "C" {
#endif

struct havel_wlr_server;

typedef struct havel_wlr_server havel_wlr_server_t;

havel_wlr_server_t* havel_wlr_create(void);
void havel_wlr_destroy(havel_wlr_server_t* server);

int havel_wlr_run(havel_wlr_server_t* server);

#ifdef __cplusplus
}
#endif
