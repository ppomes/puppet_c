# Test interpolation with facts
# Should work with facts when --facts is provided

$local_var = "test"

# Single quotes - should stay literal
$literal_with_facts = 'System: ${operatingsystem}, Local: ${local_var}'

# Double quotes - should interpolate both facts and variables  
$interpolated_mixed = "System: ${operatingsystem}, Local: ${local_var}"
$system_description = "Running ${operatingsystem} ${operatingsystemrelease} on ${hostname}"