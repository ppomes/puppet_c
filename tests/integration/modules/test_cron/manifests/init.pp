# Test cron resource provider
class test_cron {
  # Test 1: Create a simple cron job
  cron { "test_cron_simple":
    command => "/bin/echo test_cron_simple >> /tmp/cron_test.log",
    minute  => "0",
    hour    => "3",
  }

  # Test 2: Create a cron job with all time fields
  cron { "test_cron_full":
    command  => "/bin/echo test_cron_full",
    minute   => "30",
    hour     => "2",
    monthday => "15",
    month    => "6",
    weekday  => "1",
  }

  # Test 3: Create a cron job with special schedule
  cron { "test_cron_special":
    command => "/bin/echo test_cron_special",
    special => "daily",
  }

  # Test 4: Cron job to be absent
  cron { "test_cron_absent":
    ensure  => absent,
    command => "/bin/echo should_not_exist",
  }
}
