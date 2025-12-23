# Test array functions: reverse, unique, sort, flatten

# Test reverse()
$arr = ['a', 'b', 'c', 'd']
notice("reverse(['a', 'b', 'c', 'd']): ", reverse($arr))

$nums = [1, 2, 3]
notice("reverse([1, 2, 3]): ", reverse($nums))

$single = ['only']
notice("reverse(['only']): ", reverse($single))

$empty = []
notice("reverse([]): ", reverse($empty))

# Test unique()
$dups = ['a', 'b', 'a', 'c', 'b', 'a']
notice("unique(['a', 'b', 'a', 'c', 'b', 'a']): ", unique($dups))

$no_dups = ['x', 'y', 'z']
notice("unique(['x', 'y', 'z']): ", unique($no_dups))

$num_dups = [1, 2, 1, 3, 2, 1]
notice("unique([1, 2, 1, 3, 2, 1]): ", unique($num_dups))

# Test sort()
$unsorted = ['cherry', 'apple', 'banana']
notice("sort(['cherry', 'apple', 'banana']): ", sort($unsorted))

$nums_unsorted = [3, 1, 4, 1, 5, 9, 2, 6]
notice("sort([3, 1, 4, 1, 5, 9, 2, 6]): ", sort($nums_unsorted))

# Test flatten()
$nested = [['a', 'b'], ['c', 'd']]
notice("flatten([['a', 'b'], ['c', 'd']]): ", flatten($nested))

$deep = [1, [2, [3, 4]], 5]
notice("flatten([1, [2, [3, 4]], 5]): ", flatten($deep))

$flat = ['already', 'flat']
notice("flatten(['already', 'flat']): ", flatten($flat))

# Combined operations
$data = ['b', 'a', 'c', 'a', 'b']
notice("Original: ", $data)
notice("unique + sort: ", sort(unique($data)))

notice("Array function tests completed!")
