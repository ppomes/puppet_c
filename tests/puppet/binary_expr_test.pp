# Test complex binary expressions with chained access

$data = {
  'os' => {
    'family' => 'Debian',
    'name' => 'Ubuntu',
    'release' => { 'major' => '22.04' }
  }
}

# Test triple AND with simple values
$simple_and = false and true and true
notify { 'simple_and': message => "false and true and true = ${simple_and}" }

# Test triple AND with comparisons (the mysql pattern)
$triple_cmp = $data['os']['family'] == 'RedHat' and $data['os']['release']['major'] < '7' and $data['os']['name'] != 'Amazon'
notify { 'triple_cmp': message => "RedHat check (should be false): ${triple_cmp}" }

# Test triple AND where first is false (short-circuit)
if $data['os']['family'] == 'RedHat' and $data['os']['release']['major'] < '7' and $data['os']['name'] != 'Amazon' {
  fail('Triple AND short-circuit failed - should not reach here')
} else {
  notify { 'shortcircuit_ok': message => 'Short-circuit evaluation works' }
}

# Test triple AND where all true
$all_true = $data['os']['family'] == 'Debian' and $data['os']['name'] == 'Ubuntu' and $data['os']['release']['major'] == '22.04'
notify { 'all_true': message => "All conditions true: ${all_true}" }

# Test parenthesized expressions
$parens = (false) and (true)
notify { 'parens': message => "(false) and (true) = ${parens}" }

# Test nested parentheses
$nested_parens = (false and true) and (true)
notify { 'nested_parens': message => "(false and true) and (true) = ${nested_parens}" }

# Test OR expression
$or_test = false or true or false
notify { 'or_test': message => "false or true or false = ${or_test}" }

# Test mixed AND/OR (AND has higher precedence)
$mixed = true or false and false
notify { 'mixed': message => "true or false and false = ${mixed}" }
