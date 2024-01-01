// HomeKit setup QR code as SVG: for the Home Assistant notification (native API route) and the page /homekit
// on the ESP's web server (MQTT route). QR encoder: qrcodegen.c (Project Nayuki, MIT).
#include "esphome/core/defines.h"

#ifdef USE_AQUACLEAN_HOMEKIT

#include <string>

#include "qrcodegen.h"

namespace esphome::geberit_aquaclean {

std::string homekit_qr_svg(const char *text, int size_px) {
  uint8_t qr[qrcodegen_BUFFER_LEN_FOR_VERSION(6)];
  uint8_t tmp[qrcodegen_BUFFER_LEN_FOR_VERSION(6)];
  // medium error correction, as the Home app and the HomeKit specification use
  if (!qrcodegen_encodeText(text, tmp, qr, qrcodegen_Ecc_MEDIUM, 1, 6, qrcodegen_Mask_AUTO, true)) return "";
  const int n = qrcodegen_getSize(qr);
  const int border = 4, total = n + 2 * border;
  std::string svg;
  svg.reserve(2500);
  char buf[160];
  snprintf(buf, sizeof(buf),
           "<svg xmlns='http://www.w3.org/2000/svg' width='%d' height='%d' viewBox='0 0 %d %d' "
           "shape-rendering='crispEdges'>",
           size_px, size_px, total, total);
  svg += buf;
  snprintf(buf, sizeof(buf), "<rect width='%d' height='%d' fill='#fff'/><path fill='#000' d='", total, total);
  svg += buf;
  // one rectangle per run of dark modules in a row
  for (int y = 0; y < n; y++) {
    for (int x = 0; x < n;) {
      if (!qrcodegen_getModule(qr, x, y)) {
        x++;
        continue;
      }
      int run = 1;
      while (x + run < n && qrcodegen_getModule(qr, x + run, y)) run++;
      snprintf(buf, sizeof(buf), "M%d %dh%dv1h-%dz", x + border, y + border, run, run);
      svg += buf;
      x += run;
    }
  }
  svg += "'/></svg>";
  return svg;
}

}  // namespace esphome::geberit_aquaclean

#endif  // USE_AQUACLEAN_HOMEKIT
