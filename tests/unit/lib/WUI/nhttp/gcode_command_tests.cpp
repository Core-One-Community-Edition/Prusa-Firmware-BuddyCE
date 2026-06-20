#include <nhttp/gcode_command.h>
#include <nhttp/handler.h>

#include <catch2/catch.hpp>

#include <optional>
#include <string>

using nhttp::handler::ConnectionState;
using nhttp::handler::Continue;
using nhttp::handler::StatusPage;
using nhttp::handler::Step;
using nhttp::printer::GcodeCommand;
using std::get_if;
using std::nullopt;
using std::optional;
using std::string;
using std::string_view;

namespace {

// Records the last gcode passed to inject(), or nullopt if inject wasn't called.
optional<string> injected;
GcodeCommand::InjectResult inject_result = GcodeCommand::InjectResult::Submitted;

void do_test(string_view data, optional<string> expected_gcode) {
    injected.reset();

    GcodeCommand command(data.size(), true, false);
    Step result = { 0, 0, Continue() };
    command.step(data, false, nullptr, 0, result);

    REQUIRE(result.written == 0);
    REQUIRE(result.read == data.size());
    auto *state = get_if<ConnectionState>(&result.next);
    REQUIRE(state != nullptr);

    auto *page = get_if<StatusPage>(state);
    REQUIRE(page != nullptr);

    REQUIRE(injected == expected_gcode);
}

} // namespace

namespace nhttp::printer {

/*
 * In the real application, this calls into marlin and that would be pain to
 * deal with in tests. So we have a separate implementation of it here.
 */
GcodeCommand::InjectResult GcodeCommand::inject(const char *gcode) {
    injected = gcode;
    return inject_result;
}

} // namespace nhttp::printer

TEST_CASE("Simple gcode") {
    do_test("{\"gcode\": \"G28\"}", "G28");
}

TEST_CASE("Gcode with parameters") {
    do_test("{\"gcode\": \"M104 S210\"}", "M104 S210");
}

TEST_CASE("Extra fields ignored") {
    SECTION("Before") {
        do_test("{\"extra\": 42, \"gcode\": \"M115\"}", "M115");
    }

    SECTION("After") {
        do_test("{\"gcode\": \"M115\", \"extra\": 42}", "M115");
    }
}

TEST_CASE("Missing gcode is not injected") {
    do_test("{\"command\": \"M115\"}", nullopt);
}

TEST_CASE("Empty gcode is not injected") {
    do_test("{\"gcode\": \"\"}", nullopt);
}
