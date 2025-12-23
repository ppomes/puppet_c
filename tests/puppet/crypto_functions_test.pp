# Test crypto functions: sha1, md5, base64

# Test sha1()
notice("sha1('hello'): ", sha1('hello'))
notice("sha1('world'): ", sha1('world'))
notice("sha1(''): ", sha1(''))
notice("sha1('The quick brown fox'): ", sha1('The quick brown fox'))

# Test md5()
notice("md5('hello'): ", md5('hello'))
notice("md5('world'): ", md5('world'))
notice("md5(''): ", md5(''))
notice("md5('The quick brown fox'): ", md5('The quick brown fox'))

# Test base64() encode
notice("base64('encode', 'hello'): ", base64('encode', 'hello'))
notice("base64('encode', 'Hello World!'): ", base64('encode', 'Hello World!'))
notice("base64('encode', ''): ", base64('encode', ''))

# Test base64() decode
notice("base64('decode', 'aGVsbG8='): ", base64('decode', 'aGVsbG8='))
notice("base64('decode', 'SGVsbG8gV29ybGQh'): ", base64('decode', 'SGVsbG8gV29ybGQh'))

# Round-trip test
$original = 'Puppet is awesome!'
$encoded = base64('encode', $original)
$decoded = base64('decode', $encoded)
notice("Original: ", $original)
notice("Encoded: ", $encoded)
notice("Decoded: ", $decoded)

# Practical example: generate content hash
$content = 'This is the file content'
notice("Content hash (sha1): ", sha1($content))
notice("Content hash (md5): ", md5($content))

notice("Crypto function tests completed!")
