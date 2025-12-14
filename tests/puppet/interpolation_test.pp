# Test string interpolation - single vs double quotes
$name = "Alice"
$count = 42

# Single quotes should be literal
$literal = 'Name is ${name} and count is ${count}'

# Double quotes should interpolate
$interpolated = "Name is ${name} and count is ${count}"

# Mixed interpolation
$mixed = "Hello ${name}! You have ${count} items."