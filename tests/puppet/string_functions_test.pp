# Test string manipulation functions: downcase, upcase, strip

# Test downcase()
$upper = 'HELLO WORLD'
$mixed = 'HeLLo WoRLd'
$lower = 'hello world'

notice("downcase('HELLO WORLD'): ", downcase($upper))
notice("downcase('HeLLo WoRLd'): ", downcase($mixed))
notice("downcase('hello world'): ", downcase($lower))

# Test upcase()
notice("upcase('hello world'): ", upcase($lower))
notice("upcase('HeLLo WoRLd'): ", upcase($mixed))
notice("upcase('HELLO WORLD'): ", upcase($upper))

# Test strip()
$padded = '  hello world  '
$tabs = "\thello\t"
$newlines = "\nhello\n"
$clean = 'hello'

notice("strip('  hello world  '): '", strip($padded), "'")
notice("strip('hello'): '", strip($clean), "'")

# Test chaining
$input = '  HELLO WORLD  '
notice("strip + downcase: '", downcase(strip($input)), "'")
notice("upcase + strip: '", strip(upcase('  hello  ')), "'")

notice("String function tests completed!")
