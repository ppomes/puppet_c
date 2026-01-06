# Site manifest for puppetc
# Main entry point - includes classes from modules

# Demo node for Docker/quick start
node 'mynode.example.com' {
  include base_config
  include app_config
  include exec_examples
  include file_examples
  include cron_examples
  include host_examples
  include group_examples
  include user_examples
  include sysctl_examples
  include mount_examples
  include virtual_examples
}

# Vagrant nodes
node 'puppet-server', 'puppet-agent' {
  include base_config
  include app_config
  include file_examples
}

# Default fallback for any other node
node default {
  notify { 'welcome':
    message => 'Configuring node with puppetc',
  }

  include base_config
  include app_config

  notify { 'complete':
    message => 'Puppet run completed successfully!',
  }
}
