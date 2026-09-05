// SPDX-License-Identifier: GPL-2.0+

#pragma once

#include <inttypes.h>
#include <vector>

namespace Hantek5200 {

enum class BulkCode : uint8_t;

class BulkCommand : public std::vector<uint8_t> {
protected:
    BulkCommand(BulkCode code, unsigned size);
public:
    BulkCode code;
    bool pending = false;
    BulkCommand* next = nullptr;
};

} // namespace Hantek5200
