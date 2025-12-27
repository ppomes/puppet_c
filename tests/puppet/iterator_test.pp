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

# Test 6: map with function call in lambda
$numbers = [1, 2, 3]
notice("Test 6: map with array")
map($numbers) |$n| {
  notice("Mapping ${n}")
}
notice("Map complete")

# Test 7: map with hash
$ports = {
  'http' => 80,
  'https' => 443
}
notice("Test 7: map over hash")
map($ports) |$name, $port| {
  notice("Port ${name}: ${port}")
}

# Test 8: filter with function call
# (Expression-body lambdas like |$v| { $v } not yet supported)
$values = ['one', 'two', 'three']
notice("Test 8: filter test")
filter($values) |$v| {
  notice("Filtering: ${v}")
}

# Test 9: reduce with array (2 params: memo, value)
$items = ['a', 'b', 'c']
notice("Test 9: reduce with array")
reduce($items, 'start') |$memo, $item| {
  notice("Reduce: memo=${memo}, item=${item}")
}

# Test 10: reduce with array (3 params: memo, index, value)
$letters = ['x', 'y', 'z']
notice("Test 10: reduce with index")
reduce($letters, 0) |$memo, $idx, $letter| {
  notice("Reduce: memo=${memo}, idx=${idx}, letter=${letter}")
}

# Test 11: Expression-body lambda with map (arithmetic)
$nums = [1, 2, 3]
notice("Test 11: map with expression body")
$doubled = map($nums) |$n| {
  $n * 2
}
each($doubled) |$v| {
  notice("Doubled: ${v}")
}

# Test 12: Expression-body lambda with reduce (sum)
$to_sum = [10, 20, 30]
$total = reduce($to_sum, 0) |$memo, $n| {
  $memo + $n
}
notice("Test 12: reduce sum = ${total}")

# Test 13: Expression-body lambda with filter (comparison)
$all_values = [1, 5, 2, 8, 3]
$big_values = filter($all_values) |$v| {
  $v > 3
}
notice("Test 13: filter big values")
each($big_values) |$v| {
  notice("Big: ${v}")
}

notice("=== Iterator Tests Complete ===")
