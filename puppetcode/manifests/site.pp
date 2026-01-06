# Site manifest for puppetc
# Main entry point - includes classes from modules

# Demo node for puppetc quick start
node 'mynode.example.com' {
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
  include user_examples
  include sysctl_examples
  include mount_examples
  include virtual_examples

  notify { 'complete':
    message => 'Puppet run completed successfully!',
  }
}
