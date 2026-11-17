// zlib uitpakken met de decoder uit stb_image (alleen in de game; de launcher heeft dit niet nodig)
#include "net.h"
#define STBI_SUPPORT_ZLIB
#include "../third_party/stb_image.h"
#include <cstdlib>

namespace Net {
bool inflate(const std::string& in, std::string& out, size_t expect) {
    int len = 0;
    char* p = stbi_zlib_decode_malloc_guesssize_headerflag(in.data(), (int)in.size(), expect ? (int)expect : (int)in.size() * 4, &len, 1);
    if (!p) return false;
    out.assign(p, (size_t)len);
    free(p);
    return expect == 0 || out.size() == expect;
}
}  // namespace Net
