/* Example stub of the generated datapoint table.
 *
 * The real table is generated from the private reverse-engineering data (see
 * CLAUDE.md and MAINTAINERS.md §0) and is NOT committed. At release time CI
 * writes the real firmware/main/alba_dp_table.h from the ALBA_DP_TABLE_H secret
 * (see .github/workflows/release-alba.yml); for local or CI compile-only builds,
 * copy this stub to alba_dp_table.h first.
 *
 * This stub carries no real datapoints, so a build made from it compiles and
 * links but recognises no addresses (every lookup returns NULL). Do not add real
 * datapoint addresses, labels or enum tables here.
 */
#ifndef ALBA_DP_TABLE_H
#define ALBA_DP_TABLE_H
#include "alba_cmd.h"
static const alba_dp_t ALBA_DPS[] = {
    {0x0000, NULL, ALBA_CMD, 0, 0, NULL}, /* placeholder — no real datapoints */
};
#define ALBA_DP_COUNT 0
#endif
