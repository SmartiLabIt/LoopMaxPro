#include "services/web/web_bundle.h"

// Written by tools/pio/web.py in the folder of the build: kWebAssets and kWebAssetCount.
#if defined(LMP_WEB_BUNDLE)
#include "lmp_web_bundle.h"
#endif

namespace lmp {
namespace {

#if defined(LMP_WEB_BUNDLE)
const WebAsset* g_assets = webgen::kWebAssets;
size_t g_count = webgen::kWebAssetCount;
#else
const WebAsset* g_assets = nullptr;
size_t g_count = 0;
#endif

}  // namespace

const WebAsset* webBundleFind(StringView path) {
  for (size_t i = 0; i < g_count; ++i) {
    if (path == StringView(g_assets[i].path)) return &g_assets[i];
  }
  return nullptr;
}

size_t webBundleCount() { return g_count; }

const WebAsset& webBundleAt(size_t index) { return g_assets[index]; }

void webBundleReplace(const WebAsset* assets, size_t count) {
#if defined(LMP_WEB_BUNDLE)
  g_assets = assets ? assets : webgen::kWebAssets;
  g_count = assets ? count : webgen::kWebAssetCount;
#else
  g_assets = assets;
  g_count = assets ? count : 0;
#endif
}

}  // namespace lmp
