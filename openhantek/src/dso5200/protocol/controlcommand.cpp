#include "controlcommand.h"

namespace Hantek5200 {
ControlCommand::ControlCommand(ControlCode code, unsigned size): std::vector<uint8_t>(size), code((uint8_t)code) {}
}
