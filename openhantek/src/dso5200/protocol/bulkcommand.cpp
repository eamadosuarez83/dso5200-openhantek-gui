#include "bulkcommand.h"

namespace Hantek5200 {
BulkCommand::BulkCommand(BulkCode code, unsigned size): std::vector<uint8_t>(size), code(code) {}
}
