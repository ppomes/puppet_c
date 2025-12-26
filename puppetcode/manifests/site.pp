# Site manifest for puppetc
# Main entry point - includes classes from modules

# ============================================================================
# Node definitions
# ============================================================================

# Vagrant agent node
node 'puppet-agent' {
  notify { 'welcome':
    message => 'Configuring puppet-agent with puppetc',
  }

  include base_config
  include app_config
  include exec_examples
  include file_examples

  notify { 'complete':
    message => 'Puppet run completed successfully!',
  }
}

# Default node - catches any unmatched node names
node default {
  notify { 'welcome':
    message => 'Configuring node with puppetc',
  }

  include base_config
  include app_config
  include exec_examples
  include file_examples
  include cron_examples
  include host_examples
  include group_examples

  notify { 'complete':
    message => 'Puppet run completed successfully!',
  }
}
