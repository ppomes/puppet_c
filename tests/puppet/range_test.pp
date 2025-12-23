# Test range() function

# Numeric ranges
notice("range(1, 5): ", range(1, 5))
notice("range(0, 3): ", range(0, 3))
notice("range(5, 1): ", range(5, 1))

# With step
notice("range(1, 10, 2): ", range(1, 10, 2))
notice("range(0, 10, 3): ", range(0, 10, 3))
notice("range(10, 1, 2): ", range(10, 1, 2))

# Character ranges
notice("range('a', 'e'): ", range('a', 'e'))
notice("range('A', 'E'): ", range('A', 'E'))
notice("range('e', 'a'): ", range('e', 'a'))

# Character range with step
notice("range('a', 'z', 5): ", range('a', 'z', 5))

# Practical usage with size
$nums = range(1, 10)
notice("size(range(1, 10)): ", size($nums))
notice("first(range(1, 10)): ", first($nums))
notice("last(range(1, 10)): ", last($nums))

notice("Range function tests completed!")
