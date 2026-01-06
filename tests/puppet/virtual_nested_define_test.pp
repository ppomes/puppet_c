# Test: Virtual resources with nested define types
# Tests that virtual resources using nested defines (defines declared inside classes)
# are properly realized by collectors

notice("=== Testing Virtual Resources with Nested Defines ===")

# Test 1: Virtual resource using nested define with short name
notice("Test 1: Virtual resource with nested define (short name)")
class myclass {
  define inner($msg = 'default') {
    notify { "inner_${title}": message => $msg; }
  }

  # Declare virtual resource using short define name
  @inner { 'virt1': msg => 'from virtual resource' }
}

include myclass
Inner<| |>  # Collector should find myclass::inner and execute it

# Test 2: Virtual resource with nested define and filter
notice("Test 2: Virtual resource with filter")
class filterclass {
  define filtered($tag_val) {
    notify { "filtered_${title}": message => "tag=${tag_val}"; }
  }

  @filtered { 'item1': tag_val => 'a' }
  @filtered { 'item2': tag_val => 'b' }
  @filtered { 'item3': tag_val => 'a' }
}

include filterclass
# Only realize items with specific tag
Filtered<| tag_val == 'a' |>

# Test 3: Nested define inside namespaced class
notice("Test 3: Nested define in namespaced class")
class outer::inner {
  define deep($val) {
    notify { "deep_${title}": message => $val; }
  }

  @deep { 'test': val => 'from namespaced class' }
}

include outer::inner
Deep<| |>

notice("=== Virtual Nested Define Tests Complete ===")
