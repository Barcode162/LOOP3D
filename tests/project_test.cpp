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
