# Test that node definitions work correctly
# This test verifies that resources inside node blocks are properly compiled

node default {
  notify { 'node_test_marker': message => 'NODE_TEST_WORKS' }
}
