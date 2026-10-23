#include "../smalltv-pomodoro/gifcheck.h"
#include <vector>
#include <cassert>
#include <cstdio>
#include <algorithm>

struct Input { const std::vector<uint8_t> &data; size_t pos = 0; };
static int readInput(void *ctx, uint8_t *buf, int n) {
  auto &in = *(Input *)ctx;
  n = std::min(n, (int)(in.data.size() - in.pos));
  if (n) memcpy(buf, in.data.data() + in.pos, n);
  in.pos += n;
  return n;
}
static bool accepted(const std::vector<uint8_t> &v) {
  Input in{v};
  return !gifCheck(readInput, &in, 480, 480);
}
int main() {
  // One black pixel: clear, literal 0, end-code, and a complete data terminator/trailer.
  const std::vector<uint8_t> valid = {'G','I','F','8','9','a',1,0,1,0,0x80,0,0,
    0,0,0,255,255,255,0x2c,0,0,0,0,1,0,1,0,0,2,2,0x44,1,0,0x3b};
  assert(accepted(valid));
  for (size_t n = 0; n < valid.size(); n++) {
    assert(!accepted({valid.begin(), valid.begin() + n}));
  }
  auto v = valid; v[6] = 0; assert(!accepted(v));
  v = valid; v[6] = 225; v[7] = 1; assert(!accepted(v)); // 481 wide
  v = valid; v[20] = 1; assert(!accepted(v)); // frame outside canvas
  v = valid; v[24] = 0; assert(!accepted(v)); // zero-width frame
  for (int size : {0, 1, 9, 255}) { v = valid; v[29] = size; assert(!accepted(v)); }
  v = valid; v[10] = 0; v.erase(v.begin() + 13, v.begin() + 19); assert(!accepted(v));
  // A valid first frame cannot hide a truncated second frame.
  v = valid; v.pop_back(); v.push_back(0x2c); assert(!accepted(v));
  const uint8_t gce[] = {0x21,0xf9,4,1,10,0,0,0};
  v = valid; v.insert(v.begin() + 19, gce, gce + sizeof gce); assert(accepted(v));
  v[21] = 3; assert(!accepted(v));
  v = valid; v.insert(v.begin() + 19, {0x21,0x7f,0}); assert(!accepted(v));
  // Long comments use bounded sub-blocks; exercise more than the decoder's file buffer.
  v = valid; std::vector<uint8_t> ext{0x21,0xfe};
  for (int n = 0; n < 20; n++) { ext.push_back(255); ext.insert(ext.end(), 255, 0x41); }
  ext.push_back(0); v.insert(v.begin() + 19, ext.begin(), ext.end()); assert(accepted(v));
  puts("gifcheck: valid, truncated, dimensions, palettes, extensions and LZW-size tests passed");
}
