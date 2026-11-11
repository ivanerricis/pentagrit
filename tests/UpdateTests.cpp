// SPDX-License-Identifier: AGPL-3.0-or-later
// None of these tests touches the network: the pure functions are tested directly and the checker and the
// settings use injected fake fetchers.
#include "PluginInfo.h"
#include "settings/SettingsManager.h"
#include "update/UpdateChecker.h"
#include <atomic>
#include <catch2/catch_test_macros.hpp>
#include <iterator>
#include <mutex>
#include <stdexcept>
#include <vector>
#include <juce_events/juce_events.h>

using namespace pentagrit;
using namespace pentagrit::update;

namespace
{
FetchOutcome blockedFetch()
{
    FetchOutcome o;
    o.error = "network disabled in tests";
    return o;
}

// The whole suite runs with a fetcher that fails instead of connecting.
[[maybe_unused]] const bool kNetworkBlocked = []
{
    SettingsManager::setUpdateFetcherOverride(&blockedFetch);
    return true;
}();

// Installs a fake fetcher for the SettingsManagers created while it lives; restores the blocking one.
struct FakeFetcher
{
    explicit FakeFetcher(Fetcher f) { SettingsManager::setUpdateFetcherOverride(std::move(f)); }
    ~FakeFetcher() { SettingsManager::setUpdateFetcherOverride(&blockedFetch); }
};

struct TempFile
{
    juce::File file = juce::File::createTempFile(".settings");
    ~TempFile() { file.deleteFile(); }
};

FetchOutcome release(const juce::String& tag)
{
    FetchOutcome o;
    o.ok = true;
    o.status = 200;
    o.body = "{\"tag_name\":\"" + tag +
             "\",\"html_url\":\"https://github.com/ivanerricis/pentagrit/releases/tag/" + tag +
             "\",\"name\":\"x\",\"draft\":false}";
    return o;
}

// Results are normally posted with MessageManager::callAsync; plugin builds have no modal dispatch loop to
// pump in a test, so the suite collects the posted calls and runs them here, on the test (message) thread.
struct Mailbox
{
    std::mutex mutex;
    std::vector<std::function<void()>> queue;

    void post(std::function<void()> fn)
    {
        const std::lock_guard<std::mutex> lock(mutex);
        queue.push_back(std::move(fn));
    }
    void drain()
    {
        std::vector<std::function<void()>> work;
        {
            const std::lock_guard<std::mutex> lock(mutex);
            work.swap(queue);
        }
        for (auto& f : work)
            f();
    }
};

Mailbox& mailbox()
{
    static Mailbox m;
    return m;
}

[[maybe_unused]] const bool kMailboxInstalled = []
{
    update::setPosterForTests([](std::function<void()> fn) { mailbox().post(std::move(fn)); });
    return true;
}();

void pump(int ms)
{
    const auto end = juce::Time::getMillisecondCounter() + static_cast<juce::uint32>(ms);
    do
    {
        mailbox().drain();
        juce::Thread::sleep(5);
    } while (juce::Time::getMillisecondCounter() < end);
}

template <class Pred> bool waitFor(Pred pred, int timeoutMs = 5000)
{
    const auto end = juce::Time::getMillisecondCounter() + static_cast<juce::uint32>(timeoutMs);
    while (!pred() && juce::Time::getMillisecondCounter() < end)
    {
        mailbox().drain();
        juce::Thread::sleep(5);
    }
    mailbox().drain();
    return pred();
}

int cmp(const char* a, const char* b)
{
    const auto va = parseVersion(a);
    const auto vb = parseVersion(b);
    REQUIRE(va.has_value());
    REQUIRE(vb.has_value());
    return compareVersions(*va, *vb);
}
} // namespace

TEST_CASE("Version parsing", "[update]")
{
    SECTION("valid")
    {
        auto v = parseVersion("v0.2.0");
        REQUIRE(v);
        CHECK(v->major == 0);
        CHECK(v->minor == 2);
        CHECK(v->patch == 0);
        CHECK(v->pre.isEmpty());

        v = parseVersion("0.1.0-rc2");
        REQUIRE(v);
        CHECK(v->pre == juce::StringArray{"rc2"});

        v = parseVersion("  V10.20.30-alpha.1.x-y+build.5 ");
        REQUIRE(v);
        CHECK(v->major == 10);
        CHECK(v->minor == 20);
        CHECK(v->patch == 30);
        CHECK(v->pre == juce::StringArray{"alpha", "1", "x-y"});

        CHECK(parseVersion(info::kVersion).has_value()); // the running version must be understood
    }
    SECTION("garbage is rejected")
    {
        for (const char* bad : {"",        " ",          "v",
                                "latest",  "1",          "1.2",
                                "1.2.3.4", "1..3",       ".1.2",
                                "1.2.",    "1.2.x",      "1.2.3-",
                                "1.2.3-.", "1.2.3-a..b", "1.2.3-a b",
                                "-1.2.3",  "1.-2.3",     "01.2.3",
                                "1.02.3",  "1.2.03",     "1.2.3-01",
                                "1.2.3+",  "1.2.3+a b",  "vv1.2.3",
                                "1.2.3 4", "1.2.3-é",    "99999999999.0.0",
                                "<html>",  "1,2,3"})
        {
            INFO("input: \"" << bad << "\"");
            CHECK_FALSE(parseVersion(bad).has_value());
        }
    }
}

TEST_CASE("Version comparison follows semantic versioning", "[update]")
{
    CHECK(cmp("0.1.0-rc2", "0.1.0") < 0);
    CHECK(cmp("0.1.0", "0.1.1") < 0);
    CHECK(cmp("0.1.0-rc2", "0.1.1") < 0);
    CHECK(cmp("0.1.0", "0.1.0") == 0);
    CHECK(cmp("v0.1.0", "0.1.0") == 0);
    CHECK(cmp("0.1.0+a", "0.1.0+b") == 0); // build metadata is ignored
    CHECK(cmp("0.9.0", "0.10.0") < 0);     // numeric, not textual
    CHECK(cmp("1.0.0", "0.99.99") > 0);
    CHECK(cmp("2.0.0", "10.0.0") < 0);

    // The example chain of the semver specification.
    const char* chain[] = {"1.0.0-alpha",  "1.0.0-alpha.1", "1.0.0-alpha.beta", "1.0.0-beta",
                           "1.0.0-beta.2", "1.0.0-beta.11", "1.0.0-rc.1",       "1.0.0"};
    for (size_t i = 0; i + 1 < std::size(chain); ++i)
    {
        INFO(chain[i] << " < " << chain[i + 1]);
        CHECK(cmp(chain[i], chain[i + 1]) < 0);
        CHECK(cmp(chain[i + 1], chain[i]) > 0);
    }
}

TEST_CASE("Latest release parsing", "[update]")
{
    juce::String why;
    SECTION("valid")
    {
        const auto r = parseLatestRelease(
            "{\"url\":\"https://api.github.com/x\",\"tag_name\":\"v0.2.0\",\"html_url\":"
            "\"https://github.com/ivanerricis/pentagrit/releases/tag/v0.2.0\",\"assets\":[{\"a\":1}]}",
            &why);
        REQUIRE(r);
        CHECK(r->tag == "v0.2.0");
        CHECK(r->url == "https://github.com/ivanerricis/pentagrit/releases/tag/v0.2.0");
    }
    SECTION("rejected")
    {
        const juce::String ok =
            "\"html_url\":\"https://github.com/ivanerricis/pentagrit/releases/tag/v1.0.0\"";
        const std::vector<juce::String> cases{
            juce::String(),
            "not json",
            "[]",
            "\"v1.0.0\"",
            "{}",
            "{\"tag_name\":\"v1.0.0\"}",
            "{" + ok + "}",
            "{\"tag_name\":1," + ok + "}",
            "{\"tag_name\":\"latest\"," + ok + "}",
            "{\"tag_name\":\"\"," + ok + "}",
            "{\"tag_name\":\"v1.0.0\",\"html_url\":\"http://github.com/ivanerricis/pentagrit/releases\"}",
            "{\"tag_name\":\"v1.0.0\",\"html_url\":\"https://evil.example/ivanerricis/pentagrit/\"}",
            "{\"tag_name\":\"v1.0.0\",\"html_url\":\"https://github.com.evil.example/ivanerricis/pentagrit/"
            "\"}",
            "{\"tag_name\":\"v1.0.0\",\"html_url\":\"https://github.com/someone-else/pentagrit/\"}",
            "{\"tag_name\":\"v1.0.0\",\"html_url\":\"javascript:alert(1)\"}",
            "{\"tag_name\":\"v1.0.0\",\"html_url\":null}"};
        for (const auto& bad : cases)
        {
            INFO("input: " << bad);
            why.clear();
            CHECK_FALSE(parseLatestRelease(bad, &why).has_value());
            CHECK(why.isNotEmpty());
        }
    }
}

TEST_CASE("Check schedule", "[update]")
{
    const juce::int64 now = 1'800'000'000'000LL;
    CHECK(isCheckDue(0, now));
    CHECK(isCheckDue(-5, now));
    CHECK_FALSE(isCheckDue(now, now));
    CHECK_FALSE(isCheckDue(now - kCheckIntervalMs + 1, now));
    CHECK(isCheckDue(now - kCheckIntervalMs, now));
    CHECK(isCheckDue(now - 3 * kCheckIntervalMs, now));
    CHECK(isCheckDue(now + 1000, now)); // the clock moved backwards
}

TEST_CASE("Responses are evaluated against the running version", "[update]")
{
    using S = UpdateResult::Status;
    SECTION("newer release")
    {
        const auto r = evaluate(release("v0.2.0"), "0.1.0");
        CHECK(r.status == S::updateAvailable);
        CHECK(r.version == "0.2.0");
        CHECK(r.url == "https://github.com/ivanerricis/pentagrit/releases/tag/v0.2.0");
    }
    SECTION("same or older release")
    {
        CHECK(evaluate(release("v0.1.0"), "0.1.0").status == S::upToDate);
        const auto r = evaluate(release("v0.1.0"), "0.2.0"); // a development build
        CHECK(r.status == S::upToDate);
        CHECK(r.url.isEmpty());
        CHECK(evaluate(release("v0.1.0-rc2"), "0.1.0").status == S::upToDate);
    }
    SECTION("a release is newer than the running release candidate")
    {
        CHECK(evaluate(release("v0.1.0"), "0.1.0-rc2").status == S::updateAvailable);
    }
    SECTION("failures")
    {
        FetchOutcome down;
        down.error = "Timed out";
        auto r = evaluate(down, "0.1.0");
        CHECK(r.status == S::failed);
        CHECK(r.reason == "Timed out");

        FetchOutcome o;
        o.ok = true;
        o.status = 404;
        CHECK(evaluate(o, "0.1.0").status == S::failed);
        o.status = 403;
        CHECK(evaluate(o, "0.1.0").reason.containsIgnoreCase("limiting"));
        o.status = 500;
        CHECK(evaluate(o, "0.1.0").reason.contains("500"));
        o.status = 200;
        o.body = "<html>captive portal</html>";
        CHECK(evaluate(o, "0.1.0").status == S::failed);
        CHECK(evaluate(release("v9.9.9"), "garbage").status == S::failed);
        CHECK(evaluate(FetchOutcome{}, "0.1.0").reason.isNotEmpty());
    }
}

TEST_CASE("Checker runs the request in the background and delivers the result to the caller's thread",
          "[update]")
{
    std::atomic<int> fetches{0};
    std::atomic<juce::Thread::ThreadID> fetchThread{nullptr};
    UpdateChecker checker(
        [&]
        {
            ++fetches;
            fetchThread = juce::Thread::getCurrentThreadId();
            juce::Thread::sleep(50);
            return release("v3.0.0");
        });

    bool delivered = false;
    juce::Thread::ThreadID callbackThread = nullptr;
    UpdateResult result;
    REQUIRE(checker.check("0.1.0",
                          [&](const UpdateResult& r)
                          {
                              delivered = true;
                              callbackThread = juce::Thread::getCurrentThreadId();
                              result = r;
                          }));
    CHECK(checker.isBusy());
    CHECK_FALSE(checker.check("0.1.0", [](const UpdateResult&) { FAIL("second check must not run"); }));
    REQUIRE(waitFor([&] { return delivered; }));
    CHECK(callbackThread == juce::Thread::getCurrentThreadId());     // delivered where the mailbox is drained
    CHECK(fetchThread.load() != juce::Thread::getCurrentThreadId()); // the request never runs here
    CHECK(result.status == UpdateResult::Status::updateAvailable);
    CHECK(result.version == "3.0.0");
    CHECK_FALSE(checker.isBusy());
    CHECK(fetches == 1);

    // A new check is possible afterwards.
    delivered = false;
    REQUIRE(checker.check("3.0.0",
                          [&](const UpdateResult& r)
                          {
                              delivered = true;
                              result = r;
                          }));
    REQUIRE(waitFor([&] { return delivered; }));
    CHECK(result.status == UpdateResult::Status::upToDate);
    CHECK(fetches == 2);
}

TEST_CASE("Checker reports a failing or throwing fetcher", "[update]")
{
    UpdateResult result;
    bool delivered = false;
    SECTION("failure")
    {
        UpdateChecker checker([] { return blockedFetch(); });
        REQUIRE(checker.check("0.1.0",
                              [&](const UpdateResult& r)
                              {
                                  delivered = true;
                                  result = r;
                              }));
        REQUIRE(waitFor([&] { return delivered; }));
        CHECK(result.status == UpdateResult::Status::failed);
        CHECK(result.reason == "network disabled in tests");
    }
    SECTION("exception")
    {
        UpdateChecker checker([]() -> FetchOutcome { throw std::runtime_error("boom"); });
        REQUIRE(checker.check("0.1.0",
                              [&](const UpdateResult& r)
                              {
                                  delivered = true;
                                  result = r;
                              }));
        REQUIRE(waitFor([&] { return delivered; }));
        CHECK(result.status == UpdateResult::Status::failed);
    }
}

TEST_CASE("Destroying the checker mid-request never calls back", "[update]")
{
    std::atomic<bool> started{false};
    bool called = false;
    {
        UpdateChecker checker(
            [&]
            {
                started = true;
                juce::Thread::sleep(150);
                return release("v3.0.0");
            });
        REQUIRE(checker.check("0.1.0", [&](const UpdateResult&) { called = true; }));
        REQUIRE(waitFor([&] { return started.load(); }));
    } // joins the thread
    pump(100); // flush anything already posted
    CHECK_FALSE(called);
}

TEST_CASE("Automatic check is opt-in and at most once a day", "[update][settings]")
{
    std::atomic<int> fetches{0};
    std::atomic<const char*> tag{"v9.0.0"};
    FakeFetcher fake(
        [&]
        {
            ++fetches;
            return release(tag.load());
        });
    TempFile tmp;
    SettingsManager m(tmp.file);
    using Phase = SettingsManager::UpdateState::Phase;
    auto done = [&] { return m.getUpdateState().phase == Phase::done; };

    CHECK_FALSE(m.get().checkUpdates);
    CHECK(m.get().lastUpdateCheckMs == 0);
    CHECK(m.getUpdateState().phase == Phase::idle);

    SECTION("off by default: no request")
    {
        m.checkForUpdatesIfDue();
        pump(100);
        CHECK(fetches == 0);
        CHECK(m.getUpdateState().phase == Phase::idle);
        CHECK(m.get().lastUpdateCheckMs == 0);
    }
    SECTION("on: one check, stored, then not again within a day")
    {
        auto s = m.get();
        s.checkUpdates = true;
        m.set(s);

        const auto before = juce::Time::currentTimeMillis();
        m.checkForUpdatesIfDue();
        REQUIRE(waitFor(done));
        CHECK(fetches == 1);
        CHECK(m.getUpdateState().result.status == update::UpdateResult::Status::updateAvailable);
        CHECK(m.getUpdateState().result.version == "9.0.0");
        CHECK(m.get().lastUpdateCheckMs >= before);

        // persisted
        SettingsManager again(tmp.file);
        CHECK(again.get().lastUpdateCheckMs == m.get().lastUpdateCheckMs);
        CHECK(again.get().checkUpdates);

        m.checkForUpdatesIfDue();
        pump(100);
        CHECK(fetches == 1);

        // a day later it is due again
        s = m.get();
        s.lastUpdateCheckMs = juce::Time::currentTimeMillis() - kCheckIntervalMs - 1000;
        m.set(s);
        tag = "v0.0.1";
        m.checkForUpdatesIfDue();
        REQUIRE(
            waitFor([&] { return fetches == 2 && done() && m.getUpdateState().result.version == "0.0.1"; }));
        CHECK(m.getUpdateState().result.status == update::UpdateResult::Status::upToDate);
    }
    SECTION("Check now ignores the opt-in and the schedule")
    {
        CHECK(m.checkForUpdatesNow());
        REQUIRE(waitFor(done));
        CHECK(fetches == 1);
        CHECK(m.get().lastUpdateCheckMs == 0); // manual checks do not touch the daily schedule

        std::atomic<int> notified{0};
        struct L : juce::ChangeListener
        {
            std::atomic<int>* n;
            void changeListenerCallback(juce::ChangeBroadcaster*) override { ++*n; }
        } listener;
        listener.n = &notified;
        m.updateChanges().addChangeListener(&listener);
        CHECK(m.checkForUpdatesNow());
        CHECK(m.getUpdateState().phase == Phase::checking);
        CHECK_FALSE(m.checkForUpdatesNow()); // already running
        REQUIRE(waitFor([&] { return fetches == 2 && done(); }));
        CHECK(notified >= 1);
        m.updateChanges().removeChangeListener(&listener);
    }
    SECTION("a failed check is reported")
    {
        FakeFetcher failing([] { return blockedFetch(); });
        SettingsManager other(tmp.file);
        CHECK(other.checkForUpdatesNow());
        REQUIRE(waitFor([&] { return other.getUpdateState().phase == Phase::done; }));
        CHECK(other.getUpdateState().result.status == update::UpdateResult::Status::failed);
    }
}

TEST_CASE("The default settings never reach the network in tests", "[update][settings]")
{
    // The suite installs a blocking fetcher: even an explicit check fails instead of connecting.
    TempFile tmp;
    SettingsManager m(tmp.file);
    CHECK(m.checkForUpdatesNow());
    REQUIRE(waitFor([&] { return m.getUpdateState().phase == SettingsManager::UpdateState::Phase::done; }));
    CHECK(m.getUpdateState().result.status == update::UpdateResult::Status::failed);
    CHECK(m.getUpdateState().result.reason == "network disabled in tests");
}
