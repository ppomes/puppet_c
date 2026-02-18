# Test resource overrides on virtual and exported resources

# Test 1: Override on a virtual resource
@notify { 'virtual_with_override':
  message => 'original message',
}

Notify['virtual_with_override'] {
  message => 'overridden message',
}

realize(Notify['virtual_with_override'])

# Test 2: Override on an exported resource
@@notify { 'exported_with_override':
  message => 'original exported',
}

Notify['exported_with_override'] {
  message => 'overridden exported',
}

# Test 3: Conditional override on virtual resource (like nagios pattern)
$use_custom = true

@notify { 'conditional_override':
  message => 'base message',
}

if ($use_custom) {
  Notify['conditional_override'] {
    message => 'conditional override applied',
  }
}

realize(Notify['conditional_override'])
