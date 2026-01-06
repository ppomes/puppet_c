# EPP Template Test
# Tests epp() function and stdlib::deferrable_epp

$hostname = 'testhost'

# Test basic epp() with parameters
$config1 = epp('tests/puppet/simple.epp', { 'name' => 'myapp', 'port' => 9000 })
notice("EPP Result 1: ${config1}")

# Test epp() with default parameter
$config2 = epp('tests/puppet/simple.epp', { 'name' => 'otherapp' })
notice("EPP Result 2: ${config2}")

# Test stdlib::deferrable_epp (alias to epp)
$config3 = stdlib::deferrable_epp('tests/puppet/simple.epp', { 'name' => 'deferrable', 'port' => 3000 })
notice("Deferrable EPP: ${config3}")

notice('EPP tests completed')
