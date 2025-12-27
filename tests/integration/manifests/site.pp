# Integration test site manifest
# Each test class creates resources and markers for verification

node default {
  notify { 'test_start':
    message => 'Starting integration tests',
  }

  include test_file
  include test_exec
  include test_host
  include test_notify
  include test_cron
  include test_group
  include test_user
  include test_sysctl
  include test_mount
  include test_package
  include test_service
  include test_virtual

  notify { 'test_end':
    message => 'Integration tests completed',
  }
}
