# Test array functions: concat, delete, delete_at, first, last

# Test concat()
$a1 = ['a', 'b']
$a2 = ['c', 'd']
notice("concat(['a', 'b'], ['c', 'd']): ", concat($a1, $a2))
notice("concat([1], [2], [3]): ", concat([1], [2], [3]))
notice("concat(['x']): ", concat(['x']))

# Test delete() with array
$arr = ['a', 'b', 'c', 'b', 'd']
notice("delete(['a', 'b', 'c', 'b', 'd'], 'b'): ", delete($arr, 'b'))
notice("delete(['a', 'b', 'c'], 'x'): ", delete(['a', 'b', 'c'], 'x'))

$nums = [1, 2, 3, 2, 4]
notice("delete([1, 2, 3, 2, 4], 2): ", delete($nums, 2))

# Test delete() with hash
$hash = { 'a' => '1', 'b' => '2', 'c' => '3' }
notice("delete({a=>1, b=>2, c=>3}, 'b'): ", delete($hash, 'b'))

# Test delete_at()
$letters = ['a', 'b', 'c', 'd']
notice("delete_at(['a', 'b', 'c', 'd'], 0): ", delete_at($letters, 0))
notice("delete_at(['a', 'b', 'c', 'd'], 1): ", delete_at($letters, 1))
notice("delete_at(['a', 'b', 'c', 'd'], 3): ", delete_at($letters, 3))

# Test first()
notice("first(['a', 'b', 'c']): ", first(['a', 'b', 'c']))
notice("first([1, 2, 3]): ", first([1, 2, 3]))

# Test last()
notice("last(['a', 'b', 'c']): ", last(['a', 'b', 'c']))
notice("last([1, 2, 3]): ", last([1, 2, 3]))

# Combined operations
$data = ['x', 'y', 'z']
notice("first + last: ", first($data), " and ", last($data))
notice("delete_at(concat): ", delete_at(concat(['a'], ['b', 'c']), 1))

notice("Array functions (part 2) tests completed!")
