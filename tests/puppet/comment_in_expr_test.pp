# Test: Comments in binary expressions
# Tests that inline comments in multiline OR/AND expressions don't break parsing

notice("=== Testing Comments in Binary Expressions ===")

# Test 1: Multiline OR with comments
notice("Test 1: Multiline OR with inline comments")
$profile = ['admin', 'dev']
$env = 'preprod'
$sfl = false

if( ('old' in $profile) or                    # comment 1
    ('sfl' in $profile and $env != 'prod') or # comment 2
    ('adminbureau' in $profile) )             # comment 3
{
  notice("Test 1 FAILED: Should not enter IF branch")
  fail("Multiline OR with comments evaluated incorrectly")
}
else
{
  notice("Test 1 PASSED: Correctly entered ELSE branch")
}

# Test 2: Multiline AND with comments
notice("Test 2: Multiline AND with inline comments")
$a = true
$b = true
$c = true

if( $a and  # first condition
    $b and  # second condition
    $c )    # third condition
{
  notice("Test 2 PASSED: AND conditions all true")
}
else
{
  notice("Test 2 FAILED: Should have entered IF branch")
  fail("Multiline AND with comments evaluated incorrectly")
}

# Test 3: Mixed OR/AND with comments
notice("Test 3: Mixed OR/AND with comments")
$x = false
$y = true
$z = false

if( ($x and $y) or  # first group
    ($y and $z) or  # second group
    $x )            # third condition
{
  notice("Test 3 FAILED: Should not enter IF branch")
  fail("Mixed OR/AND with comments evaluated incorrectly")
}
else
{
  notice("Test 3 PASSED: Correctly entered ELSE branch")
}

# Test 4: Complex condition like in production code
notice("Test 4: Production-style complex condition")
$user_profile = ['admin', 'dev']
$environment = 'preprod'
$sfl_enabled = false
$adminbureau_enabled = false

if( ('old' in $user_profile) or                                                    # Revoked User
    ('sfl' in $user_profile and $environment != 'prod') or                         # SFL not in production
    ('adminbureau' in $user_profile and $environment == 'prod' and $adminbureau_enabled == false) or  # adminbureau in prod
    ('sfl' in $user_profile and $sfl_enabled == false) )                           # SFL and sfl disabled
{
  notice("Test 4 FAILED: User should be PRESENT not ABSENT")
  fail("Production-style condition evaluated incorrectly")
}
else
{
  notice("Test 4 PASSED: User correctly evaluated as PRESENT")
}

# Test 5: Comparison operators with comments
notice("Test 5: Comparison operators with comments")
$val = 10

if( $val > 5 and   # greater than 5
    $val < 20 and  # less than 20
    $val != 15 )   # not equal to 15
{
  notice("Test 5 PASSED: All comparisons correct")
}
else
{
  notice("Test 5 FAILED: Comparison operators failed")
  fail("Comparison operators with comments failed")
}

notice("=== Comment in Expression Tests Complete ===")
