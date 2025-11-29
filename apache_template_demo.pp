# Apache Template Demo
# Shows working template processing with module structure

# Server configuration variables  
$server_name = "web01.example.com"
$listen_port = 80
$ssl_port = 443
$admin_email = "webmaster@example.com"
$document_root = "/var/www/html"

# Apache virtual host variables
$port = 80
$docroot = "/var/www/example.com"
$ssl = false
$server_alias = "www.example.com"
$ssl_cert = "/etc/ssl/certs/example.com.crt"
$ssl_key = "/etc/ssl/private/example.com.key"

# Generate configurations from module templates
$apache_main = template("modules/apache/templates/apache2.conf.erb")
$vhost_config = template("modules/apache/templates/vhost.erb")

# Display what would be configured
$message = "Configurations generated from module templates"