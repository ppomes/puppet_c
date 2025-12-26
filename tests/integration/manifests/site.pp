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

  notify { 'test_end':
    message => 'Integration tests completed',
  }
}
