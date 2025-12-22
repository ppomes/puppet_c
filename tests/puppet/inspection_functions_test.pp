# Test inspection functions: size, empty, keys, values

# Test size() with strings
notice("size('hello'): ", size('hello'))
notice("size(''): ", size(''))

# Test size() with arrays
$arr = ['a', 'b', 'c']
$empty_arr = []
notice("size(['a', 'b', 'c']): ", size($arr))
notice("size([]): ", size($empty_arr))

# Test size() with hash
$hash = { 'name' => 'puppet', 'version' => '8' }
notice("size(hash): ", size($hash))

# Test empty()
notice("empty(''): ", empty(''))
notice("empty('hello'): ", empty('hello'))
notice("empty([]): ", empty($empty_arr))
notice("empty(['a']): ", empty(['a']))

# Test keys() and values()
$config = { 'host' => 'localhost', 'port' => '8080' }
notice("keys(config): ", keys($config))
notice("values(config): ", values($config))

# Test with split() result (runtime array)
$parts = split('one:two:three', ':')
notice("size(split result): ", size($parts))

# Test length() alias
notice("length('test'): ", length('test'))
notice("length(['x', 'y']): ", length(['x', 'y']))

notice("Inspection function tests completed!")
