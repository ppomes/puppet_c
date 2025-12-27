# Test each() iterator function

notice("=== Iterator Tests ===")

# Test 1: each with array and single parameter
$servers = ['web1', 'web2', 'web3']
notice("Test 1: each with array (single param)")
each($servers) |$server| {
  notice("Server: ${server}")
}

# Test 2: each with array and index parameter
$fruits = ['apple', 'banana', 'cherry']
notice("Test 2: each with array (index, value)")
each($fruits) |$idx, $fruit| {
  notice("Fruit ${idx}: ${fruit}")
}

# Test 3: each with hash and key/value parameters
$config = {
  'host' => 'localhost',
  'port' => 8080
}
notice("Test 3: each with hash (key, value)")
each($config) |$key, $value| {
  notice("Config ${key}=${value}")
}

# Test 4: each creating resources
$users = ['alice', 'bob']
notice("Test 4: each creating resources")
each($users) |$user| {
  notify { "user-notify-${user}":
    message => "Created user ${user}",
  }
}

# Test 5: nested each
$matrix = [['a', 'b'], ['c', 'd']]
notice("Test 5: nested each")
each($matrix) |$row_idx, $row| {
  each($row) |$col_idx, $cell| {
    notice("Matrix[${row_idx}][${col_idx}] = ${cell}")
  }
}

notice("=== Iterator Tests Complete ===")
