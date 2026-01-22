# Test resource chains (-> and ~>)

# Test 1: Simple before chain
notify { 'chain_first':
  message => 'First in chain',
}

notify { 'chain_second':
  message => 'Second in chain',
}

Notify['chain_first'] -> Notify['chain_second']

# Test 2: Notify chain
notify { 'chain_source':
  message => 'Source of notify',
}

notify { 'chain_target':
  message => 'Target of notify',
}

Notify['chain_source'] ~> Notify['chain_target']

# Test 3: Multi-step chain
notify { 'multi_a':
  message => 'Multi A',
}

notify { 'multi_b':
  message => 'Multi B',
}

notify { 'multi_c':
  message => 'Multi C',
}

Notify['multi_a'] -> Notify['multi_b'] ~> Notify['multi_c']

# Test 4: Reverse arrow
notify { 'rev_after':
  message => 'Comes after',
}

notify { 'rev_before':
  message => 'Comes before',
}

Notify['rev_after'] <- Notify['rev_before']
