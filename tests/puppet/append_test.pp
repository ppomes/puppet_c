# Test array/hash append (+=)

# Test 1: New array from append
$arr1 = ['initial']
$arr1 += ['appended']

notify { 'test1':
  message => "Test 1 - Array append: ${arr1}",
}

# Test 2: Append single value to array (should wrap in array)
$arr2 = ['a', 'b']
$arr2 += ['c']

notify { 'test2':
  message => "Test 2 - Concatenate arrays: ${arr2}",
}

# Test 3: Append to undefined (creates new array)
$arr3 += ['from_nothing']

notify { 'test3':
  message => "Test 3 - Append to undef: ${arr3}",
}

# Test 4: Hash merge
$hash1 = { 'key1' => 'val1' }
$hash1 += { 'key2' => 'val2' }

notify { 'test4':
  message => "Test 4 - Hash merge: ${hash1}",
}
