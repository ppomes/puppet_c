# Main site.pp manifest
$global_var = "production"

include myapp
include database::mysql