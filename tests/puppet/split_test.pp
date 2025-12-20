# Test split() function

# Basic split with comma
$parts = split('a,b,c', ',')
notice("Split result count test")

# Split with colon separator
$path = split('/usr/local/bin', '/')
notice("Path split test")

# Split with multi-char pattern
$words = split('one--two--three', '--')
notice("Multi-char delimiter test")

notice("split() function tests completed")
