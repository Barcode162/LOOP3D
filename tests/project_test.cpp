// A project's cover page (engine/project): the .looped file beside its scenes.
#include "engine/project/project.h"
#include "tests/test.h"

using namespace loop;

TEST(project_cover_page_reads) {
  Project p;
  std::vector<ProjectProblem> ps;
  CHECK(parseProject("# The Bar\nproject \"The Bar\"\nstart \"bar_room\"   # the first scene\n", p, ps));
  CHECK(ps.empty());
  CHECK(p.name == "The Bar");
  CHECK(p.start == "bar_room");
}

TEST(project_cover_page_problems_say_where_and_what) {
  Project p;
  std::vector<ProjectProblem> ps;
  CHECK(!parseProject("project \"The Bar\"\nstrat \"bar_room\"\n", p, ps));
  CHECK(ps.size() == 2);
  CHECK(ps[0].line == 2);
  CHECK(ps[0].text.find("did you mean \"start\"") != std::string::npos);
  CHECK(ps[1].message("bar.looped").find("no first scene") != std::string::npos);

  ps.clear();
  CHECK(!parseProject("project The Bar\nstart \"a\"\nstart \"b\"\n", p, ps));
  CHECK(ps.size() == 2);  // no quotes; given twice
  CHECK(ps[0].text.find("double quotes") != std::string::npos);
  CHECK(ps[1].line == 3);
}

TEST(project_cover_page_settings_are_checked) {
  Project p;
  std::vector<ProjectProblem> ps;
  CHECK(parseProject("project \"A\"\nstart \"a\"\n"
                     "setting \"player.walk\", 2.5     # a note\n"
                     "setting \"render.filmGrain\", no\n"
                     "setting \"physics.gravity\", [0, -3.7, 0]\n",
                     p, ps));
  CHECK(ps.empty());
  CHECK(p.settings.size() == 3);
  CHECK(p.settings[0].name == "player.walk" && p.settings[0].value.number() == 2.5);
  CHECK(p.settings[1].value.type() == Json::Type::kBool && !p.settings[1].value.boolean());
  CHECK(p.settings[2].value.size() == 3);

  ps.clear();
  Project q;
  CHECK(!parseProject("project \"A\"\nstart \"a\"\n"
                      "setting \"player.wlak\", 2\n"       // misspelt
                      "setting \"player.walk\", 500\n"     // too high
                      "setting \"player.walk\", fast\n"    // not a value
                      "setting \"player.run\", \"fast\"\n"   // words where a number belongs
                      "setting player.run, 4\n",           // no quotes
                      q, ps));
  CHECK(ps.size() == 5);
  if (ps.size() != 5) return;
  CHECK(ps[0].line == 3 && ps[0].text.find("did you mean \"player.walk\"") != std::string::npos);
  CHECK(ps[1].text.find("takes 0.1 to 20") != std::string::npos);
  CHECK(ps[2].text.find("needs a value") != std::string::npos);
  CHECK(ps[3].text.find("takes a number") != std::string::npos);
  CHECK(ps[4].text.find("double quotes") != std::string::npos);
  CHECK(q.settings.empty());  // nothing wrong is kept
}

// The template every new project is copied from always reads cleanly, every
// setting valid, its first room in places/.
TEST(project_template_reads_cleanly) {
  std::string here = __FILE__;
  std::string root = here.substr(0, here.rfind("tests/"));
  Project p;
  std::vector<ProjectProblem> ps;
  CHECK(readProject(root + "projects/template/template.looped", p, ps));
  for (const ProjectProblem& q : ps) std::printf("    %s\n", q.message("template.looped").c_str());
  CHECK(p.name == "New Project");
  CHECK(p.settings.size() == 16);
  CHECK(p.scenePath(p.start).find("places/first_room.loop") != std::string::npos);
}
