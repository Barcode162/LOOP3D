// The world words: renders (made by the `loop` command with the cinema renderer) (registered by installSceneApi, scene_api.cpp).
#include "engine/script/words_internal.h"

namespace loop::script::words {

void registerRenders(Vm& vm) {
  // ---- renders (made by the `loop` command with the cinema renderer)
  vm.native("render", [](Args& a) {
    Stage& s = stageOf(a.vm);
    RenderRequest r;
    r.file = a.text(0, "file", s.title + ".png");
    const Config::Cinema& cc = config().cinema;
    std::string q = a.text(-1, "quality", cc.quality.empty() ? "good" : cc.quality);
    r.samples = q == "draft" ? 16 : q == "good" ? 128 : q == "cinema" ? 1024 : q == "ultra" ? 4096 : 128;
    if (cc.samples > 0) r.samples = cc.samples;
    r.samples = static_cast<int>(a.num(-1, "samples", r.samples));
    r.width = static_cast<int>(a.num(-1, "width", cc.width > 0 ? cc.width : 1920));
    r.height = static_cast<int>(a.num(-1, "height", cc.height > 0 ? cc.height : r.width * 9 / 16));
    r.bounces = cc.bounces, r.denoise = cc.denoise, r.dispersion = cc.dispersion, r.motionBlur = cc.motionBlur;
    r.at = f32(a.num(-1, "at", a.vm.time()));
    s.renders.push_back(r);
    return Value();
  }, "render(file, quality: \"draft\" | \"good\" | \"cinema\" | \"ultra\", width, height, samples)");
  vm.native("film", [](Args& a) {
    Stage& s = stageOf(a.vm);
    RenderRequest r;
    r.film = true;
    const Config::Cinema& cc = config().cinema;
    r.file = a.text(0, "folder", cc.folder);
    std::string q = a.text(-1, "quality", cc.quality.empty() ? "good" : cc.quality);
    r.samples = q == "draft" ? 8 : q == "good" ? 64 : q == "cinema" ? 512 : 64;
    if (cc.samples > 0) r.samples = cc.samples;
    r.samples = static_cast<int>(a.num(-1, "samples", r.samples));
    r.width = static_cast<int>(a.num(-1, "width", cc.width > 0 ? cc.width : 1280));
    r.height = static_cast<int>(a.num(-1, "height", cc.height > 0 ? cc.height : r.width * 9 / 16));
    r.bounces = cc.bounces, r.denoise = cc.denoise, r.dispersion = cc.dispersion, r.motionBlur = cc.motionBlur;
    s.film.fps = f32(a.num(-1, "fps", cc.fps));
    s.renders.push_back(r);
    return Value();
  }, "film(folder, quality, width, fps): renders every frame of the shots");
}

}  // namespace loop::script::words
