# Test tag statement and tagged() function

# Test 1: Basic tag statement
tag 'basic_tag'

notify { 'test1':
  message => 'Resource with basic tag',
}

# Test 2: Multiple tags
tag 'tag_one'
tag 'tag_two'

notify { 'test2':
  message => 'Resource with multiple tags',
}

# Test 3: tagged() function
tag 'webserver'

if tagged('webserver') {
  notify { 'test3':
    message => 'Tagged as webserver',
  }
}

# Test 4: tagged() returns false for non-existent tag
if !tagged('nonexistent') {
  notify { 'test4':
    message => 'Not tagged as nonexistent',
  }
}
