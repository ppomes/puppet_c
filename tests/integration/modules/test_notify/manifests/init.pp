# Test notify resource provider
class test_notify {
  # Test 1: Basic notification
  notify { "test_notification":
    message => "Notify resource test passed",
  }

  # Test 2: Another notification
  notify { "test_notification_2":
    message => "Second notify test",
  }
}
