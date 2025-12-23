# Test conditional statements: if, elsif, else, unless, case, ternary

# =====================
# Test if/elsif/else
# =====================
notice("=== Testing if/elsif/else ===")

$x = 10

if $x > 15 {
  notice("x > 15")
} elsif $x > 5 {
  notice("x > 5 (should see this)")
} else {
  notice("x <= 5")
}

# Test simple if
if $x == 10 {
  notice("x equals 10 (should see this)")
}

# Test if with boolean
$flag = true
if $flag {
  notice("flag is true (should see this)")
}

# Test if with false
$other = false
if $other {
  notice("other is true (should NOT see)")
} else {
  notice("other is false (should see this)")
}

# Test if with undef (falsy)
$myundef = undef
if $myundef {
  notice("myundef is truthy (should NOT see)")
} else {
  notice("myundef is falsy (should see this)")
}

# Test if with string (truthy)
$mystr = "hello"
if $mystr {
  notice("mystr is truthy (should see this)")
}

# =====================
# Test unless
# =====================
notice("=== Testing unless ===")

unless $x > 100 {
  notice("x is NOT > 100 (should see this)")
}

unless $flag {
  notice("flag is falsy (should NOT see)")
}

$empty = false
unless $empty {
  notice("empty is falsy, so unless executes (should see this)")
}

# =====================
# Test case
# =====================
notice("=== Testing case ===")

$os = 'linux'

case $os {
  'windows': {
    notice("Running on Windows")
  }
  'darwin': {
    notice("Running on macOS")
  }
  'linux': {
    notice("Running on Linux (should see this)")
  }
  default: {
    notice("Unknown OS")
  }
}

# Test case with numbers
$num = 42

case $num {
  1: { notice("num is 1") }
  42: { notice("num is 42 (should see this)") }
  100: { notice("num is 100") }
  default: { notice("num is something else") }
}

# Test case with default
$unknown = 'mystery'
case $unknown {
  'known': { notice("it's known") }
  default: { notice("it's unknown, using default (should see this)") }
}

# =====================
# Test ternary
# =====================
notice("=== Testing ternary ===")

# Note: Comparisons in ternary conditions require parentheses
$result1 = ($x > 5) ? 'big' : 'small'
notice("(x > 5) ? 'big' : 'small' = ", $result1)

$result2 = $flag ? 'yes' : 'no'
notice("flag ? 'yes' : 'no' = ", $result2)

$result3 = $empty ? 'has value' : 'empty'
notice("empty ? 'has value' : 'empty' = ", $result3)

# Nested ternary
$nested = ($x > 20) ? 'large' : (($x > 5) ? 'medium' : 'small')
notice("nested ternary result = ", $nested)

notice("=== All conditional tests completed! ===")
