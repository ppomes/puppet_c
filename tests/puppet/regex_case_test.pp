# Regex Case Statement Test
# Tests regex matching in case statements

# Test basic regex match
$value1 = 'present'
$result1 = ''
case $value1 {
  /^(present|installed)$/: { $result1 = 'should_install' }
  /^(absent|removed)$/: { $result1 = 'should_remove' }
  default: { $result1 = 'unknown' }
}
notice("Regex case 1: ${result1}")

# Test regex match with different value
$value2 = 'absent'
$result2 = ''
case $value2 {
  /^(present|installed)$/: { $result2 = 'should_install' }
  /^(absent|removed)$/: { $result2 = 'should_remove' }
  default: { $result2 = 'unknown' }
}
notice("Regex case 2: ${result2}")

# Test regex match fallback to default
$value3 = 'purged'
$result3 = ''
case $value3 {
  /^(present|installed)$/: { $result3 = 'should_install' }
  /^(absent|removed)$/: { $result3 = 'should_remove' }
  default: { $result3 = 'unknown' }
}
notice("Regex case 3: ${result3}")

# Test simple regex without alternation
$osfamily = 'Debian'
$result4 = ''
case $osfamily {
  /Debian/: { $result4 = 'apt' }
  /RedHat/: { $result4 = 'yum' }
  default: { $result4 = 'unknown' }
}
notice("OS package manager: ${result4}")

# Test regex at end of string
$filename = 'test.pp'
$result5 = ''
case $filename {
  /\.pp$/: { $result5 = 'puppet' }
  /\.erb$/: { $result5 = 'erb' }
  /\.epp$/: { $result5 = 'epp' }
  default: { $result5 = 'other' }
}
notice("File type: ${result5}")

notice('Regex case tests completed')
