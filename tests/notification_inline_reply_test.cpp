#include "notification/notification_manager.h"
#include "tests/test_check.h"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace {

  struct ReplyCall {
    uint32_t id{0};
    std::string text;
    std::string token;
  };

  uint32_t addNotification(
      NotificationManager& manager, std::string summary, std::vector<std::string> actions, int32_t timeout = 0
  ) {
    return manager.addOrReplace(
        NotificationRequest{
            .appName = "chat-app",
            .summary = std::move(summary),
            .body = "Hey there",
            .timeout = timeout,
            .actions = std::move(actions),
        }
    );
  }

} // namespace

int main() {
  NotificationManager manager;
  std::vector<ReplyCall> replies;
  std::vector<std::pair<uint32_t, CloseReason>> closes;

  manager.setReplyCallback([&replies](uint32_t id, const std::string& text, const std::string& token) {
    replies.push_back({id, text, token});
  });
  manager.setCloseCallback([&closes](uint32_t id, CloseReason reason) { closes.emplace_back(id, reason); });

  // 1. Successful inline reply on an active notification
  const uint32_t notif1 = addNotification(manager, "msg-1", {"inline-reply", "Reply"});
  TEST_CHECK(manager.all().size() == 1);
  TEST_CHECK(manager.invokeInlineReply(notif1, "Hello back!", "activation-token-123", true));

  TEST_CHECK(replies.size() == 1);
  TEST_CHECK(replies[0].id == notif1);
  TEST_CHECK(replies[0].text == "Hello back!");
  TEST_CHECK(replies[0].token == "activation-token-123");

  // Notification was dismissed upon replying
  TEST_CHECK(manager.all().empty());
  TEST_CHECK(manager.history().size() == 1);
  TEST_CHECK(!manager.history().front().active);
  TEST_CHECK(closes.size() == 1);
  TEST_CHECK(closes[0].first == notif1);
  TEST_CHECK(closes[0].second == CloseReason::Dismissed);

  // 2. Overload without activation token passes empty token
  const uint32_t notif2 = addNotification(manager, "msg-2", {"inline-reply", "Reply"});
  TEST_CHECK(manager.invokeInlineReply(notif2, "Second reply", true));
  TEST_CHECK(replies.size() == 2);
  TEST_CHECK(replies[1].id == notif2);
  TEST_CHECK(replies[1].text == "Second reply");
  TEST_CHECK(replies[1].token.empty());

  // 3. Blank or empty reply text is rejected
  const uint32_t notif3 = addNotification(manager, "msg-3", {"inline-reply", "Reply"});
  TEST_CHECK(!manager.invokeInlineReply(notif3, "", "token"));
  TEST_CHECK(!manager.invokeInlineReply(notif3, "   \t\n  ", "token"));
  TEST_CHECK(replies.size() == 2);
  TEST_CHECK(manager.all().size() == 1);

  // 4. Notification without "inline-reply" action cannot be replied to
  const uint32_t notif4 = addNotification(manager, "msg-4", {"default", "Open", "archive", "Archive"});
  TEST_CHECK(!manager.invokeInlineReply(notif4, "Cannot reply", "token"));
  TEST_CHECK(replies.size() == 2);

  // 5. Bare "inline-reply" via invokeAction is rejected
  std::vector<std::string> invokedActions;
  manager.setActionInvokeCallback([&invokedActions](uint32_t, const std::string& key, const std::string&) {
    invokedActions.push_back(key);
  });
  TEST_CHECK(!manager.invokeAction(notif3, "inline-reply"));
  TEST_CHECK(invokedActions.empty());

  // 6. Unknown notification ID is rejected
  TEST_CHECK(!manager.invokeInlineReply(999999, "Ghost", "token"));
  TEST_CHECK(replies.size() == 2);

  // 7. Reply on an expired notification in history with pending close
  const uint32_t notif5 = addNotification(manager, "msg-5", {"inline-reply", "Reply"}, 1000);
  TEST_CHECK(manager.close(notif5, CloseReason::Expired));
  TEST_CHECK(manager.hasPendingDBusClose(notif5));

  TEST_CHECK(manager.invokeInlineReply(notif5, "Replied from history", "token-hist", true));
  TEST_CHECK(replies.size() == 3);
  TEST_CHECK(replies[2].id == notif5);
  TEST_CHECK(replies[2].text == "Replied from history");
  TEST_CHECK(replies[2].token == "token-hist");
  TEST_CHECK(!manager.hasPendingDBusClose(notif5));

  // Second reply on the same expired notification fails because pending close was consumed
  TEST_CHECK(!manager.invokeInlineReply(notif5, "Duplicate reply", "token-hist", true));
  TEST_CHECK(replies.size() == 3);

  // 8. Internal notifications do not trigger external reply callback
  const uint32_t internalNotif = manager.addOrReplace(
      NotificationRequest{
          .appName = "shell",
          .summary = "Internal",
          .origin = NotificationOrigin::Internal,
          .persistInHistory = true,
          .actions = {"inline-reply", "Reply"},
      }
  );
  TEST_CHECK(manager.invokeInlineReply(internalNotif, "Internal reply", "token", true));
  TEST_CHECK(replies.size() == 3);

  return 0;
}
