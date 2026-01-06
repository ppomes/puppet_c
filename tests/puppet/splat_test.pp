# Splat Operator Test
# Tests the * => hash attribute expansion

$attrs = { 'mode' => '0644', 'owner' => 'root', 'group' => 'root' }

file { '/tmp/splat_test1':
  ensure  => file,
  * => $attrs,
  content => 'Test with splat',
}

# Test splat with hash subtraction (common in stdlib::manage)
$full_attrs = { 'ensure' => 'file', 'mode' => '0755', 'content' => 'Direct', 'extra' => 'unused' }
$filtered = $full_attrs - 'content' - 'extra'

file { '/tmp/splat_test2':
  * => $filtered,
  content => 'Overridden content',
}

notice('Splat tests completed')
