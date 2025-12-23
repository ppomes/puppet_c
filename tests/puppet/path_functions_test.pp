# Test path functions: basename, dirname, extname

# Test basename()
notice("basename('/etc/passwd'): ", basename('/etc/passwd'))
notice("basename('/etc/nginx/nginx.conf'): ", basename('/etc/nginx/nginx.conf'))
notice("basename('/var/log/'): ", basename('/var/log/'))
notice("basename('file.txt'): ", basename('file.txt'))

# Test basename with suffix
notice("basename('file.txt', '.txt'): ", basename('file.txt', '.txt'))
notice("basename('archive.tar.gz', '.gz'): ", basename('archive.tar.gz', '.gz'))
notice("basename('config.yaml', '.json'): ", basename('config.yaml', '.json'))

# Test dirname()
notice("dirname('/etc/passwd'): ", dirname('/etc/passwd'))
notice("dirname('/etc/nginx/nginx.conf'): ", dirname('/etc/nginx/nginx.conf'))
notice("dirname('/etc'): ", dirname('/etc'))
notice("dirname('file.txt'): ", dirname('file.txt'))
notice("dirname('/'): ", dirname('/'))

# Test extname()
notice("extname('file.txt'): ", extname('file.txt'))
notice("extname('archive.tar.gz'): ", extname('archive.tar.gz'))
notice("extname('README'): ", extname('README'))
notice("extname('/path/to/file.conf'): ", extname('/path/to/file.conf'))
notice("extname('.bashrc'): ", extname('.bashrc'))
notice("extname('.hidden.txt'): ", extname('.hidden.txt'))

# Practical examples
$filepath = '/var/log/nginx/access.log'
notice("Full path: ", $filepath)
notice("Directory: ", dirname($filepath))
notice("Filename: ", basename($filepath))
notice("Extension: ", extname($filepath))
notice("Name without ext: ", basename($filepath, extname($filepath)))

notice("Path function tests completed!")
