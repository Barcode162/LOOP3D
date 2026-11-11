#pragma once
// What the world words share (the words themselves: words_*.cpp; the rest: scene_api.cpp).
#include "engine/script/scene_api.h"
#include <algorithm>
#include <map>
#include <unordered_map>
#include "engine/core/meter.h"
#include "engine/core/random.h"
#include "engine/core/file.h"
#include "engine/core/settings.h"
#include "engine/geometry/gltf.h"
#include "engine/geometry/plants.h"
#include "engine/geometry/terrain.h"
#include "engine/geometry/world_gen.h"
#include "engine/geometry/world_stream.h"
#include "engine/geometry/noise.h"
#include "engine/render/materials.h"
#include <functional>
#include <initializer_list>
#include <string>
#include <vector>

namespace loop::script {

Vec3 unitLuminanceOr(Vec3 c);
Vec3 makeStreamedWorld(Vm& vm, Stage& s, const WorldSettings& ws);

namespace words {

struct StoryValue {
  int kind = 0;  // 0 nothing, 1 number, 2 true/false, 3 text
  double n = 0;
  std::string s;
};
std::map<std::string, StoryValue>& storyMemory();
std::unordered_map<Vm*, Stage*>& stages();
Stage& stageOf(Vm& vm);
std::map<std::string, int>& meshCache(Stage& s);
Json settingValue(Vm& vm, const Value& v);
void setSetting(Vm& vm, const std::string& id, const Value& v);
int cachedMesh(Stage& st, const std::string& key, const std::function<Mesh()>& make);
std::string keyOf(const char* kind, std::initializer_list<float> v);
Value getMaterial(Vm& vm, Thing& t, const std::string& f, bool& found);
bool setMaterial(Vm& vm, Thing& t, const std::string& f, const Value& v);
extern const ThingClass kMaterialClass;
Value getShape(Vm& vm, Thing& t, const std::string& f, bool& found);
bool setShape(Vm& vm, Thing& t, const std::string& f, const Value& v);
extern const ThingClass kShapeClass;
Value getLight(Vm& vm, Thing& t, const std::string& f, bool& found);
bool setLight(Vm& vm, Thing& t, const std::string& f, const Value& v);
extern const ThingClass kLightClass;
Value getPerson(Vm& vm, Thing& t, const std::string& f, bool& found);
bool setPerson(Vm& vm, Thing& t, const std::string& f, const Value& v);
extern const ThingClass kPersonClass;
Value getSound(Vm& vm, Thing& t, const std::string& f, bool& found);
bool setSound(Vm& vm, Thing& t, const std::string& f, const Value& v);
extern const ThingClass kSoundClass;
Value getTerrain(Vm& vm, Thing& t, const std::string& f, bool& found);
extern const ThingClass kTerrainClass;
Value getGroup(Vm& vm, Thing& t, const std::string& f, bool& found);
bool setGroup(Vm& vm, Thing& t, const std::string& f, const Value& v);
extern const ThingClass kGroupClass;
Value getCritter(Vm& vm, Thing& t, const std::string& f, bool& found);
bool setCritter(Vm& vm, Thing& t, const std::string& f, const Value& v);
extern const ThingClass kCritterClass;
Material* matOf(Vm& vm, Thing& t);
Value getMaterial(Vm& vm, Thing& t, const std::string& f, bool& found);
bool applyMaterialSetting(Vm& vm, Material& m, const std::string& f, const Value& v);
bool setMaterial(Vm& vm, Thing& t, const std::string& f, const Value& v);
Stage::Placed& placedOf(Vm& vm, Thing& t);
Value getShape(Vm& vm, Thing& t, const std::string& f, bool& found);
Vec3 asVec(Vm& vm, const Value& v, const std::string& f);
int materialFrom(Vm& vm, const Value& v);
bool setShape(Vm& vm, Thing& t, const std::string& f, const Value& v);
Light& lightOf(Vm& vm, Thing& t);
Value getLight(Vm& vm, Thing& t, const std::string& f, bool& found);
bool setLight(Vm& vm, Thing& t, const std::string& f, const Value& v);
Value getTerrain(Vm& vm, Thing& t, const std::string& f, bool& found);
Stage::Person& personOf(Vm& vm, Thing& t);
Value getPerson(Vm& vm, Thing& t, const std::string& f, bool& found);
bool setPerson(Vm& vm, Thing& t, const std::string& f, const Value& v);
Value getSound(Vm& vm, Thing& t, const std::string& f, bool& found);
bool setSound(Vm& vm, Thing& t, const std::string& f, const Value& v);
Stage::Group& groupOf(Vm& vm, Thing& t);
Value getGroup(Vm& vm, Thing& t, const std::string& f, bool& found);
bool setGroup(Vm& vm, Thing& t, const std::string& f, const Value& v);
Stage::Critter& critterOf(Vm& vm, Thing& t);
Value getCritter(Vm& vm, Thing& t, const std::string& f, bool& found);
bool setCritter(Vm& vm, Thing& t, const std::string& f, const Value& v);
int materialFrom(Vm& vm, const Value& v);
Value place(Args& a, int mesh, char kind = 'm', Vec3 size = Vec3(1));
float f32(double d);
int placePart(Stage& s, int mesh, int mat, Vec3 at, float yaw, Vec3 scale, float tilt = 0);
Value newGroup(Vm& vm, Stage& s, std::vector<int> parts, Vec3 at);
float groundAt(Stage& s, float x, float z);
constexpr float kGltfNitsPerUnit = 8;
Material materialFromGltf(const GltfMaterial& g, const std::vector<int>& textures, float nitsPerUnit);
const std::vector<std::pair<int, int>>& modelParts(Args& a, const std::string& file, float nitsPerUnit);
std::pair<int, int> treeMeshes(Stage& st, const std::string& kind, int variant, float leafScale = 1, float detail = 1, int lod = 0);

void registerSettings(Vm& vm);
void registerMaterials(Vm& vm);
void registerShapes(Vm& vm);
void registerNature(Vm& vm);
void registerWorld(Vm& vm);
void registerLand(Vm& vm);
void registerLight(Vm& vm);
void registerCamera(Vm& vm);
void registerPhysics(Vm& vm);
void registerPeople(Vm& vm);
void registerSound(Vm& vm);
void registerMusic(Vm& vm);
void registerStory(Vm& vm);
void registerRenders(Vm& vm);
void registerAnim(Vm& vm);

}  // namespace words
}  // namespace loop::script
