# Test hash functions: merge, delete

# Test merge()
$h1 = { 'a' => '1', 'b' => '2' }
$h2 = { 'c' => '3', 'd' => '4' }
notice("merge({a=>1, b=>2}, {c=>3, d=>4}): ", merge($h1, $h2))

# Test merge with override
$base = { 'name' => 'default', 'port' => '80' }
$override = { 'port' => '8080' }
notice("merge with override: ", merge($base, $override))

# Test merge multiple hashes
$h3 = { 'e' => '5' }
notice("merge 3 hashes: ", merge($h1, $h2, $h3))

# Test delete() with hash (already implemented)
$config = { 'host' => 'localhost', 'port' => '8080', 'debug' => 'true' }
notice("delete(hash, 'debug'): ", delete($config, 'debug'))

# Practical example
$defaults = { 'timeout' => '30', 'retries' => '3' }
$custom = { 'timeout' => '60' }
$final = merge($defaults, $custom)
notice("Final config: ", $final)

notice("Hash function tests completed!")
