#include "c_api.h"
#include "RtEngine.h"
#include <mutex>

struct smachine_tag {
    imaging::RtEngine engine;
    std::mutex        mutex;
};

smachine_t* smachine_create() {
    return new smachine_tag{};
}

int smachine_send_event(smachine_t* sm, int event_id) {
    if (!sm) return SMACHINE_ERR;
    std::lock_guard<std::mutex> lock(sm->mutex);
    return sm->engine.sendCommand(event_id);
}

int smachine_get_state(smachine_t* sm) {
    if (!sm) return SMACHINE_ERR;
    return sm->engine.getStateId();
}

uint64_t smachine_get_deadline_violations(smachine_t* sm) {
    if (!sm) return 0;
    return sm->engine.deadlineViolations();
}

void smachine_destroy(smachine_t* sm) {
    delete sm;
}
