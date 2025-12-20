# Test join() function

# Join array from split (basic test)
$parts = split('a,b,c', ',')
$joined = join($parts, ':')
notice("Joined with colon: ${joined}")

# Join with multi-char separator
$words = split('one|two|three', '|')
$sentence = join($words, ' - ')
notice("Joined with dash: ${sentence}")

# Round-trip test (split then join with same separator)
$original = 'x:y:z'
$split_parts = split($original, ':')
$rejoined = join($split_parts, ':')
notice("Round-trip: ${rejoined}")

notice("join() function tests completed")
