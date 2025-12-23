# Test lookup functions: has_key, member

# Test has_key()
$config = { 'host' => 'localhost', 'port' => '8080', 'debug' => 'true' }

notice("has_key(config, 'host'): ", has_key($config, 'host'))
notice("has_key(config, 'port'): ", has_key($config, 'port'))
notice("has_key(config, 'missing'): ", has_key($config, 'missing'))
notice("has_key(config, 'HOST'): ", has_key($config, 'HOST'))

# Test with empty hash
$empty_hash = {}
notice("has_key({}, 'key'): ", has_key($empty_hash, 'key'))

# Test member()
$fruits = ['apple', 'banana', 'cherry']

notice("member(['apple', 'banana', 'cherry'], 'banana'): ", member($fruits, 'banana'))
notice("member(['apple', 'banana', 'cherry'], 'grape'): ", member($fruits, 'grape'))
notice("member(['apple', 'banana', 'cherry'], 'Apple'): ", member($fruits, 'Apple'))

# Test with numbers
$nums = [1, 2, 3, 4, 5]
notice("member([1, 2, 3, 4, 5], 3): ", member($nums, 3))
notice("member([1, 2, 3, 4, 5], 6): ", member($nums, 6))

# Test with empty array
$empty_arr = []
notice("member([], 'anything'): ", member($empty_arr, 'anything'))

# Practical example: conditional check
$allowed_envs = ['dev', 'staging', 'prod']
$current_env = 'staging'

if member($allowed_envs, $current_env) {
  notice("Environment '", $current_env, "' is allowed")
}

notice("Lookup function tests completed!")
