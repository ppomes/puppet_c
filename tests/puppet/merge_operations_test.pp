# Hash and Array Merge Operations Test
# Tests binary + operator for hash merge, array concatenation, and string concatenation

# Hash merge tests
$hash1 = { 'a' => 1, 'b' => 2 }
$hash2 = { 'c' => 3, 'd' => 4 }
$merged_hash = $hash1 + $hash2
notice("Hash merge: ${merged_hash}")

# Hash merge with override (right overwrites left)
$hash3 = { 'a' => 1, 'b' => 2 }
$hash4 = { 'b' => 99, 'c' => 3 }
$overridden = $hash3 + $hash4
notice("Hash override: ${overridden}")

# Undef + hash should return the hash
$maybe_hash = undef
$default_hash = { 'default' => true }
$result_hash = $maybe_hash + $default_hash
notice("Undef + hash: ${result_hash}")

# Array concatenation tests
$arr1 = [1, 2, 3]
$arr2 = [4, 5, 6]
$concat_arr = $arr1 + $arr2
notice("Array concat: ${concat_arr}")

# Array with different types
$arr3 = ['a', 'b']
$arr4 = ['c', 'd', 'e']
$concat_mixed = $arr3 + $arr4
notice("String array concat: ${concat_mixed}")

# String concatenation
$str1 = 'Hello'
$str2 = ' World'
$concat_str = $str1 + $str2
notice("String concat: ${concat_str}")

# Number addition (still works)
$num1 = 10
$num2 = 25
$sum = $num1 + $num2
notice("Number add: ${sum}")

# Hash subtraction tests
$hash5 = { 'a' => 1, 'b' => 2, 'c' => 3 }
$minus_b = $hash5 - 'b'
notice("Hash minus 'b': ${minus_b}")

$minus_ac = $hash5 - 'a' - 'c'
notice("Hash minus 'a' and 'c': ${minus_ac}")

# Array subtraction tests
$arr5 = [1, 2, 3, 4, 5]
$arr6 = [2, 4]
$diff_arr = $arr5 - $arr6
notice("Array difference: ${diff_arr}")

notice('Merge operations tests completed')
