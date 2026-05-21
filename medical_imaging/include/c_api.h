#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct smachine_tag smachine_t;

smachine_t* smachine_create(void);
int          smachine_send_event(smachine_t* sm, int event_id);
int          smachine_get_state(smachine_t* sm);
uint64_t     smachine_get_deadline_violations(smachine_t* sm);
void         smachine_destroy(smachine_t* sm);

#define SMACHINE_CMD_START      1
#define SMACHINE_CMD_STOP       2
#define SMACHINE_CMD_RESET      3
#define SMACHINE_EVT_FRAME      4
#define SMACHINE_EVT_ERROR      5
#define SMACHINE_EVT_CALIB_DONE 6
#define SMACHINE_EVT_BATCH      7
#define SMACHINE_EVT_PROC_DONE  8

#define SMACHINE_OK                  0
#define SMACHINE_INVALID_TRANSITION  1
#define SMACHINE_ERR                -1

#define SMACHINE_STATE_IDLE        0
#define SMACHINE_STATE_CALIBRATING 1
#define SMACHINE_STATE_ACQUIRING   2
#define SMACHINE_STATE_PROCESSING  3
#define SMACHINE_STATE_FAULT       4

#ifdef __cplusplus
}
#endif
