# Test math functions: floor, ceil, round, sqrt

# Test floor()
notice("floor(3.7): ", floor(3.7))
notice("floor(3.2): ", floor(3.2))
notice("floor(3.0): ", floor(3.0))
notice("floor(-3.2): ", floor(-3.2))
notice("floor(-3.7): ", floor(-3.7))

# Test ceil()
notice("ceil(3.2): ", ceil(3.2))
notice("ceil(3.7): ", ceil(3.7))
notice("ceil(3.0): ", ceil(3.0))
notice("ceil(-3.2): ", ceil(-3.2))
notice("ceil(-3.7): ", ceil(-3.7))

# Test ceiling alias
notice("ceiling(2.1): ", ceiling(2.1))

# Test round()
notice("round(3.4): ", round(3.4))
notice("round(3.5): ", round(3.5))
notice("round(3.6): ", round(3.6))
notice("round(-3.4): ", round(-3.4))
notice("round(-3.5): ", round(-3.5))

# Test sqrt()
notice("sqrt(4): ", sqrt(4))
notice("sqrt(9): ", sqrt(9))
notice("sqrt(2): ", sqrt(2))
notice("sqrt(16): ", sqrt(16))
notice("sqrt(0): ", sqrt(0))

# Combined operations
$val = 10.7
notice("floor(", $val, "): ", floor($val))
notice("ceil(", $val, "): ", ceil($val))
notice("round(", $val, "): ", round($val))

notice("Math functions (part 2) tests completed!")
