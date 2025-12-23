# Test math functions: abs, min, max

# Test abs()
notice("abs(5): ", abs(5))
notice("abs(-5): ", abs(-5))
notice("abs(0): ", abs(0))
notice("abs(-3.14): ", abs(-3.14))
notice("abs(3.14): ", abs(3.14))

# Test min() with multiple arguments
notice("min(3, 1, 2): ", min(3, 1, 2))
notice("min(-5, 5): ", min(-5, 5))
notice("min(1): ", min(1))
notice("min(10, 20, 5, 15): ", min(10, 20, 5, 15))

# Test min() with array
$nums = [5, 2, 8, 1, 9]
notice("min([5, 2, 8, 1, 9]): ", min($nums))

# Test max() with multiple arguments
notice("max(3, 1, 2): ", max(3, 1, 2))
notice("max(-5, 5): ", max(-5, 5))
notice("max(1): ", max(1))
notice("max(10, 20, 5, 15): ", max(10, 20, 5, 15))

# Test max() with array
notice("max([5, 2, 8, 1, 9]): ", max($nums))

# Combined example
$values = [-3, 7, -1, 4]
notice("values: ", $values)
notice("min(values): ", min($values))
notice("max(values): ", max($values))
notice("abs(min(values)): ", abs(min($values)))

notice("Math function tests completed!")
