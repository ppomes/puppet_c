# Test chained bracket access (2 and 3 levels deep)

# Test 2-level access
$hash = { 'os' => { 'family' => 'Debian', 'name' => 'Ubuntu' } }
$family = $hash['os']['family']
notify { 'two_level': message => "Family: ${family}" }

# Test 3-level access
$nested = { 'os' => { 'release' => { 'major' => '22.04' } } }
$major = $nested['os']['release']['major']
notify { 'three_level': message => "Major: ${major}" }

# Test in case statement
case $hash['os']['family'] {
  'Debian': {
    notify { 'case_match': message => 'Matched Debian in case' }
  }
  default: {
    fail('Case statement chained access failed')
  }
}

# Test in if condition
if $hash['os']['name'] == 'Ubuntu' {
  notify { 'if_match': message => 'Matched Ubuntu in if' }
} else {
  fail('If condition chained access failed')
}

# Test in string interpolation
notify { 'interpolation': message => "OS: ${hash['os']['name']}, Family: ${hash['os']['family']}" }
