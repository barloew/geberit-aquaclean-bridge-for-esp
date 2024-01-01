/*
 * aquaclean_bridge_id.example.c — template for the shared Geberit client credential.
 *
 * The real value is NOT in this repository. To build a working firmware:
 *   1. Copy this file to  aquaclean_bridge_id.c   (that name is git-ignored and never published).
 *   2. Replace the zeros with the real 16-byte aquacleanBridgeId.
 * Without the real value the v2 handshake will not authenticate. See firmware/README.md ("Configure").
 */
#include "arendi_common.h"

const uint8_t AQUACLEAN_BRIDGE_ID[AQUACLEAN_BRIDGE_ID_LEN] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};
