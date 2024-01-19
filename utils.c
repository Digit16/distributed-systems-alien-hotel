#include "utils.h"

char* process_to_text(ProcessType pt) {
    switch (pt) {
        case PROCESS_PURPLE_ALIEN: return "purple";
        case PROCESS_BLUE_ALIEN: return "blue";
        case PROCESS_CLEANER: return "cleaner";
        default: return "none";
    }
}