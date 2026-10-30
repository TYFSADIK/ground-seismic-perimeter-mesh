// Copy to firmware/src/node_secrets.h (gitignored) and generate a key per deployment:
//   python3 -c "import secrets;print(', '.join(f'0x{b:02x}' for b in secrets.token_bytes(32)))"
#pragma once
#include <cstdint>
constexpr uint8_t NODE_ID = 1;
constexpr uint8_t MESH_KEY[32] = {0x00 /* ... 32 bytes ... */};
