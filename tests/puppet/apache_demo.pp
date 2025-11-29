# Apache configuration demo using templates
# This demonstrates real-world configuration management with the Puppet C parser

# Server configuration variables
$server_name = "web01.example.com"
$listen_port = 80
$ssl_port = 443
$admin_email = "webmaster@example.com"
$document_root = "/var/www/html"

# Generate Apache main configuration
$apache_config = template("tests/puppet/apache2.conf.erb")

# Generate virtual host configurations
$vhost_config = template("tests/puppet/apache_vhost.erb")

# Display the generated configurations
$status = "Apache configuration generated successfully"