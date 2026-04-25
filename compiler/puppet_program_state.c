/**
 * @file puppet_program_state.c
 * @brief Implementation of the shared program-state container.
 */

#include "puppet_program_state.h"
#include "puppet_memory.h"

#include <stdlib.h>

puppet_program_state_t *puppet_program_state_create(void) {
    puppet_program_state_t *prog = puppet_calloc(1, sizeof(*prog));
    if (!prog) return NULL;

    if (pthread_mutex_init(&prog->reg_mutex, NULL) != 0) {
        puppet_free(prog);
        return NULL;
    }
    prog->reg_mutex_initialised = true;
    return prog;
}

void puppet_program_state_destroy(puppet_program_state_t *prog) {
    if (!prog) return;

    if (prog->reg_mutex_initialised) {
        pthread_mutex_destroy(&prog->reg_mutex);
    }
    puppet_free(prog);
}
